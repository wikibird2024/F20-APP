// BridgeClient against an in-process fake bridge (a QTcpServer the test
// drives by hand): reply matching, timeouts, disconnects, callback
// lifetime, oversized lines, malformed fields, reconnect alarm, heartbeat.
#include "bridgeclient.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

namespace {

// The bridge side of the socket: collects the request lines, answers when told.
class FakeBridge {
public:
    FakeBridge() {
        server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
            client_ = server_.nextPendingConnection();
            QObject::connect(client_, &QTcpSocket::readyRead, client_, [this] {
                for (const QByteArray& line : client_->readAll().split('\n'))
                    if (!line.isEmpty())
                        requests_.push_back(f20::parseRequest(line.toStdString()).value());
            });
        });
    }

    quint16 port() const { return server_.serverPort(); }
    QTcpSocket* client() const { return client_; }
    const std::vector<f20::Request>& requests() const { return requests_; }
    int lastId() const { return requests_.back().id; }

    void sendLine(const QByteArray& line) {
        client_->write(line + "\n");
        client_->flush();
    }

private:
    QTcpServer server_;
    QPointer<QTcpSocket> client_;
    std::vector<f20::Request> requests_;
};

BridgeClient::Timing fastTiming() {
    BridgeClient::Timing timing;
    timing.requestTimeoutMs = 300;
    timing.reconnectFirstDelayMs = 30;
    timing.reconnectMaxDelayMs = 60;
    timing.heartbeatIntervalMs = 3600 * 1000; // off unless a test turns it on
    return timing;
}

// Port with nothing listening: bind, read the number, close.
quint16 deadPort() {
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

} // namespace

