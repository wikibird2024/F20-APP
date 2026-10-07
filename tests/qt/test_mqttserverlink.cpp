// MqttServerLink against FakeMqttTransport: no broker, no threads. The test
// plays the broker - it accepts or drops the connection and reads what the
// link published.
#include "fakemqtttransport.h"
#include "mqttserverlink.h"

#include <QSignalSpy>
#include <QTest>

namespace
{

struct Fixture {
    FakeMqttTransport *transport = new FakeMqttTransport; // owned by the link
    MqttServerLink     link;

    explicit Fixture(const QString &serial = "F20:SIM001") : link(transport, settingsFor(serial))
    {
    }

    static MqttServerLink::Settings settingsFor(const QString &serial)
    {
        MqttServerLink::Settings settings;
        settings.serial = serial;
        settings.broker.host = "192.0.2.1";
        settings.broker.port = 1883;
        return settings;
    }

    f20::json lastMessage() const
    {
        return f20::json::parse(transport->published().back().payload.toStdString());
    }
};

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

    void statusGoesToSendTopicWithQos0()
    {
        Fixture fixture;
        fixture.link.start();
        fixture.transport->acceptConnection();
        fixture.link.publishStatus({{"state", "ready"}});

        QCOMPARE(fixture.transport->published().size(), size_t(1));
        QCOMPARE(fixture.transport->published().back().topic, QString("SIM001/ar/f20/send"));
        QCOMPARE(fixture.transport->published().back().qos, 0);
        QCOMPARE(fixture.lastMessage()["command"].get<std::string>(), std::string("status"));
        QCOMPARE(fixture.lastMessage()["data"]["state"].get<std::string>(), std::string("ready"));
    }

    void lastStatusIsSentAgainOnEveryConnect()
    {
        Fixture fixture;
        fixture.link.start();
        fixture.link.publishStatus({{"state", "starting"}}); // offline: kept, not sent
        QVERIFY(fixture.transport->published().empty());

        fixture.transport->acceptConnection();
        QCOMPARE(fixture.transport->published().size(), size_t(1));
        QCOMPARE(fixture.lastMessage()["data"]["state"].get<std::string>(), std::string("starting"));

        fixture.transport->dropConnection();
        fixture.transport->acceptConnection();
        QCOMPARE(fixture.transport->published().size(), size_t(2));
    }

    void resultAndAlarmUseQos1()
    {
        Fixture fixture;
        fixture.link.start();
        fixture.transport->acceptConnection();
        fixture.link.publishResult({{"gof", 0.987}});
        QCOMPARE(fixture.transport->published().back().qos, 1);
        QCOMPARE(fixture.lastMessage()["command"].get<std::string>(), std::string("result"));

        fixture.link.publishAlarm("fail", {});
        QCOMPARE(fixture.transport->published().back().qos, 1);
        QCOMPARE(fixture.lastMessage()["command"].get<std::string>(), std::string("alarm"));
        QCOMPARE(fixture.lastMessage()["data"]["kind"].get<std::string>(), std::string("fail"));
    }

    void resultWhileOfflineIsLoggedNotSent()
    {
        Fixture    fixture;
        QSignalSpy log(&fixture.link, &ServerLink::logLine);
        fixture.link.start();
        fixture.link.publishResult({{"gof", 0.5}});
        QVERIFY(fixture.transport->published().empty());
        QVERIFY(!log.isEmpty());
        QVERIFY(log.last().at(0).toString().contains("result NOT sent"));
    }

    void incomingMessageIsOnlyLogged()
    {
        Fixture    fixture;
        QSignalSpy log(&fixture.link, &ServerLink::logLine);
        fixture.link.start();
        fixture.transport->acceptConnection();
        fixture.transport->injectMessage("SIM001/ar/f20/receive", R"({"command":"measure"})");
        QVERIFY(log.last().at(0).toString().contains("not handled yet"));
        QVERIFY(fixture.transport->published().empty());
    }
};

int runMqttServerLinkTests(int argc, char **argv)
{
    MqttServerLinkTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mqttserverlink.moc"
