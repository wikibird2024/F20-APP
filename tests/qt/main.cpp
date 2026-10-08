#include <QApplication>

int runAppSettingsTests(int argc, char** argv);
int runBridgeClientTests(int argc, char** argv);
int runStorageTests(int argc, char** argv);
int runLogRetentionTests(int argc, char** argv);
int runMqttServerLinkTests(int argc, char** argv);
int runSettingsScreenTests(int argc, char** argv);
#ifdef F20_HAS_MQTT
int runPahoMqttTransportTests(int argc, char** argv);
int runMqttTlsTests(int argc, char** argv);
#endif

// One binary, several QtTest classes: each runs with the same arguments,
// any failure makes the exit code non-zero for ctest.
int main(int argc, char** argv) {
    QApplication app(argc, argv); // the Settings screen test needs widgets
    int failures = 0;
    failures += runAppSettingsTests(argc, argv);
    failures += runBridgeClientTests(argc, argv);
    failures += runStorageTests(argc, argv);
    failures += runLogRetentionTests(argc, argv);
    failures += runMqttServerLinkTests(argc, argv);
    failures += runSettingsScreenTests(argc, argv);
#ifdef F20_HAS_MQTT
    failures += runPahoMqttTransportTests(argc, argv);
    failures += runMqttTlsTests(argc, argv);
#endif
    return failures == 0 ? 0 : 1;
}
