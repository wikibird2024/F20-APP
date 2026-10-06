#include "bridgeclient.h"

BridgeClient::BridgeClient(QObject* parent) : QObject(parent) {
    connect(&socket_, &QTcpSocket::readyRead, this, &BridgeClient::onReadyRead);
    connect(&socket_, &QTcpSocket::connected, this, &BridgeClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected, this, &BridgeClient::onDisconnected);

    reconnectTimer_.setInterval(2000);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        ++reconnectAttempts_;
        if (reconnectAttempts_ == 4) // 3 retries used up (spec §8.1)
            emit reconnectExhausted();
        socket_.connectToHost(host_, port_);
    });
}

BridgeClient::~BridgeClient() {
    // Members die in reverse declaration order: pending_ and
    // reconnectTimer_ go before socket_. The socket's destructor emits
    // disconnected, which would run onDisconnected() against those dead
    // members - so unhook the socket from this object first.
    socket_.disconnect(this);
    reconnectTimer_.stop();
    socket_.abort();
}

void BridgeClient::connectToBridge(const QString& host, quint16 port) {
    host_ = host;
    port_ = port;
    reconnectAttempts_ = 0;
    socket_.connectToHost(host_, port_);
    reconnectTimer_.start();
}

void BridgeClient::disconnectFromBridge() {
    reconnectTimer_.stop();
    socket_.disconnectFromHost();
}

bool BridgeClient::isConnected() const {
    return socket_.state() == QAbstractSocket::ConnectedState;
}

void BridgeClient::onConnected() {
    reconnectAttempts_ = 0;
    reconnectTimer_.stop();
    emit protocolLog("[connected to bridge]");
    emit connected();
}

void BridgeClient::onDisconnected() {
    emit protocolLog("[bridge connection lost]");
    failAllPending("bridge connection lost");
    emit disconnected();
    if (!host_.isEmpty())
        reconnectTimer_.start();
}

void BridgeClient::failAllPending(const QString& message) {
    auto pending = std::move(pending_);
    pending_.clear();
    for (auto& [id, entry] : pending) {
        if (entry.timeout)
            entry.timeout->deleteLater();
        if (entry.handler)
            entry.handler(f20::errorReply(id, "filmeasureError",
                                          message.toStdString()));
    }
}

void BridgeClient::send(const QString& cmd, const f20::json& params,
                        ReplyHandler onReply, int timeoutMs) {
    const int id = nextId_++;

    if (!isConnected()) {
        if (onReply)
            onReply(f20::errorReply(id, "filmeasureError", "bridge not connected"));
        return;
    }

    f20::Request request;
    request.id = id;
    request.cmd = cmd.toStdString();
    if (params.is_object())
        request.params = params;

    auto* timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->start(timeoutMs);
    connect(timeout, &QTimer::timeout, this, [this, id] {
        const auto it = pending_.find(id);
        if (it == pending_.end())
            return;
        auto entry = std::move(it->second);
        pending_.erase(it);
        entry.timeout->deleteLater();
        emit protocolLog(QString("[timeout] id %1").arg(id));
        if (entry.handler)
            entry.handler(f20::errorReply(id, "filmeasureError",
                                          "no reply from bridge (timeout)"));
    });

    pending_[id] = Pending{std::move(onReply), timeout};

    const std::string line = f20::serialize(request) + "\n";
    socket_.write(line.data(), static_cast<qint64>(line.size()));
    emit protocolLog("-> " + QString::fromStdString(f20::serialize(request)));
}

void BridgeClient::onReadyRead() {
    const QByteArray data = socket_.readAll();
    for (const std::string& line : splitter_.feed(data.constData(),
                                                  static_cast<std::size_t>(data.size()))) {
        emit protocolLog("<- " + QString::fromStdString(line));
        const f20::Incoming incoming = f20::parseIncoming(line);

        if (std::holds_alternative<f20::Reply>(incoming)) {
            const auto& reply = std::get<f20::Reply>(incoming);
            const auto it = pending_.find(reply.id);
            if (it == pending_.end())
                continue; // late reply after timeout: already answered
            auto entry = std::move(it->second);
            pending_.erase(it);
            entry.timeout->deleteLater();
            if (entry.handler)
                entry.handler(reply);
        } else if (std::holds_alternative<f20::Event>(incoming)) {
            const auto& event = std::get<f20::Event>(incoming);
            emit eventReceived(QString::fromStdString(event.event), event.data);
        }
    }
}
