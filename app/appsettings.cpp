#include "appsettings.h"

#include "recipelimits.h"
#include "storedsecret.h"

#include <f20/envelope.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <variant>

namespace
{

// One row per setting: its key in the ini files and the member it fills.
// Adding a setting = one member + one row; load, save and changedKeys
// follow.
using Member = std::variant<QString AppSettings::*, int AppSettings::*, bool AppSettings::*>;

struct Field {
    const char *key;
    Member      member;
    bool        isSecret = false; // encrypted in the changes file (StoredSecret)
};

const Field kFields[] = {
    {"mqtt/broker", &AppSettings::mqttBroker},
    {"mqtt/port", &AppSettings::mqttPort},
    {"mqtt/username", &AppSettings::mqttUsername},
    {"mqtt/password", &AppSettings::mqttPassword, true},
    {"mqtt/useTls", &AppSettings::mqttUseTls},
    {"mqtt/caFile", &AppSettings::mqttCaFile},
    {"mqtt/clientCertFile", &AppSettings::mqttClientCertFile},
    {"mqtt/clientKeyFile", &AppSettings::mqttClientKeyFile},
    {"mqtt/clientKeyPassword", &AppSettings::mqttClientKeyPassword, true},
    {"mqtt/clientId", &AppSettings::mqttClientId},
    {"mqtt/version", &AppSettings::mqttVersion},
    {"device/serial", &AppSettings::deviceSerial},
    {"bridge/port", &AppSettings::bridgePort},
    {"recipes/folder", &AppSettings::recipesFolder},
    {"baseline/warnMinutes", &AppSettings::baselineWarnMinutes},
    {"baseline/blockMinutes", &AppSettings::baselineBlockMinutes},
    {"baseline/warmUpMinutes", &AppSettings::warmUpMinutes},
    {"security/engineerPasswordHash", &AppSettings::engineerPasswordHash},
};

QVariant valueOf(const AppSettings &settings, const Field &field)
{
    return std::visit([&settings](auto member) { return QVariant(settings.*member); }, field.member);
}

void readFile(AppSettings &settings, const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    QSettings file(path, QSettings::IniFormat);
    for (const Field &field : kFields) {
        if (!file.contains(field.key))
            continue;
        const QVariant value = file.value(field.key);
        if (auto text = std::get_if<QString AppSettings::*>(&field.member)) {
            bool readable = true;
            settings.**text = field.isSecret ? StoredSecret::unprotect(value.toString(), &readable) : value.toString();
            if (!readable)
                settings.loadNotes << QString("%1 in %2 was saved on another PC - enter it again in the Settings tab")
                                          .arg(field.key, QDir::toNativeSeparators(path));
        } else if (auto number = std::get_if<int AppSettings::*>(&field.member)) {
            bool      isNumber = false;
            const int parsed = value.toString().trimmed().toInt(&isNumber);
            if (isNumber)
                settings.**number = parsed;
            else
                settings.loadNotes << QString("%1 = '%2' in %3 is not a whole number - using %4")
                                          .arg(field.key, value.toString(), QDir::toNativeSeparators(path))
                                          .arg(settings.**number);
        } else if (auto yesNo = std::get_if<bool AppSettings::*>(&field.member)) {
            settings.**yesNo = value.toBool();
        }
    }
}

bool isPortNumber(int port)
{
    return port >= 1 && port <= 65535;
}

} // namespace

AppSettings AppSettings::load(const QString &defaultsPath, const QString &changesPath)
{
    AppSettings settings;
    settings.folder = QFileInfo(defaultsPath).absolutePath();
    readFile(settings, defaultsPath);
    readFile(settings, changesPath);
    return settings;
}

