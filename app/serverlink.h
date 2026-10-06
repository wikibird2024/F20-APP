#pragma once
#include "f20/protocol.h"

#include <QObject>

// The one transport interface of spec §10.2: everything the Linux server
// sees goes through here. The production implementation is MQTT; until
// libmosquitto-dev (or QtMqtt) is installed on the dev machine, the
// NullServerLink below logs instead of publishing, and the rest of the app
// cannot tell the difference - that is the point of the interface.
class ServerLink : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual void publishStatus(const f20::json& status) = 0;
    virtual void publishResult(const f20::json& result) = 0;
    virtual void publishAlarm(const QString& kind, const f20::json& data) = 0;

signals:
    // Remote commands (cmd/measure, cmd/baselineInvalidate...) arrive here.
    void remoteMeasureRequested(const QString& recipeName, const QString& sampleId);
    void remoteBaselineInvalidate();
};

class NullServerLink : public ServerLink {
    Q_OBJECT
public:
    using ServerLink::ServerLink;

    void publishStatus(const f20::json& status) override;
    void publishResult(const f20::json& result) override;
    void publishAlarm(const QString& kind, const f20::json& data) override;
};
