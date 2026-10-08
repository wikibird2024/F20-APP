#include "bridgeclient.h"

#include "f20/logtext.h"

#include <QRandomGenerator>
#include <QtDebug>

#include <algorithm>
#include <climits>

namespace
{
// Log a protocol line without full spectra (spec 2.4 #6).
QString logText(const std::string &line)
{
    return QString::fromStdString(f20::shortenForLog(line));
}
} // namespace

BridgeClient::BridgeClient(QObject *parent) : QObject(parent)
{
    connect(&socket_, &QTcpSocket::readyRead, this, &BridgeClient::onReadyRead);
    connect(&socket_, &QTcpSocket::connected, this, &BridgeClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &BridgeClient::onDisconnected);
    connect(&socket_, &QAbstractSocket::stateChanged, this, &BridgeClient::onStateChanged);

    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (wantConnection_ && socket_.state() == QAbstractSocket::UnconnectedState)
            startConnect();
    });
    stableTimer_.setSingleShot(true);
    connect(&stableTimer_, &QTimer::timeout, this, [this] { reconnectAttempts_ = 0; });
    connect(&heartbeatTimer_, &QTimer::timeout, this, &BridgeClient::sendHeartbeat);
}

BridgeClient::~BridgeClient()
{
    // Members die in reverse declaration order: pending_ and the timers go
    // before socket_. The socket's destructor emits disconnected, which
    // would run onDisconnected() against those dead members - so unhook
    // the socket from this object first.
    socket_.disconnect(this);
    reconnectTimer_.stop();
    socket_.abort();
}

void BridgeClient::setTiming(const Timing &timing)
{
    timing_ = timing;
}

void BridgeClient::connectToBridge(const QString &host, quint16 port)
{
    host_ = host;
    port_ = port;
    wantConnection_ = false; // the abort below must not schedule a retry
    reconnectTimer_.stop();
    socket_.abort(); // fresh start, also from a half-closed socket
    wantConnection_ = true;
    reconnectAttempts_ = 0;
    startConnect();
}

void BridgeClient::disconnectFromBridge()
{
    wantConnection_ = false;
    reconnectTimer_.stop();
    socket_.abort(); // open requests get "connection lost" in onDisconnected
}

