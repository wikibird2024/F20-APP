#include <QCoreApplication>

int runBridgeClientTests(int argc, char** argv);
int runStorageTests(int argc, char** argv);

// One binary, several QtTest classes: each runs with the same arguments,
// any failure makes the exit code non-zero for ctest.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures = 0;
    failures += runBridgeClientTests(argc, argv);
    failures += runStorageTests(argc, argv);
    return failures == 0 ? 0 : 1;
}
