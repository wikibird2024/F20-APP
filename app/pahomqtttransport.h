#pragma once
#include "mqtttransport.h"

#include <QTimer>

#include <memory>
#include <utility>
#include <vector>

namespace mqtt
{
class async_client;
class ssl_options;
} // namespace mqtt

// MqttTransport on Eclipse Paho MQTT C++ (async_client).
//
// Threads: Paho calls back on its own thread. Each callback only copies
// what it got and queues it to this object's thread; all state below is
// touched from the Qt thread only (Paho's async_subscribe.cpp sample keeps
// its callbacks just as short).
//
// Reconnect: our own timer, not Paho's automatic_reconnect - that one only
// works after a first successful connect, so a broker that is down at
// app start would never be retried.
class PahoMqttTransport : public MqttTransport
{
    Q_OBJECT
  public:
    explicit PahoMqttTransport(QObject *parent = nullptr);
    ~PahoMqttTransport() override;

    void connectToBroker(const Settings &settings) override;
    void disconnectFromBroker() override;
    bool isConnected() const override;
    void subscribe(const QString &topic, int qos) override;
    bool publish(const QString &topic, const QByteArray &payload, int qos) override;

  private:
    class ConnectListener; // Paho's iaction_listener; defined in the .cpp

    void              startConnect();
    mqtt::ssl_options sslOptions() const;
    void              scheduleReconnect();
    void              onConnectSucceeded(int generation);
    void              onConnectFailed(int generation, const QString &reason);
    void              onConnectionLost(int generation, const QString &cause);
    void              subscribeNow(const QString &topic, int qos);
    void              destroyClient();

    Settings                             settings_;
    std::unique_ptr<mqtt::async_client>  client_;
    std::unique_ptr<ConnectListener>     connectListener_;
    std::vector<std::pair<QString, int>> subscriptions_; // topic, qos
    QTimer                               reconnectTimer_;
    bool                                 wantConnection_ = false;
    bool                                 isConnected_ = false;
    int                                  reconnectAttempts_ = 0;
    // Bumped for every new client: a callback queued by an old client
    // arrives with an old number and is ignored.
    int generation_ = 0;
};
