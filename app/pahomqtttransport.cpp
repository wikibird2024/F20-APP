#include "pahomqtttransport.h"

#include <mqtt/async_client.h>

#include <QMetaObject>
#include <QRandomGenerator>
#include <QtDebug>

#include <algorithm>
#include <chrono>

// Paho reports the end of a connect attempt here, on its own thread.
class PahoMqttTransport::ConnectListener : public mqtt::iaction_listener
{
  public:
    ConnectListener(PahoMqttTransport *owner, int generation) : owner_(owner), generation_(generation)
    {
    }

    void on_success(const mqtt::token &) override
    {
        QMetaObject::invokeMethod(
            owner_, [owner = owner_, generation = generation_] { owner->onConnectSucceeded(generation); }, Qt::QueuedConnection);
    }

    void on_failure(const mqtt::token &token) override
    {
        const QString reason = QString("connect failed, code %1").arg(token.get_return_code());
        QMetaObject::invokeMethod(
            owner_,
            [owner = owner_, generation = generation_, reason] { owner->onConnectFailed(generation, reason); },
            Qt::QueuedConnection);
    }

  private:
    PahoMqttTransport *owner_;
    int                generation_;
};

PahoMqttTransport::PahoMqttTransport(QObject *parent) : MqttTransport(parent)
{
    reconnectTimer_.setSingleShot(true);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this] {
        if (wantConnection_ && !isConnected_)
            startConnect();
    });
}

PahoMqttTransport::~PahoMqttTransport()
{
    destroyClient();
}

void PahoMqttTransport::connectToBroker(const Settings &settings)
{
    destroyClient(); // fresh start, also when only the settings changed
    settings_ = settings;
    wantConnection_ = true;
    reconnectAttempts_ = 0;
    ++generation_;

    const std::string serverUri = QString("tcp://%1:%2").arg(settings_.host).arg(settings_.port).toStdString();
    try {
        // No persistence and no offline buffer: publish() while offline
        // fails, and the caller decides what to send again.
        client_ = std::make_unique<mqtt::async_client>(serverUri, settings_.clientId.toStdString());
    } catch (const std::exception &error) {
        // Bad URI or client id: retrying cannot help, the config must change.
        wantConnection_ = false;
        emit logLine(QString("[mqtt] cannot create client for %1: %2").arg(QString::fromStdString(serverUri), error.what()));
        return;
    }

    const int generation = generation_;
    connectListener_ = std::make_unique<ConnectListener>(this, generation);
    client_->set_connection_lost_handler([this, generation](const std::string &cause) {
        const QString text = QString::fromStdString(cause);
        QMetaObject::invokeMethod(this, [this, generation, text] { onConnectionLost(generation, text); }, Qt::QueuedConnection);
    });
    client_->set_message_callback([this, generation](mqtt::const_message_ptr message) {
        const QString    topic = QString::fromStdString(message->get_topic());
        const auto      &bytes = message->get_payload(); // not _ref: that one is null for an empty payload
        const QByteArray payload(bytes.data(), static_cast<int>(bytes.size()));
        QMetaObject::invokeMethod(
            this,
            [this, generation, topic, payload] {
                if (generation == generation_)
                    emit messageReceived(topic, payload);
            },
            Qt::QueuedConnection);
    });

    startConnect();
}

void PahoMqttTransport::disconnectFromBroker()
{
    const bool wasConnected = isConnected_;
    destroyClient();
    if (wasConnected)
        emit disconnected();
}

bool PahoMqttTransport::isConnected() const
{
    return isConnected_;
}

void PahoMqttTransport::subscribe(const QString &topic, int qos)
{
    for (const auto &entry : subscriptions_)
        if (entry.first == topic)
            return;
    subscriptions_.emplace_back(topic, qos);
    if (isConnected_)
        subscribeNow(topic, qos);
}

bool PahoMqttTransport::publish(const QString &topic, const QByteArray &payload, int qos)
{
    if (!client_ || !isConnected_)
        return false;
    try {
        client_->publish(topic.toStdString(), payload.constData(), static_cast<size_t>(payload.size()), qos, false);
        return true;
    } catch (const std::exception &error) {
        emit logLine(QString("[mqtt] publish to %1 failed: %2").arg(topic, error.what()));
        return false;
    }
}

