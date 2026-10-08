// AppSettings (spec §6.7): shipped defaults + a changes file that wins;
// Save writes only what differs; the checks used at start-up and on Save.
// EngineerPassword: the C# app's hash format, salted PBKDF2-SHA256.
#include "appsettings.h"
#include "brokertest.h"
#include "engineerpassword.h"
#include "storedsecret.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

namespace
{

void writeFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(text);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A shipped f20.ini as in the repo: comments, a profile, keys the
// Settings screen does not show.
const QByteArray kDefaults = "; shipped defaults - comments must survive\n"
                             "[bridge]\nport=5555\ntimeoutMs=15000\n"
                             "[baseline]\nwarnMinutes=20\nblockMinutes=30\nwarmUpMinutes=15\n"
                             "[baselineProfile_thick]\nrecipes=\"SiN thick\"\nwarnMinutes=240\n"
                             "[recipes]\nfolder=recipes\n"
                             "[device]\nserial=F20:SIM001\n"
                             "[mqtt]\nbroker=\nport=1883\n";

struct Folder {
    QTemporaryDir dir;
    QString       defaults;
    QString       changes;
    Folder()
    {
        defaults = dir.filePath("f20.ini");
        changes = dir.filePath("changes/f20app.ini");
        writeFile(defaults, kDefaults);
        QDir(dir.path()).mkpath("recipes");
    }
};

} // namespace

class AppSettingsTest : public QObject
{
    Q_OBJECT
  private slots:
    void defaultsAloneAreUsable()
    {
        Folder            folder;
        const AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        QCOMPARE(settings.deviceSerial, QString("F20:SIM001"));
        QCOMPARE(settings.mqttPort, 1883);
        QCOMPARE(settings.mqttVersion, 311);
        QVERIFY(!settings.mqttUseTls);
        QCOMPARE(settings.clientIdOrDefault(), QString("f20-SIM001"));
        QCOMPARE(settings.problems(), QStringList());
        QVERIFY(settings.loadNotes.isEmpty());
    }

    void aChangedValueWins()
    {
        Folder folder;
        QDir(folder.dir.path()).mkpath("changes");
        writeFile(folder.changes, "[mqtt]\nbroker=broker.plant\nport=8883\n[baseline]\nwarnMinutes=25\n");
        const AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        QCOMPARE(settings.mqttBroker, QString("broker.plant"));
        QCOMPARE(settings.mqttPort, 8883);
        QCOMPARE(settings.baselineWarnMinutes, 25);
        QCOMPARE(settings.baselineBlockMinutes, 30); // not changed: from the defaults
    }

    void saveWritesOnlyTheChangesAndNeverTouchesTheDefaults()
    {
        Folder      folder;
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttBroker = "broker.plant";
        settings.mqttPassword = "secret";
        settings.baselineWarnMinutes = 25;
        QString error;
        QVERIFY2(settings.save(folder.defaults, folder.changes, &error), qPrintable(error));

        QCOMPARE(readFile(folder.defaults), kDefaults);
        QSettings written(folder.changes, QSettings::IniFormat);
        QCOMPARE(written.allKeys().size(), 3);
        QCOMPARE(written.value("mqtt/broker").toString(), QString("broker.plant"));
        QVERIFY(!written.contains("device/serial")); // same as the default
        QCOMPARE(StoredSecret::unprotect(written.value("mqtt/password").toString()), QString("secret"));

        const AppSettings again = AppSettings::load(folder.defaults, folder.changes);
        QCOMPARE(again.mqttBroker, QString("broker.plant"));
        QCOMPARE(again.mqttPassword, QString("secret"));
        QCOMPARE(again.baselineWarnMinutes, 25);
        QCOMPARE(again.changedKeys(settings), QStringList());
        QCOMPARE(again.changedKeys(AppSettings::load(folder.defaults, {})),
                 QStringList({"mqtt/broker", "mqtt/password", "baseline/warnMinutes"}));
    }

    void aValueSetBackToItsDefaultIsRemoved()
    {
        Folder      folder;
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.baselineWarnMinutes = 25;
        QVERIFY(settings.save(folder.defaults, folder.changes, nullptr));
        settings.baselineWarnMinutes = 20;
        QVERIFY(settings.save(folder.defaults, folder.changes, nullptr));
        QVERIFY(!QSettings(folder.changes, QSettings::IniFormat).contains("baseline/warnMinutes"));
    }

    void keysNotInTheTableStayInTheChangesFile()
    {
        Folder folder;
        QDir(folder.dir.path()).mkpath("changes");
        writeFile(folder.changes, "[logs]\nkeepDays=90\n");
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttBroker = "broker.plant";
        QVERIFY(settings.save(folder.defaults, folder.changes, nullptr));
        QCOMPARE(QSettings(folder.changes, QSettings::IniFormat).value("logs/keepDays").toInt(), 90);
    }

