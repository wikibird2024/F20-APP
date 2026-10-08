#pragma once
#include "appstatemachine.h"
#include "baselinetracker.h"
#include "bridgeclient.h"
#include "measuregate.h"
#include "recipelimits.h"
#include "storage.h"

#include <QDir>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QTimer>

#include <optional>

class BaselineWizard;
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
    // configPath: the f20.ini to use. Relative paths inside it (database,
    // recipes, spectra) resolve against the ini's folder, never against the
    // working folder - so a shortcut with another "Start in" folder still
    // finds the same database.
    explicit MainWindow(const QString& configPath, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool bridgeConnected() const;

private:
    enum class Trigger { operatorButton, autoCycle, remote };

    ServerLink* createServerLink();
    void updateServerState();
    void loadRecipes();
    void startMeasurement(Trigger trigger);
    void failMeasurement(const QString& operatorMessage);
    void startReanalysis(const QString& spectrumPath);
    void failReanalysis(const QString& operatorMessage);
    bool storeResult(const f20::MeasureResult& result, MeasurementRecord record);
    void runBaselineWizard();
    void onBridgeConnected();
    void enterDeviceFault(const QString& message);
    void restoreBaselineAge();
    void loadRecipeLimits();
    void applyRecipeLimits(const QString& recipe);
    void syncBaselineState();
    void invalidateBaseline(const QString& reason);
    void refreshDiagnostics();
    void updateMeasurePermission();
    void updateStatusBar();
    void publishStatus();
    std::optional<f20app::Refusal> measureRefusal() const;
    QString resolvePath(const QString& path) const;
    QString spectrumFilePath(const QString& sampleId) const;

    QSettings settings_;
    QDir configDir_;
    int measureTimeoutMs_ = 60000;
    BridgeClient bridge_;
    f20app::AppStateMachine state_;
    f20app::BaselineTracker baseline_;
    f20app::RecipeLimits recipeLimits_;
    QString limitsRecipe_; // recipe whose limits the tracker uses now
    Storage storage_;
    ServerLink* serverLink_ = nullptr;
    QPointer<BaselineWizard> baselineWizard_; // null when no wizard is open

    MeasureScreen* measureScreen_;
    HistoryScreen* historyScreen_;
    DiagnosticsScreen* diagnosticsScreen_;
    QLabel* stateLabel_;
    QLabel* baselineLabel_;
    QLabel* serverLabel_;
    QTimer autoCycleTimer_;
    QTimer ageTimer_;
    QString expectedSerial_; // [device] serial in f20.ini
    QString channelSerial_;  // what the bridge reports
    QString bridgeVersion_;
};
