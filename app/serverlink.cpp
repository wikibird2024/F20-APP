#include "serverlink.h"

#include <QtDebug>

void NullServerLink::publishStatus(const f20::json& status) {
    qInfo().noquote() << "[server status]"
                      << QString::fromStdString(status.dump());
}

void NullServerLink::publishResult(const f20::json& result) {
    qInfo().noquote() << "[server result]"
                      << QString::fromStdString(result.dump());
}

void NullServerLink::publishAlarm(const QString& kind, const f20::json& data) {
    qWarning().noquote() << "[server alarm]" << kind
                         << QString::fromStdString(data.dump());
}
