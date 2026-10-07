#pragma once
#include "mqtttransport.h"

#include <vector>

// MqttTransport for tests: no broker, no threads. The test plays the
// broker: it reads what was published and injects incoming messages.
class FakeMqttTransport : public MqttTransport
{
  public:
    struct Published {
        QString    topic;
        QByteArray payload;
        int        qos;
    };

    using MqttTransport::MqttTransport;

    void connectToBroker(const Settings &settings) override
    {
        settings_ = settings;
    }

    void disconnectFromBroker() override
    {
        dropConnection();
    }

    bool isConnected() const override
    {
        return isConnected_;
    }

    void subscribe(const QString &topic, int qos) override
    {
        subscriptions_.push_back({topic, {}, qos});
    }

    bool publish(const QString &topic, const QByteArray &payload, int qos) override
    {
        if (!isConnected_)
            return false;
        published_.push_back({topic, payload, qos});
        return true;
    }

    // --- test controls ---
    void acceptConnection()
    {
        isConnected_ = true;
        emit connected();
    }

    void dropConnection()
    {
        if (!isConnected_)
            return;
        isConnected_ = false;
        emit disconnected();
    }

    void injectMessage(const QString &topic, const QByteArray &payload)
    {
        emit messageReceived(topic, payload);
    }

    const Settings &settings() const
    {
        return settings_;
    }

    const std::vector<Published> &published() const
    {
        return published_;
    }

    const std::vector<Published> &subscriptions() const
    {
        return subscriptions_;
    }

    void clearPublished()
    {
        published_.clear();
    }

  private:
    Settings               settings_;
    bool                   isConnected_ = false;
    std::vector<Published> published_;
    std::vector<Published> subscriptions_; // payload unused
};
