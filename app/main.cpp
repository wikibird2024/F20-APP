#include "logretention.h"
#include "mainwindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSettings>
#include <QTimer>

#include <cstdio>

namespace {

struct LogFile {
    QMutex mutex;
    QDir dir;
    QDate date;
    QFile file;
};
LogFile g_log;

const char* levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:    return "debug";
    case QtInfoMsg:     return "info ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "?    ";
}

// Plain text log, one file per day (spec §7: logs/f20app_<date>.log).
// Qt may log from any thread, so writing is guarded by a mutex; the file
// changes when the date does, also in an app that runs for weeks.
void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& text) {
    const QDateTime now = QDateTime::currentDateTime();
    const QString line =
        now.toString("HH:mm:ss.zzz ") + levelName(type) + ' ' + text;
    {
        QMutexLocker lock(&g_log.mutex);
        if (now.date() != g_log.date) {
            g_log.file.close();
            g_log.date = now.date();
            g_log.file.setFileName(
                g_log.dir.filePath("f20app_" + g_log.date.toString("yyyy-MM-dd") + ".log"));
            g_log.file.open(QIODevice::Append | QIODevice::Text);
        }
        if (g_log.file.isOpen()) {
            g_log.file.write(line.toUtf8() + '\n');
            g_log.file.flush();
        }
    }
    std::fprintf(stderr, "%s\n", qPrintable(line));
}

// --config <file> wins. Otherwise f20.ini next to the executable - not in
// the working folder, which depends on how the app was started.
QString findConfig(const QStringList& arguments) {
    const int index = arguments.indexOf("--config");
    if (index >= 0 && index + 1 < arguments.size())
        return QFileInfo(arguments[index + 1]).absoluteFilePath();
    return QDir(QCoreApplication::applicationDirPath()).filePath("f20.ini");
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("f20app");
    QCoreApplication::setApplicationVersion(F20_APP_VERSION);

    const QString configPath = findConfig(app.arguments());
    g_log.dir = QDir(QFileInfo(configPath).absoluteDir().filePath("logs"));
    QDir().mkpath(g_log.dir.absolutePath());
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << "f20app" << QCoreApplication::applicationVersion()
                      << "config:" << configPath;

    // Old logs go at start and every 6 h - the app runs for weeks.
    const int keepDays = QSettings(configPath, QSettings::IniFormat)
                             .value("logs/keepDays", 30)
                             .toInt();
    const auto cleanLogs = [keepDays] {
        const QStringList deleted = deleteOldLogs(g_log.dir, QDate::currentDate(), keepDays);
        if (!deleted.isEmpty())
            qInfo().noquote() << "[logs] deleted (older than" << keepDays
                              << "days):" << deleted.join(", ");
    };
    cleanLogs();
    QTimer logCleanup;
    QObject::connect(&logCleanup, &QTimer::timeout, cleanLogs);
    logCleanup.start(6 * 60 * 60 * 1000);

    int exitCode = 0;
    {
        MainWindow window(configPath);

        // --smoke: start, try to reach the bridge, report and exit. Lets the
        // build server (and us) verify app+sim end to end without a display.
        if (app.arguments().contains("--smoke")) {
            exitCode = 1;
            QTimer::singleShot(3000, &app, [&] {
                exitCode = window.bridgeConnected() ? 0 : 1;
                qInfo() << "[smoke]" << (exitCode == 0 ? "bridge connected"
                                                       : "bridge NOT reachable");
                app.exit(exitCode);
            });
            app.exec();
        } else {
            window.show();
            exitCode = app.exec();
        }
    }
    // The log file is a static: stop using it before statics are destroyed.
    qInstallMessageHandler(nullptr);
    return exitCode;
}
