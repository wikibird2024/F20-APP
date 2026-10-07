#include "storage.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QtDebug>

namespace
{

// UTC with an explicit "Z", so times stay comparable across DST changes
// and between the NUC and the server.
QString isoUtc(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}

// Version 1: the table as it was before the schema had a version number.
const char *const kCreateV1 = "CREATE TABLE IF NOT EXISTS measurements ("
                              " id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              " time TEXT NOT NULL,"
                              " recipeName TEXT NOT NULL,"
                              " channelSerial TEXT,"
                              " operatorName TEXT,"
                              " sampleId TEXT,"
                              " layerNumber INTEGER NOT NULL,"
                              " thicknessNm REAL NOT NULL,"
                              " n REAL, k REAL, roughnessNm REAL,"
                              " gof REAL NOT NULL,"
                              " passed INTEGER NOT NULL,"
                              " spectrumFile TEXT,"
                              " reanalyzedFrom TEXT)";

// Version 2: provenance columns and the baseline history. Rows from
// version 1 keep NULL in the new columns and local time without "Z".
const char *const kUpgradeTo2[] = {
    "ALTER TABLE measurements ADD COLUMN resultId TEXT",
    "ALTER TABLE measurements ADD COLUMN baselineCommittedAtUtc TEXT",
    "ALTER TABLE measurements ADD COLUMN baselineAgeMinutes INTEGER",
    "ALTER TABLE measurements ADD COLUMN appVersion TEXT",
    "ALTER TABLE measurements ADD COLUMN bridgeVersion TEXT",
    "CREATE INDEX measurementsByResult ON measurements (resultId)",
    "CREATE TABLE baselines ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " channelSerial TEXT NOT NULL,"
    " referenceMaterial TEXT,"
    " committedAtUtc TEXT NOT NULL,"
    " invalidatedAtUtc TEXT)",
    "PRAGMA user_version = 2",
};

} // namespace

bool Storage::fail(const QString &message)
{
    lastError_ = message;
    qWarning().noquote() << "[storage]" << message;
    return false;
}

bool Storage::exec(const QString &sql)
{
    QSqlQuery query(db_);
    if (!query.exec(sql))
        return fail(sql.left(60) + ": " + query.lastError().text());
    return true;
}

bool Storage::open(const QString &dbPath)
{
    // Own connection name per Storage, so two instances (tests) never share one.
    static int connectionCount = 0;
    db_ = QSqlDatabase::addDatabase("QSQLITE", QString("f20storage-%1").arg(++connectionCount));
    db_.setDatabaseName(dbPath);
    if (!db_.open())
        return fail("cannot open database " + dbPath + ": " + db_.lastError().text());

    // WAL: the history screen reading never blocks a result being written.
    // synchronous=FULL: a committed result survives a power cut of the PC
    // (sqlite.org/pragma.html; QCoDeS also opens its files in WAL mode).
    {
        QSqlQuery query(db_);
        if (!query.exec("PRAGMA journal_mode=WAL") || !query.next() || query.value(0).toString() != "wal")
            qWarning().noquote() << "[storage] WAL not available for" << dbPath << "- using the default journal";
    }
    if (!exec("PRAGMA synchronous=FULL") || !exec("PRAGMA foreign_keys=ON") || !migrate())
        return false;
    isOpen_ = true;
    return true;
}

bool Storage::migrate()
{
    int version = 0;
    {
        QSqlQuery query(db_);
        if (!query.exec("PRAGMA user_version") || !query.next())
            return fail("cannot read the schema version: " + query.lastError().text());
        version = query.value(0).toInt();
    }
    if (version > kSchemaVersion)
        return fail(QString("database schema %1 is newer than this app understands (%2)").arg(version).arg(kSchemaVersion));

    // Version 0 is a new file or one from before versioning: both get the
    // version 1 table (IF NOT EXISTS keeps an old one as it is).
    if (version < 1 && !exec(kCreateV1))
        return false;

    if (version < 2) {
        // DDL is transactional in SQLite: the upgrade happens completely or
        // not at all, and user_version moves with it.
        if (!db_.transaction())
            return fail("cannot start the schema upgrade: " + db_.lastError().text());
        for (const char *step : kUpgradeTo2) {
            if (!exec(step)) {
                db_.rollback();
                return false;
            }
        }
        if (!db_.commit()) {
            const QString error = db_.lastError().text();
            db_.rollback();
            return fail("cannot commit the schema upgrade: " + error);
        }
        qInfo().noquote() << "[storage] schema upgraded to version 2";
    }
    return true;
}

