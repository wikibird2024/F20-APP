#pragma once
#include "appsettings.h"
#include "mqtttransport.h"

#include <functional>

class QObject;

// The MQTT connection settings these app settings give: the same for the
// app's own link and for Test connection, so a test that passes means the
// app will connect too. Keep-alive and the Last Will are set by the caller.
MqttTransport::Settings transportSettings(const AppSettings &settings);

// The Settings screen's Test connection (spec 6.7): connects with the
// values in the form (not yet saved) and its own client ID - the running
// app keeps its connection - then disconnects. One try, 6 s at most.
// done gets one sentence for the engineer. Nothing happens after context
// is destroyed.
void testBroker(const AppSettings &settings, QObject *context, std::function<void(const QString &)> done);