class BridgeClientTest : public QObject {
    Q_OBJECT

private:
    // QTRY_* macros return from the calling function on failure, so the
    // connect step lives in each test via this macro.
#define CONNECT_TO(client, bridge)                                   \
    (client).setTiming(fastTiming());                                \
    (client).connectToBridge("127.0.0.1", (bridge).port());          \
    QTRY_VERIFY((client).isConnected() && (bridge).client() != nullptr)

private slots:
    void replyIsMatchedByIdAndDeliveredOnce() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);

        QObject context;
        int calls = 0;
        f20::Reply seen;
        client.send("getVersion", {}, &context, [&](const f20::Reply& reply) {
            ++calls;
            seen = reply;
        });
        QTRY_COMPARE(bridge.requests().size(), std::size_t{1});
        bridge.sendLine(QByteArray(R"({"id":)") + QByteArray::number(bridge.lastId()) +
                        R"(,"ok":true,"result":{"bridge":"x"}})");
        QTRY_COMPARE(calls, 1);
        QVERIFY(seen.ok);
        QTest::qWait(400); // past the timeout: still exactly one call
        QCOMPARE(calls, 1);
        QVERIFY(client.isConnected());
    }

    void notConnectedErrorComesLaterNotInsideSend() {
        BridgeClient client;
        QObject context;
        bool called = false;
        f20::Reply seen;
        client.send("getStatus", {}, &context, [&](const f20::Reply& reply) {
            called = true;
            seen = reply;
        });
        QVERIFY(!called); // not re-entrant
        QTRY_VERIFY(called);
        QVERIFY(!seen.ok);
        QCOMPARE(QString::fromStdString(seen.errorMessage), QString("bridge not connected"));
    }

    void timeoutAnswersOnceAndDropsTheConnection() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);
        QSignalSpy disconnects(&client, &BridgeClient::disconnected);

        QObject context;
        QStringList order;
        connect(&client, &BridgeClient::disconnected, &context, [&] { order << "disconnected"; });
        client.send("measure", {}, &context, [&](const f20::Reply& reply) {
            order << QString::fromStdString(reply.errorMessage);
        });
        QTRY_COMPARE(order.size(), 2);
        // Fault first, then the handler: the app never shows a short "ready".
        QCOMPARE(order[0], QString("disconnected"));
        QVERIFY(order[1].contains("timeout"));
        QCOMPARE(disconnects.count(), 1);
    }

    void dropFailsEveryOpenRequestAfterDisconnected() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);

        QObject context;
        QStringList order;
        connect(&client, &BridgeClient::disconnected, &context, [&] { order << "disconnected"; });
        client.send("acquireSpectrum", {}, &context, [&](const f20::Reply&) { order << "a"; });
        client.send("analyzeSpectrum", {}, &context, [&](const f20::Reply&) { order << "b"; });
        QTRY_COMPARE(bridge.requests().size(), std::size_t{2});
        bridge.client()->abort(); // the bridge crashes
        QTRY_COMPARE(order, (QStringList{"disconnected", "a", "b"}));
    }

    void destroyedContextIsNeverCalled() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);

        auto* page = new QObject; // the wizard page the operator closes
        bool called = false;
        client.send("baselineStep2", {}, page, [&](const f20::Reply&) { called = true; });
        QTRY_COMPARE(bridge.requests().size(), std::size_t{1});
        delete page;
        bridge.sendLine(QByteArray(R"({"id":)") + QByteArray::number(bridge.lastId()) +
                        R"(,"ok":true,"result":{}})");
        QTest::qWait(200);
        QVERIFY(!called);
        QVERIFY(client.isConnected());
    }

    void oversizedLineDropsTheConnection() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);
        QSignalSpy disconnects(&client, &BridgeClient::disconnected);

        bridge.client()->write(
            QByteArray(static_cast<int>(f20::LineSplitter::kDefaultMaxLineBytes) + 1000, 'x'));
        QTRY_VERIFY(disconnects.count() >= 1);
    }

    void malformedFieldsDoNotCrash() {
        FakeBridge bridge;
        BridgeClient client;
        CONNECT_TO(client, bridge);

        QObject context;
        int calls = 0;
        f20::Reply seen;
        client.send("setRecipe", {{"name", "x"}}, &context, [&](const f20::Reply& reply) {
            ++calls;
            seen = reply;
        });
        QTRY_COMPARE(bridge.requests().size(), std::size_t{1});
        bridge.sendLine("not json at all");
        bridge.sendLine(QByteArray(R"({"id":)") + QByteArray::number(bridge.lastId()) +
                        R"(,"ok":false,"error":{"code":5,"message":[1]}})");
        QTRY_COMPARE(calls, 1);
        QVERIFY(!seen.ok);
        QVERIFY(seen.errorCode.empty());
        QVERIFY(client.isConnected());
    }

    void unreachableBridgeRaisesTheAlarmAfterTheRetries() {
        BridgeClient client;
        client.setTiming(fastTiming());
        QSignalSpy exhausted(&client, &BridgeClient::reconnectExhausted);
        client.connectToBridge("127.0.0.1", deadPort());
        QTRY_COMPARE_WITH_TIMEOUT(exhausted.count(), 1, 3000);
        QTest::qWait(300); // keeps retrying, but the alarm is raised once
        QCOMPARE(exhausted.count(), 1);
    }

    void heartbeatDropsAHungBridge() {
        FakeBridge bridge; // answers nothing
        BridgeClient client;
        BridgeClient::Timing timing = fastTiming();
        timing.heartbeatIntervalMs = 50;
        timing.heartbeatTimeoutMs = 50;
        client.setTiming(timing);
        client.connectToBridge("127.0.0.1", bridge.port());
        QTRY_VERIFY(client.isConnected());
        QSignalSpy disconnects(&client, &BridgeClient::disconnected);
        QTRY_VERIFY_WITH_TIMEOUT(disconnects.count() >= 1, 3000);
        QVERIFY(bridge.requests().size() >= 2); // two missed heartbeats
        QCOMPARE(QString::fromStdString(bridge.requests()[0].cmd), QString("getStatus"));
    }

    void answeredHeartbeatKeepsTheConnection() {
        FakeBridge bridge;
        BridgeClient client;
        BridgeClient::Timing timing = fastTiming();
        timing.heartbeatIntervalMs = 50;
        timing.heartbeatTimeoutMs = 200;
        client.setTiming(timing);
        client.connectToBridge("127.0.0.1", bridge.port());
        QTRY_VERIFY(client.isConnected() && bridge.client() != nullptr);
        QSignalSpy disconnects(&client, &BridgeClient::disconnected);

        // Answer every heartbeat with "busy": still alive.
        std::size_t answered = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        while (elapsed.elapsed() < 600) {
            QTest::qWait(10);
            while (answered < bridge.requests().size()) {
                bridge.sendLine(QByteArray(R"({"id":)") +
                                QByteArray::number(bridge.requests()[answered].id) +
                                R"(,"ok":false,"error":{"code":"busy","message":""}})");
                ++answered;
            }
        }
        QVERIFY(answered >= 3);
        QCOMPARE(disconnects.count(), 0);
    }
};

int runBridgeClientTests(int argc, char** argv) {
    BridgeClientTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_bridgeclient.moc"