std::optional<QString> Storage::insertMeasurement(const f20::MeasureResult &result, const MeasurementRecord &record)
{
    if (!isOpen_) {
        fail("database not open");
        return std::nullopt;
    }
    if (result.layers.empty()) {
        fail("result has no layers - nothing stored");
        return std::nullopt;
    }

    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const QString   resultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariant        baselineAt;
    QVariant        baselineAgeMinutes;
    if (record.baselineCommittedAtUtc) {
        baselineAt = isoUtc(*record.baselineCommittedAtUtc);
        baselineAgeMinutes = record.baselineCommittedAtUtc->secsTo(nowUtc) / 60;
    }
    auto textOrNull = [](const QString &text) { return text.isEmpty() ? QVariant() : QVariant(text); };

    if (!db_.transaction()) {
        fail("cannot start a transaction: " + db_.lastError().text());
        return std::nullopt;
    }
    bool ok = true;
    for (const auto &layer : result.layers) {
        QSqlQuery query(db_);
        query.prepare("INSERT INTO measurements (time, recipeName, channelSerial,"
                      " operatorName, sampleId, layerNumber, thicknessNm, n, k,"
                      " roughnessNm, gof, passed, spectrumFile, reanalyzedFrom, resultId,"
                      " baselineCommittedAtUtc, baselineAgeMinutes, appVersion, bridgeVersion)"
                      " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        query.addBindValue(isoUtc(nowUtc));
        query.addBindValue(record.recipeName);
        query.addBindValue(record.channelSerial);
        query.addBindValue(record.operatorName);
        query.addBindValue(record.sampleId);
        query.addBindValue(layer.layer);
        query.addBindValue(layer.thicknessNm);
        query.addBindValue(layer.n ? QVariant(*layer.n) : QVariant());
        query.addBindValue(layer.k ? QVariant(*layer.k) : QVariant());
        query.addBindValue(layer.roughnessNm ? QVariant(*layer.roughnessNm) : QVariant());
        query.addBindValue(result.gof);
        query.addBindValue(result.passed ? 1 : 0);
        query.addBindValue(textOrNull(record.spectrumFile));
        query.addBindValue(textOrNull(record.reanalyzedFrom));
        query.addBindValue(resultId);
        query.addBindValue(baselineAt);
        query.addBindValue(baselineAgeMinutes);
        query.addBindValue(record.appVersion);
        query.addBindValue(textOrNull(record.bridgeVersion));
        if (!query.exec()) {
            fail(QString("insert of layer %1 failed: %2").arg(layer.layer).arg(query.lastError().text()));
            ok = false;
            break;
        }
    }
    if (ok && db_.commit())
        return resultId;
    if (ok)
        fail("commit failed: " + db_.lastError().text());
    db_.rollback();
    return std::nullopt;
}

bool Storage::saveBaseline(const QString &channelSerial, const QString &referenceMaterial, const QDateTime &committedAtUtc)
{
    QSqlQuery query(db_);
    query.prepare("INSERT INTO baselines (channelSerial, referenceMaterial, committedAtUtc)"
                  " VALUES (?,?,?)");
    query.addBindValue(channelSerial);
    query.addBindValue(referenceMaterial);
    query.addBindValue(isoUtc(committedAtUtc));
    if (!query.exec())
        return fail("cannot store the baseline: " + query.lastError().text());
    return true;
}

bool Storage::invalidateBaselines(const QString &channelSerial, const QDateTime &atUtc)
{
    QSqlQuery query(db_);
    query.prepare("UPDATE baselines SET invalidatedAtUtc = ?"
                  " WHERE channelSerial = ? AND invalidatedAtUtc IS NULL");
    query.addBindValue(isoUtc(atUtc));
    query.addBindValue(channelSerial);
    if (!query.exec())
        return fail("cannot invalidate the baseline: " + query.lastError().text());
    return true;
}

std::optional<QDateTime> Storage::lastValidBaseline(const QString &channelSerial)
{
    // The newest row decides: an older valid row must not come back after
    // a newer one was invalidated.
    QSqlQuery query(db_);
    query.prepare("SELECT committedAtUtc, invalidatedAtUtc FROM baselines"
                  " WHERE channelSerial = ? ORDER BY id DESC LIMIT 1");
    query.addBindValue(channelSerial);
    if (!query.exec() || !query.next() || !query.value(1).isNull())
        return std::nullopt;
    const QDateTime committedAt = QDateTime::fromString(query.value(0).toString(), Qt::ISODateWithMs);
    if (!committedAt.isValid())
        return std::nullopt;
    return committedAt;
}

std::optional<QString> Storage::sampleIdForSpectrum(const QString &spectrumFile)
{
    QSqlQuery query(db_);
    query.prepare("SELECT sampleId FROM measurements"
                  " WHERE spectrumFile = ? AND reanalyzedFrom IS NULL"
                  " ORDER BY id DESC LIMIT 1");
    query.addBindValue(spectrumFile);
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toString();
}
