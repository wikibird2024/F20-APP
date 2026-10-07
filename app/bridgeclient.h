#pragma once
#include "f20/framing.h"
#include "f20/protocol.h"

#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <map>

// Async client for the bridge socket (spec §5): JSON lines over local TCP,
// replies matched to requests by id, events pushed any time. The UI never
// blocks - every send() takes a callback that runs when the reply arrives.
//
// Robustness rules (each follows a mature project, see the .cpp):
// - a reply handler runs only while its context object lives;
// - a handler never runs inside send() itself;
// - a timeout drops the connection, so a late reply can't be taken for a
//   new one;
// - reconnects back off exponentially with jitter;
// - an idle connection is checked with a heartbeat;
// - an oversized line drops the connection;
// - nothing thrown inside escapes into Qt's event loop.
class BridgeClient : public QObject
{
    Q_OBJECT
  public:
    using ReplyHandler = std::function<void(const f20::Reply &)>;

    // All times in ms. Reconnect delays follow gRPC's connection-backoff.md:
    // first delay, x1.6 per retry, +-20 % jitter, a cap. Tests shrink them.
    struct Timing {
        int requestTimeoutMs = 15000; // default per-request timeout
        int reconnectFirstDelayMs = 2000;
        int reconnectMaxDelayMs = 30000;
        int stableAfterMs = 60000;       // connected this long: retry count back to 0
        int heartbeatIntervalMs = 10000; // only while no other request is open
        int heartbeatTimeoutMs = 5000;
        int heartbeatMissesAllowed = 1; // the 2nd miss in a row drops the connection
        int retriesBeforeAlarm = 3;     // spec §7: 3 retries, then alarm
    };

    explicit BridgeClient(QObject *parent = nullptr);
    ~BridgeClient() override;

    void setTiming(const Timing &timing);
    void connectToBridge(const QString &host, quint16 port);
    void disconnectFromBridge();
    bool isConnected() const;

    // Sends one command. onReply runs at most once, from the event loop
    // (never inside send()), with the bridge's reply or a synthetic error:
    // not connected, timeout, connection lost. It runs only while `context`
    // is alive - when the screen that asked is gone, the reply is dropped
    // (Qt Creator's guardedcallback.h does the same). timeoutMs 0 means
    // Timing::requestTimeoutMs.
    void send(const QString &cmd, const f20::json &params, QObject *context, ReplyHandler onReply, int timeoutMs = 0);

  signals:
    void connected();
    // Emitted BEFORE the open requests get their "connection lost" error,
    // so the app is in Fault when those handlers run.
    void disconnected();
    // Retries used up without a working connection (spec §7: fault + alarm).
    // Retrying goes on in the background.
    void reconnectExhausted();
    void eventReceived(const QString &name, const f20::json &data);
    void protocolLog(const QString &line); // feed for the diagnostics log

  private:
    struct Pending {
        ReplyHandler      handler;
        QPointer<QObject> context;
        QTimer           *timeout = nullptr;
        bool              isHeartbeat = false;
    };

    void sendRequest(const QString &cmd, const f20::json &params, Pending pending, int timeoutMs);
    void deliver(const Pending &entry, const f20::Reply &reply);
    void handleLine(const std::string &line);
    void startConnect();
    void scheduleReconnect();
    void sendHeartbeat();
    void failAllPending(const QString &message);
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onStateChanged(QAbstractSocket::SocketState state);
    void onRequestTimeout(int id);

    QTcpSocket             socket_;
    f20::LineSplitter      splitter_;
    Timing                 timing_;
    QString                host_;
    quint16                port_ = 0;
    bool                   wantConnection_ = false;
    bool                   reachedConnected_ = false; // the current attempt got connected
    int                    nextId_ = 1;
    int                    reconnectAttempts_ = 0; // since the last stable connection
    int                    heartbeatMisses_ = 0;
    QTimer                 reconnectTimer_;
    QTimer                 stableTimer_;
    QTimer                 heartbeatTimer_;
    std::map<int, Pending> pending_;
};
