#include "pahomqtttransport.h"

#include <mqtt/async_client.h>

#include <QMetaObject>
#include <QRandomGenerator>
#include <QtDebug>

#include <algorithm>
#include <chrono>

namespace
{

// The usual connect failures as a sentence for the log and the Settings
// screen's Test connection. MQTT 3.1.1 brokers answer with a CONNACK return
// code (1-5); MQTT 5 brokers with a reason code (128 and up); Paho's own
// failures (TLS, no answer) are negative.
QString connectFailure(int returnCode, int reasonCode)
{
    // 255 is Paho's "no MQTT 5 reason code" (an MQTT 3.1.1 connect), not a code.
    const int code = reasonCode >= 128 && reasonCode < 255 ? reasonCode : returnCode;
    switch (code) {
        case 1:
        case 132:
            return "the broker does not accept this MQTT version";
        case 2:
        case 133:
            return "the broker refused the client ID";
        case 4:
        case 134:
            return "the broker refused the login: wrong user name or password";
        case 5:
        case 135:
            return "the broker refused the login: not authorized";
        case 3:
        case 136:
            return "the broker is not available";
        default:
            break;
    }
    return QString("connect failed (code %1): no MQTT answer, or the TLS check failed").arg(code);
}

} // namespace

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
        const QString reason = connectFailure(token.get_return_code(), token.get_reason_code());
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

    const std::string serverUri =
        QString("%1://%2:%3").arg(settings_.useTls ? "ssl" : "tcp", settings_.host).arg(settings_.port).toStdString();
    try {
        // No persistence and no offline buffer: publish() while offline
        // fails, and the caller decides what to send again.
        const mqtt::create_options options(settings_.mqttVersion == 500 ? MQTTVERSION_5 : MQTTVERSION_3_1_1);
        client_ = std::make_unique<mqtt::async_client>(serverUri, settings_.clientId.toStdString(), options);
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
    if (settings_.mqttVersion == 500) {
        // MQTT 5 has "clean start" instead of "clean session"; Paho refuses
        // the connect when the old flag is still set.
        builder.mqtt_version(MQTTVERSION_5);
        builder.clean_session(false);
        builder.clean_start(true);
    } else {
        builder.mqtt_version(MQTTVERSION_3_1_1); // the company format needs no MQTT 5 feature
        builder.clean_session(true);
    }
    if (settings_.useTls)
        builder.ssl(sslOptions());
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

// Always check the broker: its certificate must be signed by the CA file
// and be for the host name we connect to (no "accept any certificate").
//
// No ssl error_handler: Paho C++ 1.2 passes connect_options by value, and
// Paho C keeps a pointer to that temporary for the error callback, which
// OpenSSL calls during the handshake after the temporary is gone (ASan:
// stack-use-after-return). Test connection explains TLS failures with its
// own QSslSocket check instead (brokertest.cpp).
mqtt::ssl_options PahoMqttTransport::sslOptions() const
{
    mqtt::ssl_options_builder ssl;
    ssl.trust_store(settings_.caFile.toStdString());
    ssl.enable_server_cert_auth(true);
    ssl.verify(true); // host name
    if (!settings_.clientCertFile.isEmpty()) {
        ssl.key_store(settings_.clientCertFile.toStdString());
        const QString keyFile = settings_.clientKeyFile.isEmpty() ? settings_.clientCertFile : settings_.clientKeyFile;
        ssl.private_key(keyFile.toStdString());
        if (!settings_.clientKeyPassword.isEmpty())
            ssl.private_keypassword(settings_.clientKeyPassword.toStdString());
    }
    return ssl.finalize();
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
    emit connectAttemptFailed(reason);
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
