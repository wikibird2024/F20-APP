#pragma once
#include "f20/results.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <optional>

// What the database keeps beside the layer values of one result, so a row
// can be explained later without asking anyone: which baseline it used,
// which software made it, which spectrum file it came from (the "snapshot
// per run" idea of QCoDeS and Bluesky).
struct MeasurementRecord {
    QString recipeName;
    QString channelSerial;
    QString operatorName;
    QString sampleId;
    QString spectrumFile;   // empty when the spectrum was not saved
    QString reanalyzedFrom; // spectrum file a re-analysis was made from
    std::optional<QDateTime> baselineCommittedAtUtc; // empty for a re-analysis
    QString appVersion;
    QString bridgeVersion;
};

// SQLite storage, one row per measured layer (spec §6.5), plus the history
// of baseline commits that gives a baseline its true age after a restart.
class Storage {
public:
    // Kept in PRAGMA user_version. open() upgrades older files step by step
    // and refuses newer ones (QCoDeS: dataset/sqlite/db_upgrades).
    static constexpr int kSchemaVersion = 2;

    bool open(const QString& dbPath);
    bool isOpen() const { return isOpen_; }
    QString lastError() const { return lastError_; }

    // All layers of one result in one transaction: every row is written, or
    // none. Returns the result id (a UUID shared by its layer rows).
    std::optional<QString> insertMeasurement(const f20::MeasureResult& result,
                                             const MeasurementRecord& record);

    bool saveBaseline(const QString& channelSerial, const QString& referenceMaterial,
                      const QDateTime& committedAtUtc);
    // Marks every stored baseline of the channel as no longer valid
    // (remote "baseline invalid": the fiber moved, ...).
    bool invalidateBaselines(const QString& channelSerial, const QDateTime& atUtc);
    // Commit time of the channel's newest baseline, unless it was invalidated.
    std::optional<QDateTime> lastValidBaseline(const QString& channelSerial);

    // Sample id of the measurement that saved this spectrum file.
    std::optional<QString> sampleIdForSpectrum(const QString& spectrumFile);

    QSqlDatabase& database() { return db_; }

private:
    bool exec(const QString& sql);
    bool migrate();
    bool fail(const QString& message);

    QSqlDatabase db_;
    bool isOpen_ = false;
    QString lastError_;
};
