#pragma once
#include "f20/framing.h"
#include "f20/protocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <map>

// Async client for the bridge socket (spec §5.3): JSON lines over local TCP,
// replies matched to requests by id, events pushed any time. The UI never
// blocks - every send() takes a callback that runs when the reply arrives.
class BridgeClient : public QObject {
    Q_OBJECT
public:
    using ReplyHandler = std::function<void(const f20::Reply&)>;

    explicit BridgeClient(QObject* parent = nullptr);
    ~BridgeClient() override;

    void connectToBridge(const QString& host, quint16 port);
    void disconnectFromBridge();
    bool isConnected() const;

    // Sends one command. onReply always runs exactly once: with the bridge's
    // reply, or with a synthetic timeout error after timeoutMs.
    void send(const QString& cmd, const f20::json& params = {},
              ReplyHandler onReply = {}, int timeoutMs = 15000);

signals:
    void connected();
    void disconnected();
    // 3 reconnect attempts failed in a row (spec §8.1: fault + alarm).
    void reconnectExhausted();
    void eventReceived(const QString& name, const f20::json& data);
    void protocolLog(const QString& line); // feed for the diagnostics log view

private:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void failAllPending(const QString& message);

    QTcpSocket socket_;
    f20::LineSplitter splitter_;
    QString host_;
    quint16 port_ = 0;
    int nextId_ = 1;
    int reconnectAttempts_ = 0;
    QTimer reconnectTimer_;

    struct Pending {
        ReplyHandler handler;
        QTimer* timeout = nullptr;
    };
    std::map<int, Pending> pending_;
};
