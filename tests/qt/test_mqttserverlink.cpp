// MqttServerLink against FakeMqttTransport: no broker, no threads. The test
// plays the broker and the server - it accepts or drops the connection,
// reads what the link published and injects the server's messages.
#include "fakemqtttransport.h"
#include "mqttserverlink.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

namespace
{

struct Fixture {
    FakeMqttTransport *transport = new FakeMqttTransport; // owned by the link
    MqttServerLink     link;

    explicit Fixture(const QString &serial = "F20:SIM001", int statusIntervalMs = 60000, int ackTimeoutMs = 60000)
        : link(transport, settingsFor(serial, statusIntervalMs, ackTimeoutMs))
    {
    }

    static MqttServerLink::Settings settingsFor(const QString &serial, int statusIntervalMs, int ackTimeoutMs)
    {
        MqttServerLink::Settings settings;
        settings.serial = serial;
        settings.broker.host = "192.0.2.1";
        settings.broker.port = 1883;
        settings.statusIntervalMs = statusIntervalMs;
        settings.ackTimeoutMs = ackTimeoutMs;
        settings.ackRetries = 2;
        return settings;
    }

    void startConnected()
    {
        link.start();
        transport->acceptConnection();
        transport->clearPublished();
    }

    f20::json message(std::size_t index) const
    {
        return f20::json::parse(transport->published().at(index).payload.toStdString());
    }
    f20::json lastMessage() const
    {
        return message(transport->published().size() - 1);
    }

    // The server sends one envelope to the receive topic.
    void serverSends(const std::string &command, const std::string &type, const std::string &transactionId,
                     const f20::json &data = f20::json::object(), const std::string &machineSn = "SIM001")
    {
        const f20::json message{{"command", command},     {"command_type", type},    {"data", data},
                                {"machine_name", "AR"},   {"machine_sn", machineSn}, {"transaction_id", transactionId}};
        transport->injectMessage("SIM001/ar/f20/receive", QByteArray::fromStdString(message.dump()));
    }
};

std::string text(const f20::json &message, const char *key)
{
    return message.value(key, std::string());
}

} // namespace

class MqttServerLinkTest : public QObject
{
    Q_OBJECT

  private slots:
    void topicsUseTheBareSerial()
    {
        Fixture fixture("F20:09A006");
        QCOMPARE(fixture.link.sendTopic(), QString("09A006/ar/f20/send"));
        QCOMPARE(fixture.link.receiveTopic(), QString("09A006/ar/f20/receive"));
    }

    void startSubscribesAndSetsClientIdAndLastWill()
    {
        Fixture fixture;
        fixture.link.start();

        QCOMPARE(fixture.transport->subscriptions().size(), size_t(1));
        QCOMPARE(fixture.transport->subscriptions().front().topic, QString("SIM001/ar/f20/receive"));
        QCOMPARE(fixture.transport->settings().clientId, QString("f20-SIM001"));
        QCOMPARE(fixture.transport->settings().willTopic, QString("SIM001/ar/f20/send"));
        const auto will = f20::json::parse(fixture.transport->settings().willPayload.toStdString());
        QCOMPARE(text(will, "command"), std::string("status"));
        QCOMPARE(text(will, "machine_sn"), std::string("SIM001"));
        QCOMPARE(will["data"]["machine_status"].get<std::string>(), std::string("Offline"));
    }

    void stateFollowsTheConnection()
    {
        Fixture    fixture;
        QSignalSpy changed(&fixture.link, &ServerLink::connectionChanged);
        fixture.link.start();
        QCOMPARE(toString(fixture.link.connectionState()), toString(ServerConnection::connecting));

        fixture.transport->acceptConnection();
        QCOMPARE(toString(fixture.link.connectionState()), toString(ServerConnection::connected));
        fixture.transport->dropConnection();
        QCOMPARE(toString(fixture.link.connectionState()), toString(ServerConnection::lost));
        fixture.transport->acceptConnection();
        QCOMPARE(toString(fixture.link.connectionState()), toString(ServerConnection::connected));
        QCOMPARE(changed.count(), 3);
    }

    void statusIsACompanyEnvelopeWithQos0()
    {
        Fixture fixture;
        fixture.startConnected();
        fixture.link.publishStatus({{"machine_status", "Ready"}});

        QCOMPARE(fixture.transport->published().size(), size_t(1));
        QCOMPARE(fixture.transport->published().back().topic, QString("SIM001/ar/f20/send"));
        QCOMPARE(fixture.transport->published().back().qos, 0);
        const auto status = fixture.lastMessage();
        QCOMPARE(text(status, "command"), std::string("status"));
        QCOMPARE(text(status, "command_type"), std::string("request"));
        QCOMPARE(text(status, "machine_name"), std::string("F20"));
        QCOMPARE(text(status, "machine_sn"), std::string("SIM001"));
        QVERIFY(QRegularExpression(R"(^\d{4}-\d{8}-\d{6}$)")
                    .match(QString::fromStdString(text(status, "transaction_id")))
                    .hasMatch());
        QCOMPARE(status["data"]["machine_status"].get<std::string>(), std::string("Ready"));
    }

