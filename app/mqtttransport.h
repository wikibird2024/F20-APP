#pragma once
#include <QByteArray>
#include <QObject>
#include <QString>

// The MQTT connection as the rest of the app sees it: connect, publish,
// subscribe, and three signals. No MQTT library type appears here, so
// MqttServerLink and its tests compile without Paho; the tests use a fake
// (tests/qt/fakemqtttransport.h), production uses PahoMqttTransport.
//
// Every signal is emitted in the thread that owns the object (the Qt main
// thread), never in the library's own thread.
class MqttTransport : public QObject
{
    Q_OBJECT
  public:
    struct Settings {
        QString host;
        quint16 port = 1883;
        QString clientId; // unique per broker; a second client with the same id kicks the first
        QString username; // empty = no login
        QString password;
        int     keepAliveSeconds = 10;
        // Retry delays, same rule as BridgeClient: first delay, x1.6 per
        // retry, +-20 % jitter, a cap.
        int reconnectFirstDelayMs = 1000;
        int reconnectMaxDelayMs = 30000;
        // Last Will: the broker publishes it when this client drops without
        // a clean disconnect. Empty topic = no Last Will.
        QString    willTopic;
        QByteArray willPayload;
        int        willQos = 1;
    };

    using QObject::QObject;

    // Starts connecting and keeps retrying until disconnectFromBroker().
    virtual void connectToBroker(const Settings &settings) = 0;
    virtual void disconnectFromBroker() = 0;
    virtual bool isConnected() const = 0;

    // Remembered: the topic is subscribed again after every (re)connect,
    // because the session is clean (old subscriptions and old messages are
    // dropped by the broker on purpose - a measure command sent while the
    // NUC was off must not run hours later).
    virtual void subscribe(const QString &topic, int qos) = 0;

    // Returns false when the message could not be handed to the library
    // (not connected, or the library refused it). Nothing is buffered
    // while offline: the caller decides what is worth sending again.
    virtual bool publish(const QString &topic, const QByteArray &payload, int qos) = 0;

  signals:
    void connected();
    void disconnected();
    void messageReceived(const QString &topic, const QByteArray &payload);
    void logLine(const QString &line); // feed for the diagnostics log
};