void PahoMqttTransport::startConnect()
{
    mqtt::connect_options_builder builder;
    builder.mqtt_version(MQTTVERSION_3_1_1); // the company format needs no MQTT 5 feature
    builder.clean_session(true);
    builder.keep_alive_interval(std::chrono::seconds(settings_.keepAliveSeconds));
    builder.connect_timeout(std::chrono::seconds(5));
    builder.automatic_reconnect(false); // our timer does it, see the header
    if (!settings_.username.isEmpty()) {
        builder.user_name(settings_.username.toStdString());
        builder.password(settings_.password.toStdString());
    }
    if (!settings_.willTopic.isEmpty())
        builder.will(mqtt::message(settings_.willTopic.toStdString(),
                                   settings_.willPayload.constData(),
                                   static_cast<size_t>(settings_.willPayload.size()),
                                   settings_.willQos,
                                   false));

    emit logLine(QString("[mqtt] connecting to %1:%2").arg(settings_.host).arg(settings_.port));
    try {
        client_->connect(builder.finalize(), nullptr, *connectListener_);
    } catch (const std::exception &error) {
        onConnectFailed(generation_, QString::fromUtf8(error.what()));
    }
}

// Same backoff as BridgeClient::scheduleReconnect (gRPC connection-backoff.md).
void PahoMqttTransport::scheduleReconnect()
{
    ++reconnectAttempts_;
    double delayMs = settings_.reconnectFirstDelayMs;
    for (int i = 1; i < reconnectAttempts_ && delayMs < settings_.reconnectMaxDelayMs; ++i)
        delayMs *= 1.6;
    delayMs = std::min(delayMs, static_cast<double>(settings_.reconnectMaxDelayMs));
    delayMs *= 0.8 + 0.4 * QRandomGenerator::global()->generateDouble();
    reconnectTimer_.start(static_cast<int>(delayMs));
}

void PahoMqttTransport::onConnectSucceeded(int generation)
{
    if (generation != generation_ || !wantConnection_)
        return;
    isConnected_ = true;
    reconnectAttempts_ = 0;
    emit logLine("[mqtt] connected");
    for (const auto &entry : subscriptions_) // clean session: the broker forgot them
        subscribeNow(entry.first, entry.second);
    emit connected();
}

void PahoMqttTransport::onConnectFailed(int generation, const QString &reason)
{
    if (generation != generation_ || !wantConnection_)
        return;
    emit logLine("[mqtt] " + reason);
    scheduleReconnect();
}

void PahoMqttTransport::onConnectionLost(int generation, const QString &cause)
{
    if (generation != generation_ || !isConnected_)
        return;
    isConnected_ = false;
    emit logLine("[mqtt] connection lost" + (cause.isEmpty() ? QString() : ": " + cause));
    emit disconnected();
    if (wantConnection_)
        scheduleReconnect();
}

void PahoMqttTransport::subscribeNow(const QString &topic, int qos)
{
    try {
        client_->subscribe(topic.toStdString(), qos);
        emit logLine("[mqtt] subscribed " + topic);
    } catch (const std::exception &error) {
        emit logLine(QString("[mqtt] subscribe %1 failed: %2").arg(topic, error.what()));
    }
}

// Order matters (lesson from BridgeClient's teardown bug, commit 59c7dc6):
// bump the generation first, so anything Paho still calls back with is
// ignored; disconnect while the callbacks are still set (messages that
// arrive with no callback stay in Paho C's queue and leak - seen with
// ASan); then turn the callbacks off and free the client.
void PahoMqttTransport::destroyClient()
{
    wantConnection_ = false;
    isConnected_ = false;
    reconnectTimer_.stop();
    ++generation_;
    if (!client_)
        return;
    try {
        if (client_->is_connected())
            client_->disconnect()->wait_for(std::chrono::seconds(1));
        client_->disable_callbacks();
    } catch (const std::exception &error) {
        qWarning().noquote() << "[mqtt] disconnect:" << error.what();
    }
    client_.reset();
    connectListener_.reset(); // after the client: an attempt in flight may still use it
}
