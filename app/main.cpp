#include "mainwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

QFile g_logFile;

// Rotating-by-date plain text log (spec §8 logging rule).
void messageHandler(QtMsgType, const QMessageLogContext&, const QString& text) {
    const QString line =
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + text;
    if (g_logFile.isOpen()) {
        QTextStream(&g_logFile) << line << '\n';
        g_logFile.flush();
    }
    std::fprintf(stderr, "%s\n", qPrintable(line));
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QDir().mkpath("logs");
    g_logFile.setFileName("logs/f20app_" +
                          QDate::currentDate().toString("yyyy-MM-dd") + ".log");
    g_logFile.open(QIODevice::Append | QIODevice::Text);
    qInstallMessageHandler(messageHandler);

    MainWindow window;

    // --smoke: start, try to reach the bridge, report and exit. Lets the
    // build server (and us) verify app+sim end to end without a display.
    if (app.arguments().contains("--smoke")) {
        int exitCode = 1;
        QTimer::singleShot(3000, &app, [&] {
            exitCode = window.bridgeConnected() ? 0 : 1;
            qInfo() << "[smoke]" << (exitCode == 0 ? "bridge connected"
                                                   : "bridge NOT reachable");
            app.exit(exitCode);
        });
        app.exec();
        return exitCode;
    }

    window.show();
    return app.exec();
}
