#pragma once
#include "mqtttransport.h"
#include "serverlink.h"

#include <QSet>
#include <QTimer>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>

// ServerLink over MQTT in the company format (spec 8.2): every message is
// the envelope of f20/envelope.h on {serial}/ar/f20/send (ours) or
// .../receive (the server's).
//
// Reliability rules (MQTT plan, "Rules"):
// - status: QoS 0, the latest data repeated every statusIntervalMs and
//   right after every (re)connect; never buffered while offline;
// - result and alarm: QoS 1, wait for the server's ack with the same
//   transaction id; no ack -> sent again (ackRetries), also after a
//   reconnect. The server can always fetch a gap with get_results;
// - QoS 1 may deliver a request twice: the reply to each of the last 32
//   requests is kept and sent again, the request is not run twice;
// - bad input (not JSON, unknown command, wrong field type, > 1 MiB) is
//   logged, never fatal; it gets an error reply when it has a transaction id.
//
// The transport is injected: PahoMqttTransport in the app,
// FakeMqttTransport in the tests. This object takes ownership of it.
class MqttServerLink : public ServerLink
{
    Q_OBJECT
  public:
    struct Settings {
        QString                 serial; // as in f20.ini, e.g. "F20:09A006"
        MqttTransport::Settings broker; // host, port, login; client id and Last Will are filled in here
        int                     statusIntervalMs = 1000;
        int                     ackTimeoutMs = 5000;
        int                     ackRetries = 3; // sends after the first one
    };

    MqttServerLink(MqttTransport *transport, const Settings &settings, QObject *parent = nullptr);

    void start() override;

    void publishStatus(const f20::json &status) override;
    void publishResult(const f20::json &result) override;
    void publishAlarm(const QString &kind, const f20::json &data) override;
    void sendResponse(const QString &transactionId, const QString &command, const f20::json &data) override;
    void sendAck(const QString &transactionId, const QString &command, const f20::json &data) override;

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
    int waitingForAck() const
    {
        return static_cast<int>(awaitingAck_.size());
    }

  private:
    // One of our requests that waits for the server's ack.
    struct AwaitingAck {
        QString    command;
        QByteArray payload;
        int        sends = 0;
        QTimer    *timer = nullptr;
        quint64    order = 0; // to find the oldest
    };

    QString    newTransactionId();
    QByteArray envelope(const QString &command, const char *commandType, const QString &transactionId,
                        const f20::json &data) const;
    void       sendAwaitingAck(const QString &command, const f20::json &data);
    void       resend(const QString &transactionId);
    void       onAckTimeout(const QString &transactionId);
    void       reply(const QString &transactionId, const QString &command, const char *commandType, f20::json data);
    void       rejectRequest(const QString &transactionId, const QString &command, const QString &why);
    void       setState(ServerConnection state);
    void       onConnected();
    void       onMessage(const QString &topic, const QByteArray &payload);
    void       onRequest(const QString &transactionId, const QString &command, const f20::json &data);

    MqttTransport           *transport_;
    Settings                 settings_;
    std::string              machineSn_;
    QString                  sendTopic_;
    QString                  receiveTopic_;
    ServerConnection         state_ = ServerConnection::connecting;
    std::optional<f20::json> lastStatus_;
    std::string              lastLoggedMachineStatus_;
    QTimer                   statusTimer_;
    int                      transactionCounter_ = 0;
    quint64                  nextOrder_ = 0;
    std::map<QString, AwaitingAck>              awaitingAck_;
    QSet<QString>                               inProgress_; // requests received, not answered yet
    std::deque<std::pair<QString, QByteArray>> answered_;   // last replies, for duplicates
};
