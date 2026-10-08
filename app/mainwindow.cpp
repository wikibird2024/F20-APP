#include "mainwindow.h"

#include "baselinewizard.h"
#include "devicecheck.h"
#include "diagnosticsscreen.h"
#include "f20/errors.h"
#include "f20/jsonread.h"
#include "filenames.h"
#include "historyscreen.h"
#include "measurescreen.h"
#include "mqttserverlink.h"
#include "serverlink.h"

#ifdef F20_HAS_MQTT
#include "pahomqtttransport.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLabel>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

namespace {

// recipeName fills the recipeNotFound sentence. The bridge's own message
// already contains the name, so passing it would double it ("Recipe
// 'Recipe 'X' not found' not found").
QString operatorText(const f20::Reply& reply, const QString& recipeName = {}) {
    const auto code = f20::errorCodeFromString(reply.errorCode);
    if (!code)
        return QString::fromStdString(reply.errorMessage);
    const std::string detail = *code == f20::ErrorCode::recipeNotFound
                                   ? recipeName.toStdString()
                                   : reply.errorMessage;
    return QString::fromStdString(f20::operatorMessage(*code, detail));
}

f20app::BaselineTracker::Clock::time_point toTimePoint(const QDateTime& time) {
    using Clock = f20app::BaselineTracker::Clock;
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(
        std::chrono::milliseconds(time.toMSecsSinceEpoch())));
}

// Same colors as the baseline label.
QString colorFor(ServerConnection state) {
    switch (state) {
    case ServerConnection::connected:  return "#1a7f37";
    case ServerConnection::connecting: return "#b05000";
    case ServerConnection::lost:       return "#c62828";
    case ServerConnection::off:        break;
    }
    return "#6e7781";
}

QDateTime toDateTimeUtc(f20app::BaselineTracker::Clock::time_point time) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch());
    return QDateTime::fromMSecsSinceEpoch(ms.count(), Qt::UTC);
}

} // namespace

