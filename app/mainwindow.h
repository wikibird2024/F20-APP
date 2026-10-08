#pragma once
#include "appsettings.h"
#include "appstatemachine.h"
#include "baselinetracker.h"
#include "bridgeclient.h"
#include "measuregate.h"
#include "recipelimits.h"
#include "storage.h"
#include "warmup.h"

#include <QDir>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <optional>

class BaselineWizard;
class QAction;
class QMessageBox;
class MeasureScreen;
class HistoryScreen;
class DiagnosticsScreen;
class ServerLink;
class QLabel;

// Wires everything together: screens, bridge client, state machine,
// baseline tracker, storage, server link. All decisions ("may we measure?")
// go through f20app::AppStateMachine and f20app::checkMeasure() - nothing
// else keeps state.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // configPath: the f20.ini to use (the defaults). changesPath: the
    // Settings screen's file, whose values win (spec §6.7). Relative paths
    // (database, recipes, spectra) resolve against f20.ini's folder, never
    // against the working folder - so a shortcut with another "Start in"
    // folder still finds the same database.
    MainWindow(const QString& configPath, const QString& changesPath, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool bridgeConnected() const;
    // True after the Settings screen asked for a restart and the window
    // closed for it; main() then starts the app again.
    bool restartRequested() const { return restartRequested_; }

private:
    enum class Trigger { operatorButton, autoCycle, remote };

    ServerLink* createServerLink();
    void updateServerState();
    void loadRecipes();
    // transactionId, recipe and sampleId come with a server request; empty
    // recipe or sample = the one on the Measure screen.
    void startMeasurement(Trigger trigger, const QString& transactionId = {},
                          const QString& recipe = {}, const QString& sampleId = {});
    void failMeasurement(const QString& operatorMessage, const QString& transactionId = {},
                         const QString& errorCode = {});
    void startReanalysis(const QString& spectrumPath);
    void failReanalysis(const QString& operatorMessage);
    std::optional<QString> storeResult(const f20::MeasureResult& result, MeasurementRecord record);
    void publishResultToServer(const f20::MeasureResult& result, const MeasurementRecord& record,
                               const QString& resultId, const QString& transactionId);
    void answerBaselineInvalidate(const QString& transactionId, const QString& reason);
    void answerResults(const QString& transactionId, const QString& sinceUtc, int page);
    void answerSpectrum(const QString& transactionId, const QString& resultId);
    void runBaselineWizard();
    void skipWarmUp();
    void updateWarmUp();
    void logEvent(const QString& line);
    void onBridgeConnected();
    void enterDeviceFault(const QString& message);
    void closeBaselineDialogs();
    void onBridgeEvent(const QString& name, const f20::json& data);
    void restartApp();
    void restoreBaselineAge();
    void offerBaselineRecovery();
    void recoverBaseline();
    void loadRecipeLimits();
    void applyRecipeLimits(const QString& recipe);
    void syncBaselineState();
    void askBaselineInvalid();
    void invalidateBaseline(const QString& reason);
    void refreshDiagnostics();
    void updateMeasurePermission();
    void updateStatusBar();
    void publishStatus();
    std::optional<f20app::Refusal> measureRefusal() const;
    QString resolvePath(const QString& path) const;
    QString spectrumFilePath(const QString& sampleId) const;

    QSettings settings_;     // f20.ini: settings the Settings screen does not show
    AppSettings appSettings_; // the ones it shows, changes included
    QDir configDir_;
    int measureTimeoutMs_ = 60000;
    BridgeClient bridge_;
    f20app::AppStateMachine state_;
    f20app::BaselineTracker baseline_;
    f20app::RecipeLimits recipeLimits_;
    f20app::WarmUpTimer warmUp_;
    bool warmUpWasDone_ = false;
    QString limitsRecipe_; // recipe whose limits the tracker uses now
    Storage storage_;
    ServerLink* serverLink_ = nullptr;
    QPointer<BaselineWizard> baselineWizard_; // null when no wizard is open
    QPointer<QMessageBox> recoverPrompt_;     // null when no recovery offer is open
    std::optional<QDateTime> recoverableCommit_; // our commit FILMeasure lost, still in limits

    MeasureScreen* measureScreen_;
    HistoryScreen* historyScreen_;
    DiagnosticsScreen* diagnosticsScreen_;
    QLabel* stateLabel_;
    QLabel* baselineLabel_;
    QLabel* warmUpLabel_;
    QAction* baselineAction_;
    QAction* skipWarmUpAction_;
    QAction* baselineInvalidAction_;
    QLabel* serverLabel_;
    QTimer autoCycleTimer_;
    QTimer ageTimer_;
    QString expectedSerial_; // [device] serial in f20.ini
    QString channelSerial_;  // what the bridge reports
    QString bridgeVersion_;     // "bridge / FILMeasure", stored with each result
    QString bridgeOnlyVersion_; // for the server status
    QString filmeasureVersion_;
    int resultsPageSize_ = 50;  // get_results page size
    bool restartRequested_ = false;
};
