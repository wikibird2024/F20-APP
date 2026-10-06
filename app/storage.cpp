#include "storage.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

bool Storage::open(const QString& dbPath) {
    db_ = QSqlDatabase::addDatabase("QSQLITE");
    db_.setDatabaseName(dbPath);
    if (!db_.open()) {
        qWarning() << "cannot open database" << dbPath << db_.lastError().text();
        return false;
    }
    QSqlQuery query(db_);
    const bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS measurements ("
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
        " reanalyzedFrom TEXT)");
    if (!ok)
        qWarning() << "cannot create table:" << query.lastError().text();
    return ok;
}

qint64 Storage::insertMeasurement(const f20::MeasureResult& result,
                                  const QString& recipeName,
                                  const QString& channelSerial,
                                  const QString& operatorName,
                                  const QString& sampleId,
                                  const QString& spectrumFile,
                                  const QString& reanalyzedFrom) {
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    qint64 firstId = -1;
    for (const auto& layer : result.layers) {
        QSqlQuery query(db_);
        query.prepare(
            "INSERT INTO measurements (time, recipeName, channelSerial,"
            " operatorName, sampleId, layerNumber, thicknessNm, n, k,"
            " roughnessNm, gof, passed, spectrumFile, reanalyzedFrom)"
            " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        query.addBindValue(now);
        query.addBindValue(recipeName);
        query.addBindValue(channelSerial);
        query.addBindValue(operatorName);
        query.addBindValue(sampleId);
        query.addBindValue(layer.layer);
        query.addBindValue(layer.thicknessNm);
        query.addBindValue(layer.n ? QVariant(*layer.n) : QVariant());
        query.addBindValue(layer.k ? QVariant(*layer.k) : QVariant());
        query.addBindValue(layer.roughnessNm ? QVariant(*layer.roughnessNm)
                                             : QVariant());
        query.addBindValue(result.gof);
        query.addBindValue(result.passed ? 1 : 0);
        query.addBindValue(spectrumFile);
        query.addBindValue(reanalyzedFrom.isEmpty() ? QVariant()
                                                    : QVariant(reanalyzedFrom));
        if (!query.exec()) {
            qWarning() << "insert failed:" << query.lastError().text();
            return -1;
        }
        if (firstId < 0)
            firstId = query.lastInsertId().toLongLong();
    }
    return firstId;
}