MainWindow::MainWindow(const QString& configPath, QWidget* parent)
    : QMainWindow(parent),
      settings_(configPath, QSettings::IniFormat),
      configDir_(QFileInfo(configPath).absoluteDir()) {
    setWindowTitle("F20 control");
    resize(1100, 720);

    if (!QFileInfo::exists(configPath))
        qWarning().noquote() << "config file not found:" << configPath << "- using defaults";

    baseline_.setThresholds(settings_.value("baseline/warnMinutes", 20).toInt(),
                            settings_.value("baseline/blockMinutes", 30).toInt());
    BridgeClient::Timing timing;
    timing.requestTimeoutMs =
        settings_.value("bridge/timeoutMs", timing.requestTimeoutMs).toInt();
    bridge_.setTiming(timing);
    // Acquire and analyze can take much longer than a status request.
    measureTimeoutMs_ = settings_.value("bridge/measureTimeoutMs", 60000).toInt();
    expectedSerial_ = settings_.value("device/serial").toString().trimmed();

    const QString databasePath =
        resolvePath(settings_.value("storage/database", "f20data.db").toString());
    if (!storage_.open(databasePath))
        qCritical().noquote() << "database not available - measuring is blocked:"
                              << storage_.lastError();

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
    serverLabel_ = new QLabel;
    baselineLabel_ = new QLabel("baseline: none");
    statusBar()->addWidget(stateLabel_);
    statusBar()->addPermanentWidget(serverLabel_);
    statusBar()->addPermanentWidget(baselineLabel_);

    loadRecipes();

    // Server link (spec §6.6). Wired before start(), so no log line or
    // state change is missed.
    serverLink_ = createServerLink();
    connect(serverLink_, &ServerLink::connectionChanged, this, &MainWindow::updateServerState);
    connect(serverLink_, &ServerLink::logLine, this, [this](const QString& line) {
        diagnosticsScreen_->appendLog(line);
        qInfo().noquote() << "[server]" << line;
    });
    diagnosticsScreen_->setServerDetails(serverLink_->connectionDetails());
    updateServerState();
    serverLink_->start();

    // State machine drives the UI and the server status (spec §7).
    state_.setChangeHandler([this](f20app::AppState, f20app::AppState to) {
        if (to == f20app::AppState::Fault)
            measureScreen_->stopAutoCycle();
        updateMeasurePermission();
        updateStatusBar();
        publishStatus();
    });

    // Bridge wiring
    connect(&bridge_, &BridgeClient::connected, this, &MainWindow::onBridgeConnected);
    connect(&bridge_, &BridgeClient::disconnected, this, [this] {
        // Close an open baseline wizard first, while the state is still
        // Baselining: its finished handler then keeps a commit that already
        // happened. Closed after the drop, the state would be Fault and the
        // commit ignored - the app would stay in NoBaseline.
        if (baselineWizard_)
            baselineWizard_->reject();
        // Forget the age: on reconnect it comes back from the bridge
        // ("baseline still valid?") and the database ("since when?").
        baseline_.invalidate();
        state_.onBridgeDown();
        diagnosticsScreen_->setBridgeState("disconnected", false);
        updateStatusBar();
    });
    connect(&bridge_, &BridgeClient::reconnectExhausted, this, [this] {
        state_.onBridgeDown(); // also leaves Starting when the bridge never came up
        measureScreen_->showError("Bridge not reachable - check Diagnostics");
        serverLink_->publishAlarm("bridgeFault",
                                  {{"message", "bridge not reachable after 3 retries"}});
    });
    connect(&bridge_, &BridgeClient::protocolLog, this, [this](const QString& line) {
        diagnosticsScreen_->appendLog(line);
        qInfo().noquote() << "[bridge]" << line; // spec §7: every command and reply in the log file
    });
    connect(&bridge_, &BridgeClient::eventReceived, this,
            [this](const QString& name, const f20::json&) {
                diagnosticsScreen_->appendLog("[event] " + name);
            });

    // Measure screen wiring
    connect(measureScreen_, &MeasureScreen::measureRequested, this,
            [this] { startMeasurement(Trigger::operatorButton); });
    connect(measureScreen_, &MeasureScreen::autoCycleToggled, this,
            [this](bool enabled, int seconds) {
                if (enabled)
                    autoCycleTimer_.start(seconds * 1000);
                else
                    autoCycleTimer_.stop();
            });
    connect(&autoCycleTimer_, &QTimer::timeout, this,
            [this] { startMeasurement(Trigger::autoCycle); });

    // History wiring: re-analysis never overwrites the original (spec §6.3).
    connect(historyScreen_, &HistoryScreen::reanalyzeRequested, this,
            &MainWindow::startReanalysis);

    // Diagnostics wiring
    connect(diagnosticsScreen_, &DiagnosticsScreen::refreshRequested, this,
            &MainWindow::refreshDiagnostics);
    connect(diagnosticsScreen_, &DiagnosticsScreen::restartBridgeRequested,
            this, [this] {
                bridge_.connectToBridge(
                    settings_.value("bridge/host", "127.0.0.1").toString(),
                    static_cast<quint16>(settings_.value("bridge/port", 5555).toUInt()));
            });

    // Remote commands (MQTT later; same path as the operator's button).
    connect(serverLink_, &ServerLink::remoteMeasureRequested, this,
            [this](const QString&, const QString&) { startMeasurement(Trigger::remote); });
    connect(serverLink_, &ServerLink::remoteBaselineInvalidate, this,
            [this] { invalidateBaseline("remote command"); });

    // Baseline age watchdog (spec §6.2): stale blocks measuring + alarm.
    ageTimer_.start(5000);
    connect(&ageTimer_, &QTimer::timeout, this, [this] {
        checkBaselineAge();
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

// MQTT when f20.ini names a broker and the build has Paho; otherwise the
// logging stand-in, and the app works exactly as without a server.
ServerLink* MainWindow::createServerLink() {
    const QString broker = settings_.value("mqtt/broker").toString().trimmed();
    if (broker.isEmpty())
        return new NullServerLink(this);
#ifdef F20_HAS_MQTT
    MqttServerLink::Settings link;
    link.serial = settings_.value("device/serial").toString().trimmed();
    if (link.serial.isEmpty())
        qWarning().noquote() << "[device] serial is empty in f20.ini - MQTT topics will be wrong";
    link.broker.host = broker;
    link.broker.port = static_cast<quint16>(settings_.value("mqtt/port", 1883).toUInt());
    link.broker.username = settings_.value("mqtt/username").toString();
    link.broker.password = settings_.value("mqtt/password").toString();
    link.broker.keepAliveSeconds =
        settings_.value("mqtt/keepAliveSeconds", link.broker.keepAliveSeconds).toInt();
    return new MqttServerLink(new PahoMqttTransport, link, this);
#else
    qWarning().noquote() << "f20.ini names an MQTT broker, but this build has no MQTT"
                            " (Eclipse Paho not found) - server messages are only logged";
    return new NullServerLink(this);
#endif
}

void MainWindow::updateServerState() {
    const ServerConnection state = serverLink_->connectionState();
    serverLabel_->setText("server: " + toString(state));
    serverLabel_->setStyleSheet("color: " + colorFor(state) + ";");
    diagnosticsScreen_->setServerState(toString(state), colorFor(state));
}

bool MainWindow::bridgeConnected() const { return bridge_.isConnected(); }

QString MainWindow::resolvePath(const QString& path) const {
    return QDir::isAbsolutePath(path) ? path : configDir_.absoluteFilePath(path);
}

void MainWindow::loadRecipes() {
    // Recipe names come from the FILMeasure recipes folder (spec §6.1);
    // in development a plain folder with dummy files mirrors the sim names.
    const QString folder = resolvePath(settings_.value("recipes/folder", "recipes").toString());
    QStringList names;
    for (const QFileInfo& info :
         QDir(folder).entryInfoList({"*.fmrcp"}, QDir::Files, QDir::Name))
        names << info.completeBaseName();
    if (names.isEmpty())
        names << "SiO2 on Si"; // sensible default against the simulator
    measureScreen_->setRecipes(names);
}

// Absolute, because the bridge is another process with its own working
// folder. Milliseconds in the name: two results in one second don't collide.
QString MainWindow::spectrumFilePath(const QString& sampleId) const {
    const QDateTime now = QDateTime::currentDateTime();
    const QDir dir(resolvePath("spectra/" + now.toString("yyyy-MM-dd")));
    QDir().mkpath(dir.absolutePath());
    const QString sample =
        QString::fromStdString(f20app::safeFileNamePart(sampleId.toStdString()));
    return dir.absoluteFilePath(now.toString("HHmmss_zzz") + "_" + sample + ".csv");
}

std::optional<f20app::Refusal> MainWindow::measureRefusal() const {
    return f20app::checkMeasure(state_.state(), baseline_.status(), storage_.isOpen());
}

void MainWindow::startMeasurement(Trigger trigger) {
    checkBaselineAge(); // right now, not at the next timer tick
    if (const auto refusal = measureRefusal()) {
        const QString text = f20app::operatorText(*refusal);
        if (trigger == Trigger::autoCycle) {
            // Logged, not shown: every N seconds it would cover a real error.
            diagnosticsScreen_->appendLog("[auto-cycle] skipped: " + text);
            return;
        }
        measureScreen_->showError(text);
        return;
    }
    state_.onMeasureStarted();
    const char* triggerText = trigger == Trigger::operatorButton ? "operator"
                              : trigger == Trigger::autoCycle    ? "auto-cycle"
                                                                 : "remote";
    diagnosticsScreen_->appendLog(QString("[measure] trigger: ") + triggerText);

    const QString recipe = measureScreen_->currentRecipe();
    MeasurementRecord record;
    record.recipeName = recipe;
    record.operatorName = measureScreen_->operatorName();
    record.sampleId = measureScreen_->sampleId();
    if (const auto committedAt = baseline_.committedAt()) // the baseline this result uses
        record.baselineCommittedAtUtc = toDateTimeUtc(*committedAt);

    // One spectrum feeds the result, the chart and the saved file:
    // acquire -> analyze -> save. A separate acquire after `measure` would
    // save a second, different spectrum next to the result.
    bridge_.send("setRecipe", {{"name", recipe.toStdString()}}, this,
                 [this, record](const f20::Reply& recipeReply) {
        if (!recipeReply.ok) {
            failMeasurement(operatorText(recipeReply, record.recipeName));
            return;
        }
        bridge_.send("acquireSpectrum", {}, this,
                     [this, record](const f20::Reply& spectrumReply) {
            if (!spectrumReply.ok) {
                failMeasurement(operatorText(spectrumReply));
                return;
            }
            const auto spectrum = f20::spectrumFromJson(spectrumReply.result);
            if (!spectrum) {
                failMeasurement("Malformed spectrum from bridge");
                return;
            }
            measureScreen_->showSpectrum(*spectrum);
            bridge_.send("analyzeSpectrum", {}, this,
                         [this, record](const f20::Reply& analyzeReply) {
                if (!analyzeReply.ok) {
                    failMeasurement(operatorText(analyzeReply));
                    return;
                }
                const auto result = f20::measureResultFromJson(analyzeReply.result);
                if (!result) {
                    failMeasurement("Malformed result from bridge");
                    return;
                }
                const QString file = spectrumFilePath(record.sampleId);
                const f20::json resultJson = analyzeReply.result;
                bridge_.send("saveSpectrum", {{"path", file.toStdString()}}, this,
                             [this, record, result, file, resultJson](
                                 const f20::Reply& saveReply) mutable {
                    if (saveReply.ok)
                        record.spectrumFile = file; // only a file that exists
                    else
                        diagnosticsScreen_->appendLog("[measure] spectrum NOT saved: " +
                                                      operatorText(saveReply));
                    state_.onMeasureFinished();
                    measureScreen_->showResult(*result);
                    if (!storeResult(*result, record))
                        measureScreen_->showError("Result NOT saved to the database - see log");
                    serverLink_->publishResult(resultJson);
                    if (!result->passed)
                        serverLink_->publishAlarm("fail", resultJson);
                }, measureTimeoutMs_);
            }, measureTimeoutMs_);
        }, measureTimeoutMs_);
    });
}

void MainWindow::failMeasurement(const QString& operatorMessage) {
    state_.onMeasureFinished(); // ignored if the bridge drop already made it Fault
    measureScreen_->showError(operatorMessage);
}

// The snapshot fields every stored result carries; returns false (and
// raises the alarm) when the database refused the result.
bool MainWindow::storeResult(const f20::MeasureResult& result, MeasurementRecord record) {
    record.channelSerial = channelSerial_;
    record.appVersion = QCoreApplication::applicationVersion();
    record.bridgeVersion = bridgeVersion_;
    const bool stored = storage_.insertMeasurement(result, record).has_value();
    if (!stored)
        serverLink_->publishAlarm("storageFailed",
                                  {{"message", storage_.lastError().toStdString()}});
    historyScreen_->refresh();
    return stored;
}

void MainWindow::startReanalysis(const QString& spectrumPath) {
    if (!state_.canAnalyze()) {
        historyScreen_->showStatus(QString("Re-analysis not possible now (%1)")
                                       .arg(f20app::toString(state_.state())),
                                   true);
        return;
    }
    state_.onAnalyzeStarted();
    historyScreen_->showStatus("Re-analyzing " + spectrumPath + " ...", false);

    // Spec §6.3: re-analyze with the current recipe - set it first, so the
    // recipe stored with the result is the one the bridge really used.
    MeasurementRecord record;
    record.recipeName = measureScreen_->currentRecipe();
    record.operatorName = measureScreen_->operatorName();
    record.sampleId =
        storage_.sampleIdForSpectrum(spectrumPath).value_or(measureScreen_->sampleId());
    record.spectrumFile = spectrumPath;
    record.reanalyzedFrom = spectrumPath;

    bridge_.send("setRecipe", {{"name", record.recipeName.toStdString()}}, this,
                 [this, record](const f20::Reply& recipeReply) {
        if (!recipeReply.ok) {
            failReanalysis(operatorText(recipeReply, record.recipeName));
            return;
        }
        bridge_.send("openSpectrum", {{"path", record.spectrumFile.toStdString()}}, this,
                     [this, record](const f20::Reply& openReply) {
            if (!openReply.ok) {
                failReanalysis(operatorText(openReply));
                return;
            }
            bridge_.send("analyzeSpectrum", {}, this,
                         [this, record](const f20::Reply& reply) {
                if (!reply.ok) {
                    failReanalysis(operatorText(reply));
                    return;
                }
                const auto result = f20::measureResultFromJson(reply.result);
                if (!result) {
                    failReanalysis("Malformed result from bridge");
                    return;
                }
                state_.onAnalyzeFinished();
                if (storeResult(*result, record))
                    historyScreen_->showStatus("Re-analysis stored (original kept)", false);
                else
                    historyScreen_->showStatus("Re-analysis NOT saved to the database - see log",
                                               true);
            }, measureTimeoutMs_);
        });
    });
}

void MainWindow::failReanalysis(const QString& operatorMessage) {
    state_.onAnalyzeFinished();
    historyScreen_->showStatus("Re-analysis failed: " + operatorMessage, true);
}

void MainWindow::runBaselineWizard() {
    if (!state_.canOpenBaselineWizard()) {
        measureScreen_->showError(QString("Baseline not possible now (%1)")
                                      .arg(f20app::toString(state_.state())));
        return;
    }
    state_.onBaselineWizardOpened();
    auto* wizard = new BaselineWizard(bridge_, this);
    baselineWizard_ = wizard;
    // finished: Finish, Cancel and the window's close button all end here.
    // A commit counts even if the operator then cancels - the bridge has it.
    connect(wizard, &QDialog::finished, this, [this, wizard] {
        const auto committedAt = wizard->committedAtUtc();
        if (committedAt) {
            if (!storage_.saveBaseline(channelSerial_, wizard->referenceMaterial(), *committedAt))
                qWarning().noquote() << "baseline commit not stored - its age will be"
                                        " unknown after a restart";
            baseline_.committed(toTimePoint(*committedAt));
        } else {
            baseline_.invalidate();
        }
        state_.onBaselineWizardClosed(committedAt.has_value());
        checkBaselineAge(); // a wizard left open after the commit ages too
        updateMeasurePermission();
        updateStatusBar();
    });
    wizard->setAttribute(Qt::WA_DeleteOnClose);
    wizard->open();
}

void MainWindow::onBridgeConnected() {
    state_.onBridgeUp();
    diagnosticsScreen_->setBridgeState("connected", true);
    bridge_.send("getStatus", {}, this, [this](const f20::Reply& reply) {
        if (!reply.ok) {
            if (reply.errorCode == "hardwareMissing")
                enterDeviceFault(operatorText(reply));
            return;
        }
        const std::string reported = f20::stringAt(reply.result, "channelSerial").value_or("");
        switch (f20app::checkChannelSerial(expectedSerial_.toStdString(), reported)) {
        case f20app::SerialCheck::ok:
            break;
        case f20app::SerialCheck::notConfigured:
            qWarning().noquote() << "[device] no serial in f20.ini - the bridge's F20 is not checked";
            break;
        case f20app::SerialCheck::mismatch:
        case f20app::SerialCheck::missing:
            // Before channelSerial_ is set: no baseline of the wrong F20 is restored.
            enterDeviceFault(QString("Wrong F20: f20.ini expects %1, the bridge reports %2 - "
                                     "check [device] serial")
                                 .arg(expectedSerial_,
                                      reported.empty() ? QString("no serial")
                                                       : QString::fromStdString(reported)));
            return;
        }
        channelSerial_ = QString::fromStdString(reported);
        if (f20::boolAt(reply.result, "baselineValid").value_or(false))
            restoreBaselineAge();
        updateStatusBar();
    });
    refreshDiagnostics();
}

// The bridge is up, but not with a usable F20 for this app: Fault, so
// nothing measures, and the operator and the server learn why. A reconnect
// (Diagnostics) runs the check again.
void MainWindow::enterDeviceFault(const QString& message) {
    state_.onBridgeDown();
    measureScreen_->showError(message);
    diagnosticsScreen_->setBridgeState(message, false);
    diagnosticsScreen_->appendLog("[device] " + message);
    qCritical().noquote() << "[device]" << message;
    serverLink_->publishAlarm("bridgeFault", {{"message", message.toStdString()}});
    updateStatusBar();
}

// FILMeasure says a baseline exists, but not how old it is. The age comes
// from our own record of the commit (stored with the time, like a
// calibration date). No record means an unknown age, and an unknown age
// must never pass as fresh - so the operator runs the baseline again.
void MainWindow::restoreBaselineAge() {
    const auto committedAt = storage_.lastValidBaseline(channelSerial_);
    if (!committedAt) {
        diagnosticsScreen_->appendLog("[baseline] bridge has a baseline of unknown age - "
                                      "run the baseline");
        return;
    }
    baseline_.committed(toTimePoint(*committedAt));
    state_.onBaselineValid();
    checkBaselineAge();
    diagnosticsScreen_->appendLog("[baseline] restored, committed " +
                                  committedAt->toLocalTime().toString(Qt::ISODate));
}

// Stale blocks measuring (spec §6.2). Run by the age timer AND at every
// measure trigger, so there is no window between timer ticks.
void MainWindow::checkBaselineAge() {
    if (baseline_.status() == f20app::BaselineStatus::Stale &&
        state_.state() == f20app::AppState::Ready) {
        state_.onBaselineInvalid();
        serverLink_->publishAlarm("baselineStale", {});
    }
}

// Stored as invalid too, so a reconnect cannot bring this baseline back.
void MainWindow::invalidateBaseline(const QString& reason) {
    storage_.invalidateBaselines(channelSerial_, QDateTime::currentDateTimeUtc());
    baseline_.invalidate();
    state_.onBaselineInvalid();
    diagnosticsScreen_->appendLog("[baseline] invalidated: " + reason);
    updateStatusBar();
}

void MainWindow::refreshDiagnostics() {
    diagnosticsScreen_->setBridgeState(
        bridge_.isConnected() ? "connected" : "disconnected",
        bridge_.isConnected());
    bridge_.send("getVersion", {}, this, [this](const f20::Reply& reply) {
        if (!reply.ok)
            return;
        bridgeVersion_ = QString::fromStdString(
            f20::stringAt(reply.result, "bridge").value_or("?") + " / " +
            f20::stringAt(reply.result, "filmeasure").value_or("?"));
        diagnosticsScreen_->setVersions(bridgeVersion_);
    });
    // Signal health reads the spectrometer: not between the steps of a
    // measurement, a re-analysis or a baseline.
    if (!state_.canAnalyze()) {
        diagnosticsScreen_->appendLog(QString("[diagnostics] signal health not read while %1")
                                          .arg(f20app::toString(state_.state())));
        return;
    }
    bridge_.send("getDiagnostics", {}, this, [this](const f20::Reply& reply) {
        if (reply.ok)
            diagnosticsScreen_->setSignalHealth(
                f20::intAt(reply.result, "referenceCounts").value_or(0),
                f20::intAt(reply.result, "backgroundCounts").value_or(0));
    });
}

void MainWindow::updateMeasurePermission() {
    const auto refusal = measureRefusal();
    measureScreen_->setMeasureAllowed(!refusal,
                                      refusal ? f20app::operatorText(*refusal) : QString());
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
    const bool usable = state_.state() == f20app::AppState::Ready ||
                        state_.state() == f20app::AppState::Measuring;
    const auto baseline = baseline_.status();
    f20::json status{{"state", f20app::toString(state_.state())},
                     {"baselineValid",
                      usable && (baseline == f20app::BaselineStatus::Fresh ||
                                 baseline == f20app::BaselineStatus::Aging)},
                     {"channelSerial", channelSerial_.toStdString()}};
    if (const auto age = baseline_.ageMinutes())
        status["baselineAgeMinutes"] = *age;
    if (const auto committedAt = baseline_.committedAt())
        status["baselineCommittedAtUtc"] =
            toDateTimeUtc(*committedAt).toString(Qt::ISODateWithMs).toStdString();
    serverLink_->publishStatus(status);
}