    void statusRepeatsEveryInterval()
    {
        Fixture fixture("F20:SIM001", 30);
        fixture.startConnected();
        fixture.link.publishStatus({{"machine_status", "Ready"}});
        QTRY_VERIFY(fixture.transport->published().size() >= 4);
        QCOMPARE(text(fixture.lastMessage(), "command"), std::string("status"));
    }

    void lastStatusIsSentAgainOnEveryConnect()
    {
        Fixture fixture;
        fixture.link.start();
        fixture.link.publishStatus({{"machine_status", "Starting"}}); // offline: kept, not sent
        QVERIFY(fixture.transport->published().empty());

        fixture.transport->acceptConnection();
        QCOMPARE(fixture.transport->published().size(), size_t(1));
        QCOMPARE(fixture.lastMessage()["data"]["machine_status"].get<std::string>(), std::string("Starting"));

        fixture.transport->dropConnection();
        fixture.transport->acceptConnection();
        QCOMPARE(fixture.transport->published().size(), size_t(2));
    }

    void resultWaitsForTheServersAck()
    {
        Fixture fixture;
        fixture.startConnected();
        fixture.link.publishResult({{"result_id", "r1"}});
        QCOMPARE(fixture.transport->published().back().qos, 1);
        const auto result = fixture.lastMessage();
        QCOMPARE(text(result, "command"), std::string("result"));
        QCOMPARE(fixture.link.waitingForAck(), 1);

        fixture.serverSends("result", "ack", "0000-wrong-id");
        QCOMPARE(fixture.link.waitingForAck(), 1);
        fixture.serverSends("result", "ack", text(result, "transaction_id"));
        QCOMPARE(fixture.link.waitingForAck(), 0);
    }

    void resultWithoutAckIsSentAgainThenGivenUp()
    {
        Fixture    fixture("F20:SIM001", 60000, 30); // ack timeout 30 ms, 2 retries
        QSignalSpy log(&fixture.link, &ServerLink::logLine);
        fixture.startConnected();
        fixture.link.publishResult({{"result_id", "r1"}});
        QTRY_COMPARE(fixture.link.waitingForAck(), 0);
        QCOMPARE(fixture.transport->published().size(), size_t(3)); // first send + 2 retries
        QVERIFY(fixture.message(0)["transaction_id"] == fixture.message(2)["transaction_id"]);
        QVERIFY(log.last().at(0).toString().contains("get_results"));
    }

    void resultWhileOfflineIsSentOnConnect()
    {
        Fixture fixture;
        fixture.link.start();
        fixture.link.publishResult({{"result_id", "r1"}});
        QVERIFY(fixture.transport->published().empty());
        QCOMPARE(fixture.link.waitingForAck(), 1);

        fixture.transport->acceptConnection();
        QCOMPARE(text(fixture.lastMessage(), "command"), std::string("result"));
    }

    void alarmCarriesKindAndMessage()
    {
        Fixture fixture;
        fixture.startConnected();
        fixture.link.publishAlarm("baseline_stale", {});
        QCOMPARE(text(fixture.lastMessage(), "command"), std::string("alarm"));
        QCOMPARE(fixture.lastMessage()["data"]["kind"].get<std::string>(), std::string("baseline_stale"));
        QVERIFY(fixture.lastMessage()["data"].contains("message"));
        QCOMPARE(fixture.link.waitingForAck(), 1);
    }

    void measureRequestReachesTheAppAndGetsOneReply()
    {
        Fixture    fixture;
        QSignalSpy measure(&fixture.link, &ServerLink::remoteMeasureRequested);
        fixture.startConnected();
        fixture.serverSends("measure", "request", "0007-20261008-101500",
                            {{"recipe_name", "SiO2 on Si"}, {"sample_id", "LOT42-07"}});
        QCOMPARE(measure.count(), 1);
        QCOMPARE(measure.last().at(0).toString(), QString("0007-20261008-101500"));
        QCOMPARE(measure.last().at(1).toString(), QString("SiO2 on Si"));
        QCOMPARE(measure.last().at(2).toString(), QString("LOT42-07"));

        fixture.link.sendResponse("0007-20261008-101500", "measure", {{"result_id", "r9"}});
        const auto response = fixture.lastMessage();
        QCOMPARE(text(response, "command_type"), std::string("response"));
        QCOMPARE(text(response, "transaction_id"), std::string("0007-20261008-101500"));
        QCOMPARE(response["data"]["error"].get<std::string>(), std::string("")); // added
    }

