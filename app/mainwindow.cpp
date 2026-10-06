#include "mainwindow.h"

#include "baselinewizard.h"
#include "diagnosticsscreen.h"
#include "f20/errors.h"
#include "historyscreen.h"
#include "measurescreen.h"
#include "serverlink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace {

QString operatorText(const f20::Reply& reply) {
    const auto code = f20::errorCodeFromString(reply.errorCode);
    const std::string msg = code
        ? f20::operatorMessage(*code, reply.errorMessage)
        : reply.errorMessage;
    return QString::fromStdString(msg);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      settings_(QDir::current().filePath("f20.ini"), QSettings::IniFormat) {
    setWindowTitle("F20 control");
    resize(1100, 720);

    serverLink_ = new NullServerLink(this); // MQTT impl replaces this (§10.2)

    baseline_.setThresholds(settings_.value("baseline/warnMinutes", 20).toInt(),
                            settings_.value("baseline/blockMinutes", 30).toInt());
    storage_.open(settings_.value("storage/database", "f20data.db").toString());

    // Screens
    auto* tabs = new QTabWidget;
    measureScreen_ = new MeasureScreen;
    historyScreen_ = new HistoryScreen(storage_);
    diagnosticsScreen_ = new DiagnosticsScreen;
    tabs->addTab(measureScreen_, "Measure");
    tabs->addTab(historyScreen_, "History / SPC");
    tabs->addTab(diagnosticsScreen_, "Diagnostics");
    setCentralWidget(tabs);

    auto* toolbar = addToolBar("main");
    toolbar->setMovable(false);
    toolbar->addAction("Baseline...", this, &MainWindow::runBaselineWizard);

    stateLabel_ = new QLabel("starting");
    baselineLabel_ = new QLabel("baseline: none");
    statusBar()->addWidget(stateLabel_);
    statusBar()->addPermanentWidget(baselineLabel_);

    loadRecipes();

    // State machine drives the UI and the server status (spec §8.1).
    state_.setChangeHandler([this](f20app::AppState, f20app::AppState) {
        updateMeasurePermission();
        updateStatusBar();
        publishStatus();
    });

    // Bridge wiring
    connect(&bridge_, &BridgeClient::connected, this, [this] {
        state_.onBridgeUp();
        bridge_.send("getStatus", {}, [this](const f20::Reply& reply) {
            if (reply.ok && reply.result.value("baselineValid", false)) {
                baseline_.committed(); // recovered baseline counts as fresh
                state_.onBaselineValid();
            }
            channelSerial_ = QString::fromStdString(
                reply.result.value("channelSerial", ""));
            updateStatusBar();
        });
        refreshDiagnostics();
    });
    connect(&bridge_, &BridgeClient::disconnected, this,
            [this] { state_.onBridgeDown(); });
    connect(&bridge_, &BridgeClient::reconnectExhausted, this, [this] {
        serverLink_->publishAlarm("bridgeFault",
                                  {{"message", "bridge restart failed 3x"}});
    });
    connect(&bridge_, &BridgeClient::protocolLog, diagnosticsScreen_,
            &DiagnosticsScreen::appendLog);
    connect(&bridge_, &BridgeClient::eventReceived, this,
            [this](const QString& name, const f20::json&) {
                diagnosticsScreen_->appendLog("[event] " + name);
            });

    // Measure screen wiring
    connect(measureScreen_, &MeasureScreen::measureRequested, this,
            [this] { startMeasurement("operator"); });
    connect(measureScreen_, &MeasureScreen::autoCycleToggled, this,
            [this](bool enabled, int seconds) {
                if (enabled)
                    autoCycleTimer_.start(seconds * 1000);
                else
                    autoCycleTimer_.stop();
            });
    connect(&autoCycleTimer_, &QTimer::timeout, this,
            [this] { startMeasurement("auto-cycle"); });

    // History wiring: re-analysis never overwrites the original (spec §7.3).
    connect(historyScreen_, &HistoryScreen::reanalyzeRequested, this,
            [this](const QString& path) {
                bridge_.send("openSpectrum", {{"path", path.toStdString()}},
                             [this, path](const f20::Reply& openReply) {
                    if (!openReply.ok)
                        return;
                    bridge_.send("analyzeSpectrum", {},
                                 [this, path](const f20::Reply& reply) {
                        if (!reply.ok)
                            return;
                        if (const auto result =
                                f20::measureResultFromJson(reply.result)) {
                            storage_.insertMeasurement(
                                *result, measureScreen_->currentRecipe(),
                                channelSerial_, measureScreen_->operatorName(),
                                measureScreen_->sampleId(), path,
                                /*reanalyzedFrom=*/path);
                            historyScreen_->refresh();
                        }
                    });
                });
            });

    // Diagnostics wiring
    connect(diagnosticsScreen_, &DiagnosticsScreen::refreshRequested, this,
            &MainWindow::refreshDiagnostics);
    connect(diagnosticsScreen_, &DiagnosticsScreen::restartBridgeRequested,
            this, [this] {
                bridge_.disconnectFromBridge();
                bridge_.connectToBridge(
                    settings_.value("bridge/host", "127.0.0.1").toString(),
                    static_cast<quint16>(
                        settings_.value("bridge/port", 5555).toUInt()));
            });

    // Remote commands (MQTT later; same path as the operator's button).
    connect(serverLink_, &ServerLink::remoteMeasureRequested, this,
            [this](const QString&, const QString&) { startMeasurement("remote"); });
    connect(serverLink_, &ServerLink::remoteBaselineInvalidate, this, [this] {
        baseline_.invalidate();
        state_.onBaselineInvalid();
    });

    // Baseline age watchdog (spec §7.2): stale blocks measuring + alarm.
    ageTimer_.start(5000);
    connect(&ageTimer_, &QTimer::timeout, this, [this] {
        if (baseline_.status() == f20app::BaselineStatus::Stale &&
            state_.state() == f20app::AppState::Ready) {
            state_.onBaselineInvalid();
            serverLink_->publishAlarm("baselineStale", {});
        }
        updateMeasurePermission();
        updateStatusBar();
    });

    updateMeasurePermission();
    updateStatusBar();

    bridge_.connectToBridge(
        settings_.value("bridge/host", "127.0.0.1").toString(),
        static_cast<quint16>(settings_.value("bridge/port", 5555).toUInt()));
    historyScreen_->refresh();
}

MainWindow::~MainWindow() {
    // Members are destroyed in reverse declaration order: state_ dies before
    // bridge_. The socket teardown would otherwise emit disconnected into
    // lambdas that touch already-destroyed members - so unhook everything
    // from bridge_ first.
    bridge_.disconnect();
    autoCycleTimer_.disconnect();
    ageTimer_.disconnect();
}

bool MainWindow::bridgeConnected() const { return bridge_.isConnected(); }

void MainWindow::loadRecipes() {
    // Recipe names come from the FILMeasure recipes folder (spec §7.1);
    // in development a plain folder with dummy files mirrors the sim names.
    const QString folder = settings_.value("recipes/folder", "recipes").toString();
    QStringList names;
    for (const QFileInfo& info :
         QDir(folder).entryInfoList({"*.fmrcp"}, QDir::Files, QDir::Name))
        names << info.completeBaseName();
    if (names.isEmpty())
        names << "SiO2 on Si"; // sensible default against the simulator
    measureScreen_->setRecipes(names);
}

QString MainWindow::spectrumFilePath(const QString& sampleId) const {
    const QDate today = QDate::currentDate();
    QDir dir(QString("spectra/%1").arg(today.toString("yyyy-MM-dd")));
    QDir().mkpath(dir.path());
    const QString time = QTime::currentTime().toString("HHmmss");
    const QString sample = sampleId.isEmpty() ? "sample" : sampleId;
    return dir.filePath(time + "_" + sample + ".csv");
}

void MainWindow::startMeasurement(const QString& reason) {
    if (!state_.canMeasure()) {
        measureScreen_->showError("Cannot measure now (" +
                                  QString(f20app::toString(state_.state())) + ")");
        return;
    }
    state_.onMeasureStarted();
    diagnosticsScreen_->appendLog("[measure] trigger: " + reason);

    const QString recipe = measureScreen_->currentRecipe();
    bridge_.send("setRecipe", {{"name", recipe.toStdString()}},
                 [this, recipe](const f20::Reply& recipeReply) {
        if (!recipeReply.ok) {
            state_.onMeasureFinished();
            measureScreen_->showError(operatorText(recipeReply));
            return;
        }
        bridge_.send("measure", {{"addToHistory", false}},
                     [this, recipe](const f20::Reply& reply) {
            state_.onMeasureFinished();
            if (!reply.ok) {
                measureScreen_->showError(operatorText(reply));
                return;
            }
            const auto result = f20::measureResultFromJson(reply.result);
            if (!result) {
                measureScreen_->showError("Malformed result from bridge");
                return;
            }
            measureScreen_->showResult(*result);
            serverLink_->publishResult(reply.result);
            if (!result->passed)
                serverLink_->publishAlarm("fail", reply.result);

            // Spectrum: show it and store it beside the database row (§10.1).
            const QString file = spectrumFilePath(measureScreen_->sampleId());
            bridge_.send("acquireSpectrum", {},
                         [this, result, file, recipe](const f20::Reply& spectrumReply) {
                if (spectrumReply.ok) {
                    if (const auto spectrum =
                            f20::spectrumFromJson(spectrumReply.result))
                        measureScreen_->showSpectrum(*spectrum);
                    bridge_.send("saveSpectrum", {{"path", file.toStdString()}},
                                 [](const f20::Reply&) {});
                }
                storage_.insertMeasurement(*result, recipe, channelSerial_,
                                           measureScreen_->operatorName(),
                                           measureScreen_->sampleId(), file);
                historyScreen_->refresh();
            });
        });
    });
}

void MainWindow::runBaselineWizard() {
    auto* wizard = new BaselineWizard(bridge_, this);
    connect(wizard, &BaselineWizard::baselineCommitted, this, [this] {
        baseline_.committed();
        state_.onBaselineValid();
        updateMeasurePermission();
        updateStatusBar();
    });
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->open();
}

void MainWindow::refreshDiagnostics() {
    diagnosticsScreen_->setBridgeState(
        bridge_.isConnected() ? "connected" : "disconnected",
        bridge_.isConnected());
    bridge_.send("getVersion", {}, [this](const f20::Reply& reply) {
        if (reply.ok)
            diagnosticsScreen_->setVersions(QString::fromStdString(
                reply.result.value("bridge", "") + " / " +
                reply.result.value("filmeasure", "")));
    });
    bridge_.send("getDiagnostics", {}, [this](const f20::Reply& reply) {
        if (reply.ok)
            diagnosticsScreen_->setSignalHealth(
                reply.result.value("referenceCounts", 0),
                reply.result.value("backgroundCounts", 0));
    });
}

void MainWindow::updateMeasurePermission() {
    QString reason;
    if (!state_.canMeasure()) {
        switch (state_.state()) {
        case f20app::AppState::Starting:   reason = "Connecting to the bridge..."; break;
        case f20app::AppState::NoBaseline: reason = "Run the baseline first"; break;
        case f20app::AppState::Measuring:  reason = "Measurement running..."; break;
        case f20app::AppState::Fault:      reason = "Bridge fault - check Diagnostics"; break;
        default: break;
        }
    } else if (baseline_.status() == f20app::BaselineStatus::Stale) {
        reason = "Baseline too old - redo the baseline";
    }
    measureScreen_->setMeasureAllowed(reason.isEmpty(), reason);
}

void MainWindow::updateStatusBar() {
    stateLabel_->setText(QString("state: %1").arg(f20app::toString(state_.state())));

    QString text = "baseline: none";
    QString color = "#c62828";
    if (const auto age = baseline_.ageMinutes()) {
        text = QString("baseline: %1 min ago").arg(*age);
        switch (baseline_.status()) {
        case f20app::BaselineStatus::Fresh: color = "#1a7f37"; break;
        case f20app::BaselineStatus::Aging: color = "#b05000"; break;
        default:                            color = "#c62828"; break;
        }
    }
    baselineLabel_->setText(text);
    baselineLabel_->setStyleSheet("color: " + color + ";");
}

void MainWindow::publishStatus() {
    f20::json status{{"state", f20app::toString(state_.state())},
                     {"baselineValid",
                      baseline_.status() == f20app::BaselineStatus::Fresh ||
                          baseline_.status() == f20app::BaselineStatus::Aging},
                     {"channelSerial", channelSerial_.toStdString()}};
    if (const auto age = baseline_.ageMinutes())
        status["baselineAgeMinutes"] = *age;
    serverLink_->publishStatus(status);
}
