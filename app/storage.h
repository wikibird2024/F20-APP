#pragma once
#include "f20/results.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <vector>

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
    QString reanalyzedFromResultId; // the stored result owning that file; "" if none
    std::optional<QDateTime> baselineCommittedAtUtc; // empty for a re-analysis
    QString appVersion;
    QString bridgeVersion;
};

// One stored result with its layers, as get_results sends it (spec 6.6.5.6).
struct StoredResult {
    QString resultId;
    QString timeUtc; // as stored: 2026-10-07T08:15:30.120Z
    QString recipeName;
    QString sampleId;
    QString operatorName;
    QString spectrumFile;
    QString reanalyzedFrom;
    QString reanalyzedFromResultId;
    std::optional<int> baselineAgeMinutes;
    f20::MeasureResult result; // layers, gof, passed (no summary)
};

struct ResultPage {
    std::vector<StoredResult> results;
    int total = 0; // results matching, over all pages
};

// SQLite storage, one row per measured layer (spec §6.5), plus the history
// of baseline commits that gives a baseline its true age after a restart.
class Storage {
public:
    // Kept in PRAGMA user_version. open() upgrades older files step by step
    // and refuses newer ones (QCoDeS: dataset/sqlite/db_upgrades).
    static constexpr int kSchemaVersion = 3;

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

    // Results measured at or after sinceUtc (all of them when it is
    // invalid), oldest first; `limit` results from `offset`. Rows from
    // schema 1 have no result id: each row counts as its own result.
    ResultPage resultsSince(const QDateTime& sinceUtc, int offset, int limit);
    // How many results were measured at or after sinceUtc.
    int countResultsSince(const QDateTime& sinceUtc);
    // nullopt: no such result; "" : the result has no saved spectrum.
    std::optional<QString> spectrumFileForResult(const QString& resultId);

    // Sample id of the measurement that saved this spectrum file.
    std::optional<QString> sampleIdForSpectrum(const QString& spectrumFile);
    // Result id of the measurement that saved this spectrum file.
    std::optional<QString> resultIdForSpectrum(const QString& spectrumFile);

    QSqlDatabase& database() { return db_; }

private:
    bool exec(const QString& sql);
    bool migrate();
    bool upgrade(int toVersion, const char* const* first, const char* const* last);
    bool fail(const QString& message);

    QSqlDatabase db_;
    bool isOpen_ = false;
    QString lastError_;
};
