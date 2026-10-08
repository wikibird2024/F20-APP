#pragma once
#include <QString>
#include <QStringList>

// The settings the Settings screen shows (spec §6.7), read from two files:
//
//   defaults  f20.ini next to the app: shipped, with comments, never written
//   changes   only what the engineer changed (Windows:
//             C:\ProgramData\Greystone\f20app.ini); a key here wins
//
// The split is the QGIS QgsSettings pattern: QSettings drops comments when
// it writes a file, and Program Files is read-only at run time, so the app
// writes only its own small file. Settings that rarely change (timeouts,
// database, [baselineProfile_*]) stay in f20.ini and are read there.
struct AppSettings {
    // Server (MQTT broker), spec 6.6.7
    QString mqttBroker; // "" = no server: messages are only logged
    int     mqttPort = 1883;
    QString mqttUsername; // "" = no login
    QString mqttPassword;
    bool    mqttUseTls = false;
    QString mqttCaFile;            // company CA, PEM
    QString mqttClientCertFile;    // "" = no client certificate
    QString mqttClientKeyFile;     // "" = the key is inside the certificate file
    QString mqttClientKeyPassword; // "" = key not encrypted
    QString mqttClientId;          // "" = f20-<bare serial>
    int     mqttVersion = 311;     // 311 = MQTT 3.1.1, 500 = MQTT 5 (same values as the C# app)

    QString deviceSerial; // "F20:09A006", as the bridge reports it
    int     bridgePort = 5555;
    QString recipesFolder = "recipes";

    // Defaults for recipes without a [baselineProfile_*]
    int baselineWarnMinutes = 20;
    int baselineBlockMinutes = 30;
    int warmUpMinutes = 15;

    QString engineerPasswordHash; // "" = no password chosen yet

    QString     folder;    // relative paths resolve here: the defaults file's folder
    QStringList loadNotes; // read problems that do not stop the app (e.g. a password this PC cannot decrypt)

    static AppSettings load(const QString &defaultsPath, const QString &changesPath);

    // What is wrong, as sentences for the engineer; empty = usable. The same
    // rules at start-up (logged) and on Save (nothing saved while any).
    QStringList problems() const;

    // Writes the values that differ from the defaults file into changesPath;
    // a value set back to its default is removed. The write is atomic
    // (QSettings uses QSaveFile): the old file stays if it fails.
    bool save(const QString &defaultsPath, const QString &changesPath, QString *error) const;

    // Names of the keys whose values differ, e.g. "mqtt/broker" - for the
    // log; never the values (passwords).
    QStringList changedKeys(const AppSettings &other) const;

    QString resolve(const QString &path) const; // relative to folder
    QString clientIdOrDefault() const;          // mqttClientId, or f20-<bare serial>
};