    void repeatedRequestGetsTheSameReplyAndRunsOnce()
    {
        Fixture    fixture;
        QSignalSpy measure(&fixture.link, &ServerLink::remoteMeasureRequested);
        fixture.startConnected();
        fixture.serverSends("measure", "request", "t1");
        fixture.serverSends("measure", "request", "t1"); // still running: ignored
        QCOMPARE(measure.count(), 1);
        QVERIFY(fixture.transport->published().empty());

        fixture.link.sendResponse("t1", "measure", {{"result_id", "r1"}});
        fixture.serverSends("measure", "request", "t1"); // QoS 1 duplicate after the reply
        QCOMPARE(measure.count(), 1);
        QCOMPARE(fixture.transport->published().size(), size_t(2));
        QVERIFY(fixture.message(0) == fixture.message(1));
    }

    void badRequestsGetAnErrorNotACrash()
    {
        Fixture    fixture;
        QSignalSpy measure(&fixture.link, &ServerLink::remoteMeasureRequested);
        fixture.startConnected();

        fixture.serverSends("reboot", "request", "u1");
        QCOMPARE(fixture.lastMessage()["data"]["error"].get<std::string>(), std::string("badRequest"));

        fixture.serverSends("measure", "request", "u2", {{"recipe_name", 5}});
        QCOMPARE(text(fixture.lastMessage(), "transaction_id"), std::string("u2"));
        QCOMPARE(fixture.lastMessage()["data"]["error"].get<std::string>(), std::string("badRequest"));

        fixture.serverSends("get_spectrum", "request", "u3"); // result_id missing
        QCOMPARE(fixture.lastMessage()["data"]["error"].get<std::string>(), std::string("badRequest"));

        // Not an envelope, but the transaction id is readable: still answered.
        fixture.transport->injectMessage("SIM001/ar/f20/receive", R"({"transaction_id":"u4","command":"measure"})");
        QCOMPARE(text(fixture.lastMessage(), "transaction_id"), std::string("u4"));

        const std::size_t before = fixture.transport->published().size();
        fixture.transport->injectMessage("SIM001/ar/f20/receive", "not json");
        fixture.transport->injectMessage("SIM001/ar/f20/receive", QByteArray(1024 * 1024 + 1, 'x'));
        QCOMPARE(fixture.transport->published().size(), before);
        QCOMPARE(measure.count(), 0);
    }

    void messageForAnotherMachineIsIgnored()
    {
        Fixture    fixture;
        QSignalSpy measure(&fixture.link, &ServerLink::remoteMeasureRequested);
        fixture.startConnected();
        fixture.serverSends("measure", "request", "x1", f20::json::object(), "OTHER9");
        // Broken (data is not an object) and for another machine: no reply either.
        fixture.serverSends("measure", "request", "x2", f20::json::array(), "OTHER9");
        QCOMPARE(measure.count(), 0);
        QVERIFY(fixture.transport->published().empty());
    }

    void otherServerRequestsReachTheApp()
    {
        Fixture    fixture;
        QSignalSpy invalidate(&fixture.link, &ServerLink::remoteBaselineInvalidate);
        QSignalSpy results(&fixture.link, &ServerLink::resultsRequested);
        QSignalSpy spectrum(&fixture.link, &ServerLink::spectrumRequested);
        fixture.startConnected();

        fixture.serverSends("baseline_invalidate", "request", "b1", {{"reason", "fiber moved"}});
        QCOMPARE(invalidate.last().at(1).toString(), QString("fiber moved"));
        fixture.link.sendAck("b1", "baseline_invalidate", {});
        QCOMPARE(text(fixture.lastMessage(), "command_type"), std::string("ack"));

        fixture.serverSends("get_results", "request", "g1", {{"since", "2026-10-07T00:00:00.000Z"}});
        QCOMPARE(results.last().at(1).toString(), QString("2026-10-07T00:00:00.000Z"));
        QCOMPARE(results.last().at(2).toInt(), 1); // page defaults to 1

        fixture.serverSends("get_spectrum", "request", "s1", {{"result_id", "r1"}});
        QCOMPARE(spectrum.last().at(1).toString(), QString("r1"));
    }
};

int runMqttServerLinkTests(int argc, char **argv)
{
    MqttServerLinkTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mqttserverlink.moc"
