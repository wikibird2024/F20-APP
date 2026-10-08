#include "mqttserverlink.h"

#include "f20/envelope.h"

#include <chrono>

namespace
{

constexpr int         kMaxPayloadBytes = 1024 * 1024; // same limit as a bridge line
constexpr std::size_t kRepliesKept = 32;
constexpr std::size_t kMaxAwaitingAck = 100; // older ones: the server backfills with get_results

QString fromStd(const std::string &text)
{
    return QString::fromStdString(text);
}

// An optional string field of a request: missing gives "", another type
// makes the request invalid.
bool optionalString(const f20::json &data, const char *key, QString &out)
{
    const auto it = data.find(key);
    if (it == data.end() || it->is_null())
        return true;
    if (!it->is_string())
        return false;
    out = fromStd(it->get<std::string>());
    return true;
}

} // namespace

MqttServerLink::MqttServerLink(MqttTransport *transport, const Settings &settings, QObject *parent)
    : ServerLink(parent), transport_(transport), settings_(settings), machineSn_(f20::bareSerial(settings.serial.toStdString())),
      sendTopic_(fromStd(f20::sendTopic(settings.serial.toStdString()))),
      receiveTopic_(fromStd(f20::receiveTopic(settings.serial.toStdString())))
{
    transport_->setParent(this);

    connect(transport_, &MqttTransport::connected, this, &MqttServerLink::onConnected);
    connect(transport_, &MqttTransport::disconnected, this, [this] { setState(ServerConnection::lost); });
    connect(transport_, &MqttTransport::messageReceived, this, &MqttServerLink::onMessage);
    connect(transport_, &MqttTransport::logLine, this, &ServerLink::logLine);

    // Status every second (spec 8.2.5.1); if it stops, the server treats
    // the F20 as offline.
    connect(&statusTimer_, &QTimer::timeout, this, [this] {
        if (lastStatus_ && transport_->isConnected())
            transport_->publish(sendTopic_, envelope("status", "request", newTransactionId(), *lastStatus_), 0);
    });
}

void MqttServerLink::start()
{
    MqttTransport::Settings broker = settings_.broker;
    broker.clientId = "f20-" + fromStd(machineSn_);
    // Last Will: the broker tells the server we are gone, even after a
    // power cut, when we can send nothing ourselves (Sparkplug's "death
    // certificate").
    broker.willTopic = sendTopic_;
    broker.willPayload = envelope("status", "request", newTransactionId(), {{"machine_status", "Offline"}});
    broker.willQos = 1;

    transport_->subscribe(receiveTopic_, 1);
    transport_->connectToBroker(broker);
    statusTimer_.start(settings_.statusIntervalMs);
}

QString MqttServerLink::newTransactionId()
{
    transactionCounter_ = transactionCounter_ % 9999 + 1;
    return fromStd(f20::makeTransactionId(transactionCounter_, std::chrono::system_clock::now()));
}

QByteArray MqttServerLink::envelope(const QString   &command,
                                    const char      *commandType,
                                    const QString   &transactionId,
                                    const f20::json &data) const
{
    f20::Envelope message;
    message.command = command.toStdString();
    message.commandType = commandType;
    message.data = data.is_object() ? data : f20::json::object();
    message.machineName = "F20";
    message.machineSn = machineSn_;
    message.transactionId = transactionId.toStdString();
    return QByteArray::fromStdString(f20::serialize(message));
}

// Sent at once on a change, then repeated by the timer. Logged only when
// machine_status changes - every second would bury the real traffic.
void MqttServerLink::publishStatus(const f20::json &status)
{
    lastStatus_ = status;
    const std::string machineStatus = f20::stringAt(status, "machine_status").value_or("");
    if (machineStatus != lastLoggedMachineStatus_) {
        lastLoggedMachineStatus_ = machineStatus;
        emit logLine(QString("[mqtt] status %1").arg(fromStd(machineStatus)));
    }
    if (transport_->isConnected())
        transport_->publish(sendTopic_, envelope("status", "request", newTransactionId(), status), 0);
}

void MqttServerLink::publishResult(const f20::json &result)
{
    sendAwaitingAck("result", result);
}

void MqttServerLink::publishAlarm(const QString &kind, const f20::json &data)
{
    f20::json alarm = data.is_object() ? data : f20::json::object();
    alarm["kind"] = kind.toStdString();
    if (!alarm.contains("message"))
        alarm["message"] = "";
    sendAwaitingAck("alarm", alarm);
}

