#include <QCoreApplication>

int runBridgeClientTests(int argc, char** argv);
int runStorageTests(int argc, char** argv);
int runLogRetentionTests(int argc, char** argv);
int runMqttServerLinkTests(int argc, char** argv);
#ifdef F20_HAS_MQTT
int runPahoMqttTransportTests(int argc, char** argv);
#endif

// One binary, several QtTest classes: each runs with the same arguments,
// any failure makes the exit code non-zero for ctest.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    failures += runBridgeClientTests(argc, argv);
    failures += runStorageTests(argc, argv);
    failures += runLogRetentionTests(argc, argv);
    failures += runMqttServerLinkTests(argc, argv);
#ifdef F20_HAS_MQTT
    failures += runPahoMqttTransportTests(argc, argv);
#endif
    return failures == 0 ? 0 : 1;
}
