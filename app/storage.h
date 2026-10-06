#pragma once
#include "f20/results.h"

#include <QSqlDatabase>
#include <QString>

// SQLite storage, one row per measured layer (spec §10.1).
class Storage {
public:
    bool open(const QString& dbPath);

    // Returns the database row id of the first inserted layer, or -1.
    qint64 insertMeasurement(const f20::MeasureResult& result,
                             const QString& recipeName,
                             const QString& channelSerial,
                             const QString& operatorName,
                             const QString& sampleId,
                             const QString& spectrumFile,
                             const QString& reanalyzedFrom = {});

    QSqlDatabase& database() { return db_; }

private:
    QSqlDatabase db_;
};