void MqttServerLink::sendAwaitingAck(const QString &command, const f20::json &data)
{
    if (awaitingAck_.size() >= kMaxAwaitingAck) {
        const auto oldest = std::min_element(awaitingAck_.begin(), awaitingAck_.end(), [](const auto &a, const auto &b) {
            return a.second.order < b.second.order;
        });
        emit       logLine(
            QString("[mqtt] %1 %2 dropped unacknowledged - too many waiting").arg(oldest->second.command, oldest->first));
        oldest->second.timer->deleteLater();
        awaitingAck_.erase(oldest);
    }
    const QString transactionId = newTransactionId();
    AwaitingAck   entry;
    entry.command = command;
    entry.payload = envelope(command, "request", transactionId, data);
    entry.order = nextOrder_++;
    entry.timer = new QTimer(this);
    entry.timer->setSingleShot(true);
    connect(entry.timer, &QTimer::timeout, this, [this, transactionId] { onAckTimeout(transactionId); });
    awaitingAck_[transactionId] = std::move(entry);
    emit logLine(QString("[mqtt] -> %1 %2").arg(command, transactionId));
    resend(transactionId);
}

// Sends (again) and starts the ack timer. Offline it only waits: the next
// connect sends everything that still waits.
void MqttServerLink::resend(const QString &transactionId)
{
    const auto it = awaitingAck_.find(transactionId);
    if (it == awaitingAck_.end() || !transport_->isConnected())
        return;
    AwaitingAck &entry = it->second;
    if (transport_->publish(sendTopic_, entry.payload, 1))
        ++entry.sends;
    entry.timer->start(settings_.ackTimeoutMs);
}

void MqttServerLink::onAckTimeout(const QString &transactionId)
{
    const auto it = awaitingAck_.find(transactionId);
    if (it == awaitingAck_.end() || !transport_->isConnected())
        return;
    if (it->second.sends > settings_.ackRetries) {
        emit logLine(QString("[mqtt] no ack for %1 %2 after %3 sends - the server can fetch it with get_results")
                         .arg(it->second.command, transactionId)
                         .arg(it->second.sends));
        it->second.timer->deleteLater();
        awaitingAck_.erase(it);
        return;
    }
    emit logLine(QString("[mqtt] no ack for %1 %2 yet - sending again").arg(it->second.command, transactionId));
    resend(transactionId);
}

void MqttServerLink::sendResponse(const QString &transactionId, const QString &command, const f20::json &data)
{
    reply(transactionId, command, "response", data);
}

void MqttServerLink::sendAck(const QString &transactionId, const QString &command, const f20::json &data)
{
    reply(transactionId, command, "ack", data);
}

// Kept for duplicates even when it cannot be sent now: the server repeats
// the request after a reconnect and then gets this reply.
void MqttServerLink::reply(const QString &transactionId, const QString &command, const char *commandType, f20::json data)
{
    if (!data.is_object())
        data = f20::json::object();
    if (!data.contains("error"))
        data["error"] = "";
    const QByteArray payload = envelope(command, commandType, transactionId, data);
    inProgress_.remove(transactionId);
    answered_.emplace_back(transactionId, payload);
    if (answered_.size() > kRepliesKept)
        answered_.pop_front();

    const std::string error = f20::stringAt(data, "error").value_or("");
    emit              logLine(QString("[mqtt] -> %1 %2 %3%4")
                     .arg(fromStd(commandType), command, transactionId, error.empty() ? QString() : " error " + fromStd(error)));
    if (!transport_->publish(sendTopic_, payload, 1))
        emit logLine(QString("[mqtt] %1 %2 NOT sent - no broker connection").arg(command, transactionId));
}

void MqttServerLink::rejectRequest(const QString &transactionId, const QString &command, const QString &why)
{
    reply(transactionId, command, "response", {{"error", "badRequest"}, {"message", why.toStdString()}});
}

QString MqttServerLink::connectionDetails() const
{
    return QString("broker: %1:%2\nsend: %3\nreceive: %4")
        .arg(settings_.broker.host)
        .arg(settings_.broker.port)
        .arg(sendTopic_, receiveTopic_);
}

void MqttServerLink::setState(ServerConnection state)
{
    if (state == state_)
        return;
    state_ = state;
    emit connectionChanged(state_);
}

void MqttServerLink::onConnected()
{
    setState(ServerConnection::connected);
    // The server sees our full state at once, not only after the next
    // change (Sparkplug: birth message on every connect).
    if (lastStatus_)
        transport_->publish(sendTopic_, envelope("status", "request", newTransactionId(), *lastStatus_), 0);
    std::vector<QString> waiting;
    for (const auto &[transactionId, entry] : awaitingAck_)
        waiting.push_back(transactionId);
    for (const QString &transactionId : waiting)
        resend(transactionId);
}