bool BridgeClient::isConnected() const
{
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void BridgeClient::startConnect()
{
    reachedConnected_ = false;
    splitter_.reset(); // a half line from the old connection must not prefix the new one
    socket_.connectToHost(host_, port_);
}

// Exponential backoff with jitter (gRPC doc/connection-backoff.md): a dead
// bridge is not hammered, and several clients don't retry in lockstep.
void BridgeClient::scheduleReconnect()
{
    ++reconnectAttempts_;
    if (reconnectAttempts_ == timing_.retriesBeforeAlarm + 1)
        emit reconnectExhausted();

    double delayMs = timing_.reconnectFirstDelayMs;
    for (int i = 1; i < reconnectAttempts_ && delayMs < timing_.reconnectMaxDelayMs; ++i)
        delayMs *= 1.6;
    delayMs = std::min(delayMs, static_cast<double>(timing_.reconnectMaxDelayMs));
    delayMs *= 0.8 + 0.4 * QRandomGenerator::global()->generateDouble();
    reconnectTimer_.start(static_cast<int>(delayMs));
}

void BridgeClient::onConnected()
{
    reachedConnected_ = true;
    heartbeatMisses_ = 0;
    // The retry count goes back to 0 only after a stable connection, so a
    // bridge that crashes right after each start still reaches the alarm
    // (Qt Creator resets its restart budget on a timer the same way).
    stableTimer_.start(timing_.stableAfterMs);
    heartbeatTimer_.start(timing_.heartbeatIntervalMs);
    emit protocolLog("[connected to bridge]");
    emit connected();
}

void BridgeClient::onDisconnected()
{
    stableTimer_.stop();
    heartbeatTimer_.stop();
    emit protocolLog("[bridge connection lost]");
    emit disconnected();
    failAllPending("bridge connection lost");
    if (wantConnection_)
        scheduleReconnect();
}

// A connect attempt that fails never reaches Connected, so `disconnected`
// is not emitted for it; the drop back to Unconnected is the signal.
void BridgeClient::onStateChanged(QAbstractSocket::SocketState state)
{
    if (state != QAbstractSocket::UnconnectedState || reachedConnected_ || !wantConnection_)
        return;
    emit protocolLog("[cannot reach bridge] " + socket_.errorString());
    scheduleReconnect();
}

// The written request first, then the queue in send order. Both are moved
// out before any handler runs: the socket is already down, so a send() from
// a handler gets its own "not connected" error instead of joining the queue.
void BridgeClient::failAllPending(const QString &message)
{
    auto pending = std::move(pending_);
    pending_.clear();
    auto queued = std::move(queue_);
    queue_.clear();
    for (auto &[id, entry] : pending) {
        entry.timeout->deleteLater();
        if (!entry.isHeartbeat)
            deliver(entry, f20::errorReply(id, "filmeasureError", message.toStdString()));
    }
    for (const Queued &request : queued)
        deliver(request.pending, f20::errorReply(request.id, "filmeasureError", message.toStdString()));
}

// Runs a handler only while its context lives, and never lets an
// exception out: "throwing an exception from a slot invoked by Qt's
// signal-slot connection mechanism is considered undefined behaviour"
// (doc.qt.io/qt-6/exceptionsafety.html).
void BridgeClient::deliver(const Pending &entry, const f20::Reply &reply)
{
    if (!entry.context || !entry.handler)
        return;
    try {
        entry.handler(reply);
    } catch (const std::exception &e) {
        qWarning() << "reply handler for id" << reply.id << "threw:" << e.what();
    }
}

void BridgeClient::send(const QString &cmd, const f20::json &params, QObject *context, ReplyHandler onReply, int timeoutMs)
{
    Pending pending;
    pending.handler = std::move(onReply);
    pending.context = context;
    Queued request = makeRequest(cmd, params, std::move(pending), timeoutMs > 0 ? timeoutMs : timing_.requestTimeoutMs);

    if (!isConnected()) {
        // Posted, never called inside send(): the caller may be halfway
        // through its own state change (Qt Creator posts this error the
        // same way, languageclient/client.cpp).
        QMetaObject::invokeMethod(
            this,
            [this, id = request.id, pending = request.pending] {
                deliver(pending, f20::errorReply(id, "filmeasureError", "bridge not connected"));
            },
            Qt::QueuedConnection);
        return;
    }
    queue_.push_back(std::move(request));
    sendNext();
}

int BridgeClient::takeId()
{
    const int id = nextId_;
    nextId_ = nextId_ == INT_MAX ? 1 : nextId_ + 1;
    return id;
}

BridgeClient::Queued BridgeClient::makeRequest(const QString &cmd, const f20::json &params, Pending pending, int timeoutMs)
{
    f20::Request request;
    request.id = takeId();
    request.cmd = cmd.toStdString();
    if (params.is_object())
        request.params = params;
    return Queued{request.id, f20::serialize(request), std::move(pending), timeoutMs};
}

// One request on the wire at a time (spec 2.1 #6): FILMeasure does one
// thing at a time, and the real bridge answers a second command with busy.
void BridgeClient::sendNext()
{
    if (!isConnected() || !pending_.empty() || queue_.empty())
        return;
    Queued request = std::move(queue_.front());
    queue_.pop_front();
    writeRequest(std::move(request));
}

void BridgeClient::writeRequest(Queued request)
{
    const int id = request.id;
    Pending   pending = std::move(request.pending);
    // The timeout counts from now, not from send(): time spent waiting in
    // the queue is not the bridge's fault.
    pending.timeout = new QTimer(this);
    pending.timeout->setSingleShot(true);
    connect(pending.timeout, &QTimer::timeout, this, [this, id] { onRequestTimeout(id); });
    pending.timeout->start(request.timeoutMs);
    const bool isHeartbeat = pending.isHeartbeat;
    pending_[id] = std::move(pending);

    socket_.write(request.line.data(), static_cast<qint64>(request.line.size()));
    socket_.write("\n", 1);
    if (!isHeartbeat) // every 10 s - would bury the real traffic in the log
        emit protocolLog("-> " + logText(request.line));
}

void BridgeClient::onRequestTimeout(int id)
{
    const auto it = pending_.find(id);
    if (it == pending_.end())
        return;
    const Pending entry = std::move(it->second);
    pending_.erase(it);
    entry.timeout->deleteLater();

    if (entry.isHeartbeat) {
        if (++heartbeatMisses_ > timing_.heartbeatMissesAllowed) {
            emit protocolLog("[heartbeat] bridge not answering - dropping the connection");
            socket_.abort();
            return;
        }
        sendNext(); // a request that arrived meanwhile waited for the heartbeat
        return;
    }
    // The bridge is hung or lost the request. Keeping the connection would
    // let a late reply arrive after the app moved on, so drop it: the app
    // goes to Fault first (disconnected), then this handler gets its error.
    emit protocolLog(QString("[timeout] id %1 - dropping the connection").arg(id));
    socket_.abort();
    deliver(entry, f20::errorReply(id, "filmeasureError", "no reply from bridge (timeout)"));
}

// Only while nothing is open or waiting: an open request has its own
// timeout, and the heartbeat must never jump the queue.
void BridgeClient::sendHeartbeat()
{
    if (!isConnected() || !pending_.empty() || !queue_.empty())
        return;
    Pending pending;
    pending.isHeartbeat = true;
    pending.context = this;
    writeRequest(makeRequest("getStatus", f20::json::object(), std::move(pending), timing_.heartbeatTimeoutMs));
}

void BridgeClient::onReadyRead()
{
    const QByteArray data = socket_.readAll();
    const auto       lines = splitter_.feed(data.constData(), static_cast<std::size_t>(data.size()));
    for (const std::string &line : lines) {
        try {
            handleLine(line);
        } catch (const std::exception &e) {
            emit protocolLog(QString("[error] unreadable line dropped: %1").arg(e.what()));
        }
        if (!isConnected())
            return; // a handler dropped the connection: the rest is stale
    }
    if (splitter_.overflowed()) {
        emit protocolLog(
            QString("[error] line over %1 bytes - dropping the connection").arg(f20::LineSplitter::kDefaultMaxLineBytes));
        socket_.abort();
    }
}

void BridgeClient::handleLine(const std::string &line)
{
    const f20::Incoming incoming = f20::parseIncoming(line);

    if (const auto *reply = std::get_if<f20::Reply>(&incoming)) {
        heartbeatMisses_ = 0; // any reply proves the bridge is alive
        const auto it = pending_.find(reply->id);
        if (it == pending_.end()) {
            emit protocolLog("<- [no open request] " + logText(line));
            return;
        }
        const Pending entry = std::move(it->second);
        pending_.erase(it);
        entry.timeout->deleteLater();
        if (!entry.isHeartbeat)
            emit protocolLog("<- " + logText(line));
        if (!reply->ok && reply->errorCode == "busy")
            emit protocolLog(QString("[busy] id %1 refused - is a second client connected?").arg(reply->id));
        // Next one out before this handler runs: a request the handler sends
        // then waits behind the ones already queued.
        sendNext();
        deliver(entry, *reply);
    } else if (const auto *event = std::get_if<f20::Event>(&incoming)) {
        emit protocolLog("<- " + logText(line));
        emit eventReceived(QString::fromStdString(event->event), event->data);
    } else {
        emit protocolLog("<- [unreadable] " + logText(line));
    }
}
