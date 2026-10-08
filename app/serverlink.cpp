#include "serverlink.h"

#include <QtDebug>

QString toString(ServerConnection state)
{
    switch (state) {
        case ServerConnection::off:
            return "off";
        case ServerConnection::connecting:
            return "connecting…";
        case ServerConnection::connected:
            return "connected";
        case ServerConnection::lost:
            return "lost, retrying";
    }
    return "unknown";
}

void NullServerLink::publishStatus(const f20::json &status)
{
    if (status == lastStatus_)
        return;
    lastStatus_ = status;
    qInfo().noquote() << "[server status]" << QString::fromStdString(status.dump());
}

void NullServerLink::publishResult(const f20::json &result)
{
    qInfo().noquote() << "[server result]" << QString::fromStdString(result.dump());
}

void NullServerLink::publishAlarm(const QString &kind, const f20::json &data)
{
    qWarning().noquote() << "[server alarm]" << kind << QString::fromStdString(data.dump());
}

void NullServerLink::sendResponse(const QString &transactionId, const QString &command, const f20::json &data)
{
    qInfo().noquote() << "[server response]" << command << transactionId << QString::fromStdString(data.dump());
}

void NullServerLink::sendAck(const QString &transactionId, const QString &command, const f20::json &data)
{
    qInfo().noquote() << "[server ack]" << command << transactionId << QString::fromStdString(data.dump());
}