void MqttServerLink::onMessage(const QString &topic, const QByteArray &payload)
{
    if (payload.size() > kMaxPayloadBytes) {
        emit logLine(QString("[mqtt] message over 1 MiB on %1 dropped").arg(topic));
        return;
    }
    const auto message = f20::parseEnvelope(payload.toStdString());
    if (!message) {
        // Not an envelope - but if the server's transaction id is readable,
        // it learns why instead of waiting.
        const f20::json raw = f20::json::parse(payload.toStdString(), nullptr, false);
        const auto      transactionId = f20::stringAt(raw, "transaction_id");
        const auto      machineSn = f20::stringAt(raw, "machine_sn");
        emit            logLine(QString("[mqtt] unreadable message on %1 (%2 bytes)").arg(topic).arg(payload.size()));
        if (transactionId && (!machineSn || machineSn->empty() || *machineSn == machineSn_))
            rejectRequest(fromStd(*transactionId),
                          fromStd(f20::stringAt(raw, "command").value_or("unknown")),
                          "not a valid company envelope");
        return;
    }
    if (!message->machineSn.empty() && message->machineSn != machineSn_) {
        emit logLine(QString("[mqtt] message for machine %1 ignored").arg(fromStd(message->machineSn)));
        return;
    }
    const QString transactionId = fromStd(message->transactionId);
    const QString command = fromStd(message->command);

    if (message->commandType == "ack") {
        const auto it = awaitingAck_.find(transactionId);
        if (it == awaitingAck_.end()) {
            emit logLine(QString("[mqtt] <- ack %1 %2 - nothing waits for it").arg(command, transactionId));
            return;
        }
        emit logLine(QString("[mqtt] <- ack %1 %2").arg(command, transactionId));
        it->second.timer->deleteLater();
        awaitingAck_.erase(it);
        return;
    }
    if (message->commandType != "request") {
        emit logLine(QString("[mqtt] <- %1 %2 %3 ignored").arg(fromStd(message->commandType), command, transactionId));
        return;
    }
    emit logLine(QString("[mqtt] <- %1 %2").arg(command, transactionId));
    onRequest(transactionId, command, message->data);
}

void MqttServerLink::onRequest(const QString &transactionId, const QString &command, const f20::json &data)
{
    // QoS 1 can deliver a request twice: answer it again, never run it twice.
    for (const auto &[answeredId, payload] : answered_)
        if (answeredId == transactionId) {
            emit logLine(QString("[mqtt] %1 %2 repeated - same reply sent again").arg(command, transactionId));
            transport_->publish(sendTopic_, payload, 1);
            return;
        }
    if (inProgress_.contains(transactionId)) {
        emit logLine(QString("[mqtt] %1 %2 repeated - still working on it").arg(command, transactionId));
        return;
    }

    if (command == "measure") {
        QString recipe, sample;
        if (!optionalString(data, "recipe_name", recipe) || !optionalString(data, "sample_id", sample))
            return rejectRequest(transactionId, command, "recipe_name and sample_id must be text");
        inProgress_.insert(transactionId);
        emit remoteMeasureRequested(transactionId, recipe, sample);
    } else if (command == "baseline_invalidate") {
        QString reason;
        if (!optionalString(data, "reason", reason))
            return rejectRequest(transactionId, command, "reason must be text");
        inProgress_.insert(transactionId);
        emit remoteBaselineInvalidate(transactionId, reason);
    } else if (command == "get_results") {
        QString since;
        int     page = 1;
        if (!optionalString(data, "since", since))
            return rejectRequest(transactionId, command, "since must be a UTC time text");
        if (data.contains("page")) {
            const auto value = f20::intAt(data, "page");
            if (!value || *value < 1)
                return rejectRequest(transactionId, command, "page must be a whole number from 1");
            page = *value;
        }
        inProgress_.insert(transactionId);
        emit resultsRequested(transactionId, since, page);
    } else if (command == "get_spectrum") {
        QString resultId;
        if (!optionalString(data, "result_id", resultId) || resultId.isEmpty())
            return rejectRequest(transactionId, command, "result_id is required");
        inProgress_.insert(transactionId);
        emit spectrumRequested(transactionId, resultId);
    } else {
        rejectRequest(transactionId, command, "unknown command");
    }
}
