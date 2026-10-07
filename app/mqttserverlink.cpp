#include "mqttserverlink.h"

namespace
{

// "F20:09A006" -> "09A006": the doc's machine_sn and topic use the bare
// number (server-team question 6 may change this).
QString bareSerial(const QString &serial)
{
    return serial.startsWith("F20:") ? serial.mid(4) : serial;
}

// Topic rule of spec §6.6. Moves to f20::sendTopic / receiveTopic with the
// envelope (phase 2).
QString topicFor(const QString &serial, const char *direction)
{
    return QString("%1/ar/f20/%2").arg(bareSerial(serial), direction);
}

QByteArray toPayload(const f20::json &message)
{
    return QByteArray::fromStdString(message.dump());
}

} // namespace

MqttServerLink::MqttServerLink(MqttTransport *transport, const Settings &settings, QObject *parent)
    : ServerLink(parent), transport_(transport), settings_(settings), sendTopic_(topicFor(settings.serial, "send")),
      receiveTopic_(topicFor(settings.serial, "receive"))
{
    transport_->setParent(this);

    connect(transport_, &MqttTransport::connected, this, &MqttServerLink::onConnected);
    connect(transport_, &MqttTransport::disconnected, this, [this] { setState(ServerConnection::lost); });
    connect(transport_, &MqttTransport::messageReceived, this, &MqttServerLink::onMessage);
    connect(transport_, &MqttTransport::logLine, this, &ServerLink::logLine);
}

void MqttServerLink::start()
{
    MqttTransport::Settings broker = settings_.broker;
    broker.clientId = "f20-" + bareSerial(settings_.serial);
    // Last Will: the broker tells the server we are gone, even after a
    // power cut, when we can send nothing ourselves (Sparkplug's "death
    // certificate").
    broker.willTopic = sendTopic_;
    broker.willPayload = toPayload({{"command", "status"}, {"data", {{"machine_status", "Offline"}}}});
    broker.willQos = 1;

    transport_->subscribe(receiveTopic_, 1);
    transport_->connectToBroker(broker);
}

// Status: QoS 0. It is resent on every change and after every reconnect,
// so a lost one is replaced soon; old ones are never worth buffering.
void MqttServerLink::publishStatus(const f20::json &status)
{
    lastStatus_ = status;
    if (transport_->isConnected())
        publish("status", status, 0);
}

void MqttServerLink::publishResult(const f20::json &result)
{
    publish("result", result, 1);
}

void MqttServerLink::publishAlarm(const QString &kind, const f20::json &data)
{
    f20::json alarm = data.is_object() ? data : f20::json::object();
    alarm["kind"] = kind.toStdString();
    publish("alarm", alarm, 1);
}

QString MqttServerLink::connectionDetails() const
{
    return QString("broker: %1:%2\nsend: %3\nreceive: %4")
        .arg(settings_.broker.host)
        .arg(settings_.broker.port)
        .arg(sendTopic_, receiveTopic_);
}

// v0 payload: command + data only. Phase 2 replaces it with the full
// company envelope (command_type, machine_name, machine_sn, transaction_id).
void MqttServerLink::publish(const char *command, const f20::json &data, int qos)
{
    const f20::json message{{"command", command}, {"data", data}};
    if (!transport_->publish(sendTopic_, toPayload(message), qos) && qos > 0)
        emit logLine(QString("[mqtt] %1 NOT sent - no broker connection").arg(command));
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
        publish("status", *lastStatus_, 0);
}

void MqttServerLink::onMessage(const QString &topic, const QByteArray &payload)
{
    emit logLine(QString("[mqtt] received on %1 (%2 bytes) - not handled yet (phase 4)").arg(topic).arg(payload.size()));
}
