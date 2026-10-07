#pragma once
#include "mqtttransport.h"
#include "serverlink.h"

#include <optional>

// ServerLink over MQTT - first version ("walking skeleton", plan step 4a).
//
// Today: connects, shows its connection state, publishes status / result /
// alarm to {serial}/ar/f20/send, and logs what arrives on .../receive.
// Not yet: the company envelope and transaction_id (phase 2), the 1 s
// status timer, command dispatch and acks (phase 4).
//
// The transport is injected: PahoMqttTransport in the app,
// FakeMqttTransport in the tests. This object takes ownership of it.
class MqttServerLink : public ServerLink
{
    Q_OBJECT
  public:
    struct Settings {
        QString                 serial; // as the bridge reports it, e.g. "F20:09A006"
        MqttTransport::Settings broker; // host, port, login; client id and Last Will are filled in here
    };

    MqttServerLink(MqttTransport *transport, const Settings &settings, QObject *parent = nullptr);

    void start() override;

    void publishStatus(const f20::json &status) override;
    void publishResult(const f20::json &result) override;
    void publishAlarm(const QString &kind, const f20::json &data) override;

    ServerConnection connectionState() const override
    {
        return state_;
    }
    QString connectionDetails() const override;

    QString sendTopic() const
    {
        return sendTopic_;
    }
    QString receiveTopic() const
    {
        return receiveTopic_;
    }

  private:
    void publish(const char *command, const f20::json &data, int qos);
    void setState(ServerConnection state);
    void onConnected();
    void onMessage(const QString &topic, const QByteArray &payload);

    MqttTransport           *transport_;
    Settings                 settings_;
    QString                  sendTopic_;
    QString                  receiveTopic_;
    ServerConnection         state_ = ServerConnection::connecting;
    std::optional<f20::json> lastStatus_; // sent again after every (re)connect
};