    void aFailedSaveKeepsTheOldFile()
    {
        Folder folder;
        QDir(folder.dir.path()).mkpath("changes");
        writeFile(folder.changes, "[mqtt]\nbroker=old.plant\n");
        QFile::setPermissions(folder.dir.filePath("changes"), QFile::ReadOwner | QFile::ExeOwner);

        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttBroker = "new.plant";
        QString    error;
        const bool saved = settings.save(folder.defaults, folder.changes, &error);
        QFile::setPermissions(folder.dir.filePath("changes"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (saved)
            QSKIP("this user can write a read-only folder (root?)");
        QVERIFY(error.contains("cannot write"));
        QCOMPARE(readFile(folder.changes), QByteArray("[mqtt]\nbroker=old.plant\n"));
    }

    void badNumbersAndForeignPasswordsAreNotesNotCrashes()
    {
        Folder folder;
        QDir(folder.dir.path()).mkpath("changes");
        writeFile(folder.changes, "[mqtt]\nport=eighty\npassword=dpapi:AAAA\n");
        const AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        QCOMPARE(settings.mqttPort, 1883);
        QCOMPARE(settings.mqttPassword, QString());
        QCOMPARE(settings.loadNotes.size(), 2);
        QVERIFY(settings.loadNotes.join('\n').contains("enter it again in the Settings tab"));
    }

    void problemsNameEachWrongValue()
    {
        Folder      folder;
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttPort = 0;
        settings.mqttVersion = 4;
        settings.mqttClientId = "f20 one";
        settings.deviceSerial = " ";
        settings.bridgePort = 70000;
        settings.recipesFolder = "missing";
        settings.baselineBlockMinutes = 10; // less than warn (20)
        const QString text = settings.problems().join('\n');
        QCOMPARE(settings.problems().size(), 7);
        for (const char *expected :
             {"MQTT port", "MQTT version", "spaces", "serial number", "Bridge port", "Recipes folder", "Baseline"})
            QVERIFY2(text.contains(expected), expected);
    }

    void tlsNeedsItsFiles()
    {
        Folder      folder;
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttCaFile = "ca.crt";
        QVERIFY(settings.problems().join('\n').contains("TLS is off"));

        settings.mqttUseTls = true;
        settings.mqttCaFile.clear();
        QVERIFY(settings.problems().join('\n').contains("choose the company CA"));

        settings.mqttCaFile = "ca.crt"; // relative: next to f20.ini
        QVERIFY(settings.problems().join('\n').contains("CA certificate file not found"));
        writeFile(folder.dir.filePath("ca.crt"), "-----BEGIN CERTIFICATE-----\n");
        QCOMPARE(settings.problems(), QStringList());

        settings.mqttClientCertFile = "f20.pfx";
        writeFile(folder.dir.filePath("f20.pfx"), "binary");
        QVERIFY(settings.problems().join('\n').contains("openssl pkcs12 -in f20.pfx"));
        settings.mqttClientCertFile.clear();
        settings.mqttClientKeyFile = "f20.key";
        QVERIFY(settings.problems().join('\n').contains("needs its client certificate"));
    }

    void theAppAndTestConnectionGetTheSameBrokerSettings()
    {
        Folder      folder;
        AppSettings settings = AppSettings::load(folder.defaults, folder.changes);
        settings.mqttBroker = " broker.plant ";
        settings.mqttUseTls = true;
        settings.mqttCaFile = "ca.crt";
        settings.mqttVersion = 500;
        const MqttTransport::Settings broker = transportSettings(settings);
        QCOMPARE(broker.host, QString("broker.plant"));
        QCOMPARE(broker.clientId, QString("f20-SIM001"));
        QCOMPARE(broker.caFile, QDir(folder.dir.path()).absoluteFilePath("ca.crt"));
        QCOMPARE(broker.clientCertFile, QString());
        QVERIFY(broker.useTls);
        QCOMPARE(broker.mqttVersion, 500);
        settings.mqttClientId = "f20-line2";
        QCOMPARE(transportSettings(settings).clientId, QString("f20-line2"));
    }
};

class EngineerPasswordTest : public QObject
{
    Q_OBJECT
  private slots:
    void onlyASaltedHashIsStored()
    {
        const QString first = EngineerPassword::hash("Engineer1", 1000);
        QVERIFY(first.startsWith("pbkdf2$1000$"));
        QVERIFY(!first.contains("Engineer1"));
        QVERIFY(first != EngineerPassword::hash("Engineer1", 1000)); // new salt each time
        QVERIFY(EngineerPassword::verify("Engineer1", first));
        QVERIFY(!EngineerPassword::verify("engineer1", first)); // case counts
        QVERIFY(!EngineerPassword::verify("", first));
    }

    void newHashesUseTheOwaspCount()
    {
        QVERIFY(EngineerPassword::hash("1234").startsWith("pbkdf2$600000$"));
    }

    // RFC 7914 section 11, PBKDF2-HMAC-SHA256 vector 1, written in the C#
    // app's format: a hash from the C# app verifies here too.
    void knownVectorInTheCSharpFormat()
    {
        const QString stored = "pbkdf2$1$c2FsdA==$VawEblbjCJ/sFpHCJUS2BflBhSFt3gRl5oudV8INrLxJypzM8Xm2RZkWZLOdd+8xfHG4"
                               "RbHjC9UJESBB06GXgw==";
        QVERIFY(EngineerPassword::verify("passwd", stored));
        QVERIFY(!EngineerPassword::verify("passwe", stored));
    }

    void garbageNeverVerifies()
    {
        for (const char *stored : {"",
                                   "pbkdf2",
                                   "pbkdf2$x$c2FsdA==$AAAA",
                                   "pbkdf2$1$c2FsdA==$",
                                   "sha1$1$a$b",
                                   "pbkdf2$1$!!!$AAAA",
                                   "pbkdf2$0$c2FsdA==$AAAA"})
            QVERIFY2(!EngineerPassword::verify("passwd", stored), stored);
    }
};

int runAppSettingsTests(int argc, char **argv)
{
    AppSettingsTest      settings;
    EngineerPasswordTest password;
    return QTest::qExec(&settings, argc, argv) + QTest::qExec(&password, argc, argv);
}

#include "test_appsettings.moc"
