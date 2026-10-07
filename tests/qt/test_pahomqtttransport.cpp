// PahoMqttTransport against a real broker on 127.0.0.1:1883 (Mosquitto on
// the dev PC). Skipped when no broker listens there, so ctest still passes
// on a machine without one. Each test uses its own random topic and client
// id, so other traffic on the broker does not matter.
#include "pahomqtttransport.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QPointer>
#include <QUuid>

#include <memory>
#include <vector>

namespace
{

const char *const kBrokerHost = "127.0.0.1";
const quint16     kBrokerPort = 1883;

bool brokerIsRunning()
{
    QTcpSocket probe;
    probe.connectToHost(kBrokerHost, kBrokerPort);
    return probe.waitForConnected(500);
}

QString uniqueName(const char *prefix)
{
    return prefix + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

MqttTransport::Settings brokerSettings()
{
    MqttTransport::Settings settings;
    settings.host = kBrokerHost;
    settings.port = kBrokerPort;
    settings.clientId = uniqueName("f20test-");
    settings.reconnectFirstDelayMs = 50;
    settings.reconnectMaxDelayMs = 100;
    return settings;
}

// Port with nothing listening: bind, read the number, close.
quint16 deadPort()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    const quint16 port = server.serverPort();
    server.close();
    return port;
}

// TCP relay between the transport and the broker. cut() aborts both
// sides: the broker sees the client vanish without a DISCONNECT (like a
// NUC power cut) and publishes its Last Will. The relay keeps listening,
// so the transport can reconnect through it.
class CutRelay
{
  public:
    CutRelay()
    {
        server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
            while (QTcpSocket *client = server_.nextPendingConnection()) {
                auto *broker = new QTcpSocket(client);
                broker->connectToHost(kBrokerHost, kBrokerPort);
                QObject::connect(client, &QTcpSocket::readyRead, broker, [client, broker] { broker->write(client->readAll()); });
                QObject::connect(broker, &QTcpSocket::readyRead, client, [client, broker] { client->write(broker->readAll()); });
                QObject::connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
                pairs_.push_back({client, broker});
            }
        });
    }

    quint16 port() const
    {
        return server_.serverPort();
    }

    void cut()
    {
        for (auto &pair : pairs_) {
            if (pair.second)
                pair.second->abort();
            if (pair.first)
                pair.first->abort();
        }
        pairs_.clear();
    }

  private:
    QTcpServer                                                         server_;
    std::vector<std::pair<QPointer<QTcpSocket>, QPointer<QTcpSocket>>> pairs_;
};

} // namespace

class PahoMqttTransportTest : public QObject
{
    Q_OBJECT

  private slots:
    void init()
    {
        if (!brokerIsRunning())
            QSKIP("no MQTT broker on 127.0.0.1:1883");
    }

    void publishedMessageComesBackOnSubscribedTopic()
    {
        const QString     topic = uniqueName("f20test/") + "/send";
        PahoMqttTransport transport;
        QSignalSpy        received(&transport, &MqttTransport::messageReceived);
        transport.subscribe(topic, 1); // before connect: done on connect
        transport.connectToBroker(brokerSettings());
        QTRY_VERIFY(transport.isConnected());

        // The subscribe is asynchronous; publish until the first copy is back.
        QTRY_VERIFY_WITH_TIMEOUT((transport.publish(topic, R"({"command":"status"})", 1), received.count() > 0), 5000);
        QCOMPARE(received.first().at(0).toString(), topic);
        QCOMPARE(received.first().at(1).toByteArray(), QByteArray(R"({"command":"status"})"));
    }

    void emptyPayloadIsDelivered()
    {
        const QString     topic = uniqueName("f20test/");
        PahoMqttTransport transport;
        QSignalSpy        received(&transport, &MqttTransport::messageReceived);
        transport.connectToBroker(brokerSettings());
        QTRY_VERIFY(transport.isConnected());
        transport.subscribe(topic, 0);
        QTRY_VERIFY_WITH_TIMEOUT((transport.publish(topic, QByteArray(), 0), received.count() > 0), 5000);
        QVERIFY(received.first().at(1).toByteArray().isEmpty());
    }

    void publishWhileOfflineFails()
    {
        PahoMqttTransport transport;
        QVERIFY(!transport.publish("f20test/x", "data", 1));
    }

    void deadBrokerIsRetried()
    {
        PahoMqttTransport       transport;
        QSignalSpy              log(&transport, &MqttTransport::logLine);
        QSignalSpy              connected(&transport, &MqttTransport::connected);
        MqttTransport::Settings settings = brokerSettings();
        settings.port = deadPort();
        transport.connectToBroker(settings);

        auto failures = [&log] {
            int count = 0;
            for (const auto &entry : log)
                if (entry.at(0).toString().contains("connect failed"))
                    ++count;
            return count;
        };
        QTRY_VERIFY_WITH_TIMEOUT(failures() >= 3, 10000);
        QCOMPARE(connected.count(), 0);
        QVERIFY(!transport.isConnected());
    }

    void lastWillIsPublishedWhenConnectionDrops()
    {
        const QString willTopic = uniqueName("f20test/") + "/send";

        PahoMqttTransport watcher;
        QSignalSpy        received(&watcher, &MqttTransport::messageReceived);
        watcher.subscribe(willTopic, 1);
        watcher.connectToBroker(brokerSettings());
        QTRY_VERIFY(watcher.isConnected());

        CutRelay                relay;
        PahoMqttTransport       machine;
        MqttTransport::Settings settings = brokerSettings();
        settings.port = relay.port();
        settings.willTopic = willTopic;
        settings.willPayload = R"({"machine_status":"Offline"})";
        settings.reconnectFirstDelayMs = 60000; // stay down for this test
        machine.connectToBroker(settings);
        QTRY_VERIFY(machine.isConnected());
        QTest::qWait(200); // the watcher's subscribe is asynchronous
        QCOMPARE(received.count(), 0);

        relay.cut();
        QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 5000);
        QCOMPARE(received.first().at(1).toByteArray(), QByteArray(R"({"machine_status":"Offline"})"));
    }

    void reconnectsAndSubscribesAgainAfterDrop()
    {
        const QString           topic = uniqueName("f20test/") + "/receive";
        CutRelay                relay;
        PahoMqttTransport       transport;
        QSignalSpy              received(&transport, &MqttTransport::messageReceived);
        QSignalSpy              connected(&transport, &MqttTransport::connected);
        QSignalSpy              disconnected(&transport, &MqttTransport::disconnected);
        MqttTransport::Settings settings = brokerSettings();
        settings.port = relay.port();
        transport.subscribe(topic, 1);
        transport.connectToBroker(settings);
        QTRY_COMPARE(connected.count(), 1);

        relay.cut();
        QTRY_COMPARE(disconnected.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 2, 5000);

        // Clean session: the broker forgot the subscription, so a message
        // arrives only if the transport subscribed again by itself.
        PahoMqttTransport sender;
        sender.connectToBroker(brokerSettings());
        QTRY_VERIFY(sender.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT((sender.publish(topic, "measure", 1), received.count() > 0), 5000);
    }

    void deleteWhileConnectingDoesNotCrash()
    {
        auto transport = std::make_unique<PahoMqttTransport>();
        transport->connectToBroker(brokerSettings());
        transport.reset(); // callbacks may still be in flight
        QTest::qWait(300); // let any queued callback arrive (it must be dropped)
    }
};

int runPahoMqttTransportTests(int argc, char **argv)
{
    PahoMqttTransportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_pahomqtttransport.moc"
