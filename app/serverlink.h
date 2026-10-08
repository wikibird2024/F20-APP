#pragma once
#include "f20/protocol.h"

#include <QObject>

// Connection to the server as the operator sees it (status bar, Diagnostics).
enum class ServerConnection { off, connecting, connected, lost };

QString toString(ServerConnection state); // "off", "connecting…", "connected", "lost, retrying"

// The one transport interface of spec §6.6: everything the Linux server
// sees goes through here. The production implementation is MqttServerLink;
// with no broker in f20.ini (or no Paho in the build) the NullServerLink
// below logs instead of publishing, and the rest of the app cannot tell
// the difference - that is the point of the interface.
class ServerLink : public QObject
{
    Q_OBJECT
  public:
    using QObject::QObject;

    // Starts connecting. Separate from the constructor, so the caller can
    // connect the signals first and miss no log line or state change.
    virtual void start()
    {
    }

    // F20APP's own messages (spec 6.6.4). status: the latest data, repeated
    // by the link (every 1 s over MQTT). result and alarm wait for the
    // server's ack.
    virtual void publishStatus(const f20::json &status) = 0;
    virtual void publishResult(const f20::json &result) = 0;
    virtual void publishAlarm(const QString &kind, const f20::json &data) = 0;

    // Answers to the server's requests, matched by transaction id. data
    // gets error "" when it has no error key (spec 6.6.3).
    virtual void sendResponse(const QString &transactionId, const QString &command, const f20::json &data) = 0;
    virtual void sendAck(const QString &transactionId, const QString &command, const f20::json &data) = 0;

    virtual ServerConnection connectionState() const = 0;
    virtual QString          connectionDetails() const = 0; // broker address and topics, for Diagnostics

  signals:
    // The server's requests (spec 6.6.4). Each one is answered once with
    // sendResponse() or sendAck() and the same transaction id.
    void remoteMeasureRequested(const QString &transactionId, const QString &recipeName, const QString &sampleId);
    void remoteBaselineInvalidate(const QString &transactionId, const QString &reason);
    void resultsRequested(const QString &transactionId, const QString &sinceUtc, int page);
    void spectrumRequested(const QString &transactionId, const QString &resultId);

    void connectionChanged(ServerConnection state);
    void logLine(const QString &line); // feed for the diagnostics log
};

class NullServerLink : public ServerLink
{
    Q_OBJECT
  public:
    using ServerLink::ServerLink;

    void publishStatus(const f20::json &status) override;
    void publishResult(const f20::json &result) override;
    void publishAlarm(const QString &kind, const f20::json &data) override;
    void sendResponse(const QString &transactionId, const QString &command, const f20::json &data) override;
    void sendAck(const QString &transactionId, const QString &command, const f20::json &data) override;

    ServerConnection connectionState() const override
    {
        return ServerConnection::off;
    }
    QString connectionDetails() const override
    {
        return "no broker in f20.ini";
    }

  private:
    f20::json lastStatus_; // logged only when it changes, not every tick
};
