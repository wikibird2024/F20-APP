// Storage on a temporary file: schema version + WAL, migration from the
// unversioned (v1) file, all-or-nothing results, baseline history.
#include "storage.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>

namespace {

QVariant scalar(QSqlDatabase& db, const QString& sql) {
    QSqlQuery query(db);
    if (!query.exec(sql) || !query.next())
        return {};
    return query.value(0);
}

f20::MeasureResult twoLayers(double secondThicknessNm) {
    f20::MeasureResult result;
    f20::LayerResult layer;
    layer.layer = 1;
    layer.thicknessNm = 512.3;
    result.layers.push_back(layer);
    layer.layer = 2;
    layer.thicknessNm = secondThicknessNm;
    result.layers.push_back(layer);
    result.gof = 0.98;
    result.passed = true;
    return result;
}

MeasurementRecord minimalRecord() {
    MeasurementRecord record;
    record.recipeName = "SiO2 on Si"; // NOT NULL column (a null QString binds as NULL)
    return record;
}

} // namespace

class StorageTest : public QObject {
    Q_OBJECT

private slots:
    void newFileGetsTheCurrentSchemaInWalMode() {
        QTemporaryDir dir;
        Storage storage;
        QVERIFY(storage.open(dir.filePath("f20.db")));
        QCOMPARE(scalar(storage.database(), "PRAGMA user_version").toInt(),
                 Storage::kSchemaVersion);
        QCOMPARE(scalar(storage.database(), "PRAGMA journal_mode").toString(), QString("wal"));
    }

    void unversionedFileIsUpgradedAndKeepsItsRows() {
        QTemporaryDir dir;
        const QString path = dir.filePath("old.db");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", "legacy");
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE measurements (id INTEGER PRIMARY KEY AUTOINCREMENT,"
                               " time TEXT NOT NULL, recipeName TEXT NOT NULL, channelSerial TEXT,"
                               " operatorName TEXT, sampleId TEXT, layerNumber INTEGER NOT NULL,"
                               " thicknessNm REAL NOT NULL, n REAL, k REAL, roughnessNm REAL,"
                               " gof REAL NOT NULL, passed INTEGER NOT NULL, spectrumFile TEXT,"
                               " reanalyzedFrom TEXT)"));
            QVERIFY(query.exec("INSERT INTO measurements (time, recipeName, layerNumber,"
                               " thicknessNm, gof, passed) VALUES"
                               " ('2026-10-01T10:00:00', 'SiO2 on Si', 1, 500.0, 0.99, 1)"));
            db.close();
        }
        QSqlDatabase::removeDatabase("legacy");

        Storage storage;
        QVERIFY(storage.open(path));
        QCOMPARE(scalar(storage.database(), "PRAGMA user_version").toInt(), 2);
        QCOMPARE(scalar(storage.database(), "SELECT COUNT(*) FROM measurements").toInt(), 1);
        QVERIFY(scalar(storage.database(), "SELECT resultId FROM measurements").isNull());
        QVERIFY(storage.insertMeasurement(twoLayers(80.0), minimalRecord()).has_value());
    }

    void newerSchemaIsRefused() {
        QTemporaryDir dir;
        const QString path = dir.filePath("f20.db");
        {
            Storage storage;
            QVERIFY(storage.open(path));
            QSqlQuery query(storage.database());
            QVERIFY(query.exec("PRAGMA user_version = 99"));
        }
        Storage newer;
        QVERIFY(!newer.open(path));
        QVERIFY(!newer.isOpen());
        QVERIFY(newer.lastError().contains("newer"));
    }

    void layersOfOneResultShareAnIdAndTheSnapshot() {
        QTemporaryDir dir;
        Storage storage;
        QVERIFY(storage.open(dir.filePath("f20.db")));
        MeasurementRecord record;
        record.recipeName = "SiO2 on Si";
        record.appVersion = "0.1.0";
        record.baselineCommittedAtUtc = QDateTime::currentDateTimeUtc().addSecs(-12 * 60);
        const auto resultId = storage.insertMeasurement(twoLayers(80.0), record);
        QVERIFY(resultId.has_value());
        QCOMPARE(scalar(storage.database(),
                        "SELECT COUNT(*) FROM measurements WHERE resultId = '" + *resultId + "'")
                     .toInt(),
                 2);
        QCOMPARE(scalar(storage.database(), "SELECT MAX(baselineAgeMinutes) FROM measurements")
                     .toInt(),
                 12);
        QVERIFY(scalar(storage.database(), "SELECT time FROM measurements").toString().endsWith('Z'));
        // No spectrum saved: NULL, never a path to a file that isn't there.
        QVERIFY(scalar(storage.database(), "SELECT spectrumFile FROM measurements").isNull());
    }

    void aFailedLayerRollsBackTheWholeResult() {
        QTemporaryDir dir;
        Storage storage;
        QVERIFY(storage.open(dir.filePath("f20.db")));
        // SQLite stores NaN as NULL, so layer 2 breaks "thicknessNm NOT NULL".
        const auto resultId = storage.insertMeasurement(
            twoLayers(std::numeric_limits<double>::quiet_NaN()), minimalRecord());
        QVERIFY(!resultId.has_value());
        QVERIFY(storage.lastError().contains("layer 2"));
        QCOMPARE(scalar(storage.database(), "SELECT COUNT(*) FROM measurements").toInt(), 0);
        // and the connection is usable afterwards
        QVERIFY(storage.insertMeasurement(twoLayers(80.0), minimalRecord()).has_value());
    }

    void baselineHistoryGivesTheAgeBackUntilInvalidated() {
        QTemporaryDir dir;
        Storage storage;
        QVERIFY(storage.open(dir.filePath("f20.db")));
        QVERIFY(!storage.lastValidBaseline("F20:SIM001").has_value());

        const QDateTime committedAt = QDateTime::currentDateTimeUtc().addSecs(-25 * 60);
        QVERIFY(storage.saveBaseline("F20:SIM001", "Si", committedAt));
        QCOMPARE(storage.lastValidBaseline("F20:SIM001").value().toMSecsSinceEpoch(),
                 committedAt.toMSecsSinceEpoch());
        QVERIFY(!storage.lastValidBaseline("F20:OTHER").has_value());

        QVERIFY(storage.invalidateBaselines("F20:SIM001", QDateTime::currentDateTimeUtc()));
        QVERIFY(!storage.lastValidBaseline("F20:SIM001").has_value());

        const QDateTime next = QDateTime::currentDateTimeUtc();
        QVERIFY(storage.saveBaseline("F20:SIM001", "Si", next));
        QCOMPARE(storage.lastValidBaseline("F20:SIM001").value().toMSecsSinceEpoch(),
                 next.toMSecsSinceEpoch());
    }
};

int runStorageTests(int argc, char** argv) {
    StorageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_storage.moc"