QStringList AppSettings::problems() const
{
    QStringList found;
    if (!isPortNumber(mqttPort))
        found << QString("MQTT port must be 1-65535 (now %1)").arg(mqttPort);
    if (mqttVersion != 311 && mqttVersion != 500)
        found << QString("MQTT version must be 311 (MQTT 3.1.1) or 500 (MQTT 5), not %1").arg(mqttVersion);
    if (mqttClientId.contains(QRegularExpression("\\s")))
        found << "MQTT client ID must not contain spaces";

    const bool hasCertificateFiles = !mqttCaFile.isEmpty() || !mqttClientCertFile.isEmpty() || !mqttClientKeyFile.isEmpty();
    if (hasCertificateFiles && !mqttUseTls)
        found << "MQTT certificate files are set but TLS is off - turn TLS on or clear the files";
    if (mqttUseTls) {
        // Paho uses OpenSSL, which does not read the Windows certificate
        // store: the CA must be given as a file (unlike the C# app).
        if (mqttCaFile.isEmpty())
            found << "TLS is on: choose the company CA certificate file (.crt / .pem)";
        else if (!QFileInfo::exists(resolve(mqttCaFile)))
            found << "CA certificate file not found: " + QDir::toNativeSeparators(resolve(mqttCaFile));
    }
    if (!mqttClientCertFile.isEmpty()) {
        const QString suffix = QFileInfo(mqttClientCertFile).suffix().toLower();
        if (!QFileInfo::exists(resolve(mqttClientCertFile)))
            found << "Client certificate file not found: " + QDir::toNativeSeparators(resolve(mqttClientCertFile));
        else if (suffix == "pfx" || suffix == "p12")
            found << "The client certificate must be PEM. Convert it once: openssl pkcs12 -in " +
                         QFileInfo(mqttClientCertFile).fileName() + " -out f20-client.pem";
    }
    if (!mqttClientKeyFile.isEmpty()) {
        if (mqttClientCertFile.isEmpty())
            found << "A client key file needs its client certificate file";
        else if (!QFileInfo::exists(resolve(mqttClientKeyFile)))
            found << "Client key file not found: " + QDir::toNativeSeparators(resolve(mqttClientKeyFile));
    }

    if (deviceSerial.trimmed().isEmpty())
        found << "Set the F20 serial number - it names the MQTT topics and is checked against the bridge";
    if (!isPortNumber(bridgePort))
        found << QString("Bridge port must be 1-65535 (now %1)").arg(bridgePort);
    if (!QFileInfo(resolve(recipesFolder)).isDir())
        found << "Recipes folder not found: " + QDir::toNativeSeparators(resolve(recipesFolder));

    f20app::BaselineLimits limits;
    limits.warnMinutes = baselineWarnMinutes;
    limits.blockMinutes = baselineBlockMinutes;
    limits.warmUpMinutes = warmUpMinutes;
    if (const auto problem = f20app::checkLimits(limits))
        found << "Baseline: " + QString::fromStdString(*problem);
    return found;
}

bool AppSettings::save(const QString &defaultsPath, const QString &changesPath, QString *error) const
{
    const AppSettings defaults = load(defaultsPath, {});
    if (!QDir().mkpath(QFileInfo(changesPath).absolutePath())) {
        if (error)
            *error = "cannot create the folder " + QDir::toNativeSeparators(QFileInfo(changesPath).absolutePath());
        return false;
    }
    // Keys this table does not know are left as they are in the file.
    QSettings file(changesPath, QSettings::IniFormat);
    for (const Field &field : kFields) {
        const QVariant value = valueOf(*this, field);
        if (value == valueOf(defaults, field))
            file.remove(field.key);
        else if (field.isSecret)
            file.setValue(field.key, StoredSecret::protect(value.toString()));
        else
            file.setValue(field.key, value);
    }
    file.sync();
    if (file.status() != QSettings::NoError) {
        if (error)
            *error = "cannot write " + QDir::toNativeSeparators(changesPath);
        return false;
    }
    return true;
}

QStringList AppSettings::changedKeys(const AppSettings &other) const
{
    QStringList keys;
    for (const Field &field : kFields)
        if (valueOf(*this, field) != valueOf(other, field))
            keys << field.key;
    return keys;
}

QString AppSettings::resolve(const QString &path) const
{
    if (path.isEmpty() || QDir::isAbsolutePath(path))
        return path;
    return QDir(folder).absoluteFilePath(path);
}

QString AppSettings::clientIdOrDefault() const
{
    if (!mqttClientId.trimmed().isEmpty())
        return mqttClientId.trimmed();
    return "f20-" + QString::fromStdString(f20::bareSerial(deviceSerial.trimmed().toStdString()));
}
