#pragma once
#include <string_view>

namespace f20app {

// Is the bridge talking to the F20 this app is set up for (spec 2.1 #2)?
// The bridge finds its channel by serial number; this compares the serial
// it reports with [device] serial in f20.ini, so results, baselines and
// MQTT topics are never booked to the wrong instrument.
enum class SerialCheck {
    ok,
    notConfigured, // f20.ini has no serial: warn, but go on
    mismatch,      // another F20 is connected
    missing,       // the bridge reports no serial
};

// Compares after trimming spaces, ignoring ASCII case ("f20:09a006" ==
// "F20:09A006").
SerialCheck checkChannelSerial(std::string_view expected, std::string_view reported);

} // namespace f20app
