// deleteOldLogs on a temporary folder: only our dated log files older than
// keepDays go; the date comes from the file name.
#include "logretention.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

namespace {

void touch(const QDir& dir, const QString& name) {
    QFile file(dir.filePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
}

} // namespace

class LogRetentionTest : public QObject {
    Q_OBJECT
private slots:
    void deletesOnlyOldDatedLogs() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir dir(temp.path());
        for (const char* name : {"f20app_2026-09-01.log", "f20app_2026-09-07.log",
                                 "f20app_2026-09-08.log", "f20app_2026-10-07.log",
                                 "other.txt", "f20app_garbage.log", "f20app_2026-09-01.log.bak"})
            touch(dir, name);

        const QStringList deleted = deleteOldLogs(dir, QDate(2026, 10, 8), 30);

        // 30 days before 8 Oct is 8 Sep: that day is kept, older days go.
        QCOMPARE(deleted, QStringList({"f20app_2026-09-01.log", "f20app_2026-09-07.log"}));
        const QStringList left = dir.entryList(QDir::Files, QDir::Name);
        QCOMPARE(left, QStringList({"f20app_2026-09-01.log.bak", "f20app_2026-09-08.log",
                                    "f20app_2026-10-07.log", "f20app_garbage.log",
                                    "other.txt"}));
    }

    void zeroKeepsEverything() {
        QTemporaryDir temp;
        const QDir dir(temp.path());
        touch(dir, "f20app_2000-01-01.log");
        QVERIFY(deleteOldLogs(dir, QDate(2026, 10, 8), 0).isEmpty());
        QVERIFY(deleteOldLogs(dir, QDate(2026, 10, 8), -5).isEmpty());
        QVERIFY(QFile::exists(dir.filePath("f20app_2000-01-01.log")));
    }
};

int runLogRetentionTests(int argc, char** argv) {
    LogRetentionTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_logretention.moc"
