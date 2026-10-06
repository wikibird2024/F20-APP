#pragma once
#include "appstatemachine.h"
#include "baselinetracker.h"
#include "bridgeclient.h"
#include "storage.h"

#include <QMainWindow>
#include <QSettings>
#include <QTimer>

class MeasureScreen;
class HistoryScreen;
class DiagnosticsScreen;
class ServerLink;
class QLabel;

// Wires everything together: screens, bridge client, state machine,
// baseline tracker, storage, server link. All decisions ("may we measure?")
// go through f20app::AppStateMachine - nothing else keeps state.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool bridgeConnected() const;

private:
    void loadRecipes();
    void startMeasurement(const QString& reason);
    void runBaselineWizard();
    void refreshDiagnostics();
    void updateMeasurePermission();
    void updateStatusBar();
    void publishStatus();
    QString spectrumFilePath(const QString& sampleId) const;

    QSettings settings_;
    BridgeClient bridge_;
    f20app::AppStateMachine state_;
    f20app::BaselineTracker baseline_;
    Storage storage_;
    ServerLink* serverLink_;

    MeasureScreen* measureScreen_;
    HistoryScreen* historyScreen_;
    DiagnosticsScreen* diagnosticsScreen_;
    QLabel* stateLabel_;
    QLabel* baselineLabel_;
    QTimer autoCycleTimer_;
    QTimer ageTimer_;
    QString channelSerial_;
};
