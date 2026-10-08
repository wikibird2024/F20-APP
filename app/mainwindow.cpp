#include "mainwindow.h"

#include "baselinewizard.h"
#include "brokertest.h"
#include "devicecheck.h"
#include "f20/envelope.h"
#include "diagnosticsscreen.h"
#include "f20/errors.h"
#include "f20/jsonread.h"
#include "filenames.h"
#include "historyscreen.h"
#include "measurescreen.h"
#include "mqttserverlink.h"
#include "serverlink.h"
#include "settingsscreen.h"

#ifdef F20_HAS_MQTT
#include "pahomqtttransport.h"
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

#include <algorithm>
#include <chrono>

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

MainWindow::MainWindow(const QString& configPath, const QString& changesPath, QWidget* parent)
    : QMainWindow(parent),
      settings_(configPath, QSettings::IniFormat),
      appSettings_(AppSettings::load(configPath, changesPath)),
      configDir_(QFileInfo(configPath).absoluteDir()) {
    setWindowTitle("F20 control");
    resize(1100, 720);

    if (!QFileInfo::exists(configPath))
        qWarning().noquote() << "config file not found:" << configPath << "- using defaults";
    // Start anyway: an engineer fixes these in the Settings tab (spec §6.7).
    for (const QString& note : appSettings_.loadNotes)
        qWarning().noquote() << "[settings]" << note;
    for (const QString& problem : appSettings_.problems())
        qWarning().noquote() << "[settings]" << problem;

    loadRecipeLimits();
    // Power-on, as far as the software can tell: the app starts with Windows.
    warmUp_.start();
    BridgeClient::Timing timing;
    timing.requestTimeoutMs =
        settings_.value("bridge/timeoutMs", timing.requestTimeoutMs).toInt();
    bridge_.setTiming(timing);
    // Acquire and analyze can take much longer than a status request.
    measureTimeoutMs_ = settings_.value("bridge/measureTimeoutMs", 60000).toInt();
    expectedSerial_ = appSettings_.deviceSerial.trimmed();
    resultsPageSize_ = std::clamp(settings_.value("mqtt/pageSize", 50).toInt(), 1, 500);

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
    auto* settingsScreen = new SettingsScreen(configPath, changesPath);
    tabs->addTab(measureScreen_, "Measure");
    tabs->addTab(historyScreen_, "History / SPC");
    tabs->addTab(diagnosticsScreen_, "Diagnostics");
    tabs->addTab(settingsScreen, "Settings");
    setCentralWidget(tabs);

    auto* toolbar = addToolBar("main");
    toolbar->setMovable(false);
    baselineAction_ = toolbar->addAction("Baseline...", this, &MainWindow::runBaselineWizard);
    skipWarmUpAction_ = toolbar->addAction("Skip warm-up...", this, &MainWindow::skipWarmUp);
    baselineInvalidAction_ =
        toolbar->addAction("Baseline invalid...", this, &MainWindow::askBaselineInvalid);

    stateLabel_ = new QLabel("starting");
    serverLabel_ = new QLabel;
    baselineLabel_ = new QLabel("baseline: none");
    warmUpLabel_ = new QLabel;
    statusBar()->addWidget(stateLabel_);
    statusBar()->addPermanentWidget(serverLabel_);
    statusBar()->addPermanentWidget(warmUpLabel_);
    statusBar()->addPermanentWidget(baselineLabel_);
    // Right corner: tells at a glance which build is running.
    statusBar()->addPermanentWidget(new QLabel("v" + QCoreApplication::applicationVersion()));

    // Settings wiring (spec §6.7). Saved values wait for a restart; the
    // notice stays until then, so the old values never look "ignored".
    auto* restartNotice = new QLabel("Settings changed - restart to use them");
    restartNotice->setStyleSheet("color: #b35900; font-weight: bold;");
    restartNotice->hide();
    statusBar()->addPermanentWidget(restartNotice);
    connect(settingsScreen, &SettingsScreen::saved, restartNotice, &QLabel::show);
    connect(settingsScreen, &SettingsScreen::restartRequested, this, &MainWindow::restartApp);
    connect(settingsScreen, &SettingsScreen::reloadRecipesRequested, this, &MainWindow::loadRecipes);
    connect(tabs, &QTabWidget::currentChanged, settingsScreen, [tabs, settingsScreen] {
        if (tabs->currentWidget() != settingsScreen)
            settingsScreen->lock(); // leaving the tab locks it
    });

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
        closeBaselineDialogs();
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
        serverLink_->publishAlarm("fault", {{"message", "bridge not reachable after 3 retries"}});
    });
    connect(&bridge_, &BridgeClient::protocolLog, this, [this](const QString& line) {
        diagnosticsScreen_->appendLog(line);
        qInfo().noquote() << "[bridge]" << line; // spec §7: every command and reply in the log file
    });
    connect(&bridge_, &BridgeClient::eventReceived, this, &MainWindow::onBridgeEvent);

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
                    static_cast<quint16>(appSettings_.bridgePort));
            });

    // Server requests (spec 6.6.4): the same paths as the operator's
    // buttons, each answered once with its transaction id.
    connect(serverLink_, &ServerLink::remoteMeasureRequested, this,
            [this](const QString& transactionId, const QString& recipe, const QString& sampleId) {
                startMeasurement(Trigger::remote, transactionId, recipe, sampleId);
            });
    connect(serverLink_, &ServerLink::remoteBaselineInvalidate, this,
            &MainWindow::answerBaselineInvalidate);
    connect(serverLink_, &ServerLink::resultsRequested, this, &MainWindow::answerResults);
    connect(serverLink_, &ServerLink::spectrumRequested, this, &MainWindow::answerSpectrum);

    // The recipe decides the baseline limits (spec 2.2 #3). setRecipes()
    // ran before this connect, so apply the first recipe by hand.
    connect(measureScreen_, &MeasureScreen::recipeChanged, this, &MainWindow::applyRecipeLimits);
    applyRecipeLimits(measureScreen_->currentRecipe());

    // Baseline age watchdog (spec §6.2): stale blocks measuring + alarm.
    ageTimer_.start(5000);
    connect(&ageTimer_, &QTimer::timeout, this, [this] {
        updateWarmUp();
        syncBaselineState();
        updateMeasurePermission();
        updateStatusBar();
        publishStatus(); // baseline age and warm-up count down
    });

    updateMeasurePermission();
    updateStatusBar();

    bridge_.connectToBridge(
        settings_.value("bridge/host", "127.0.0.1").toString(),
        static_cast<quint16>(appSettings_.bridgePort));
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

// MQTT when the settings name a broker and the build has Paho; otherwise the
// logging stand-in, and the app works exactly as without a server.
ServerLink* MainWindow::createServerLink() {
    if (appSettings_.mqttBroker.trimmed().isEmpty())
        return new NullServerLink(this);
#ifdef F20_HAS_MQTT
    MqttServerLink::Settings link;
    link.serial = appSettings_.deviceSerial.trimmed();
    link.statusIntervalMs = settings_.value("mqtt/statusIntervalMs", link.statusIntervalMs).toInt();
    link.ackTimeoutMs = settings_.value("mqtt/ackTimeoutMs", link.ackTimeoutMs).toInt();
    link.broker = transportSettings(appSettings_);
    link.broker.keepAliveSeconds =
        settings_.value("mqtt/keepAliveSeconds", link.broker.keepAliveSeconds).toInt();
    return new MqttServerLink(new PahoMqttTransport, link, this);
#else
    qWarning().noquote() << "the settings name an MQTT broker, but this build has no MQTT"
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
    const QString folder = resolvePath(appSettings_.recipesFolder);
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

void MainWindow::startMeasurement(Trigger trigger, const QString& transactionId,
                                  const QString& recipeName, const QString& sampleId) {
    const QString recipe = recipeName.isEmpty() ? measureScreen_->currentRecipe() : recipeName;
    // This recipe's limits, right now - not at the next timer tick.
    applyRecipeLimits(recipe);
    if (const auto refusal = measureRefusal()) {
        const QString text = f20app::operatorText(*refusal);
        if (trigger == Trigger::remote) {
            // Same check as the button (spec 2.3 #5); the server learns why.
            logEvent("[measure] server request refused: " + text);
            serverLink_->sendResponse(transactionId, "measure",
                                      {{"error", f20app::serverErrorCode(*refusal)},
                                       {"message", text.toStdString()}});
            return;
        }
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

    MeasurementRecord record;
    record.recipeName = recipe;
    record.operatorName = measureScreen_->operatorName();
    if (trigger == Trigger::remote && record.operatorName.trimmed().isEmpty())
        record.operatorName = "server";
    record.sampleId = sampleId.isEmpty() ? measureScreen_->sampleId() : sampleId;
    const QString tid = transactionId; // empty unless the server asked
    if (const auto committedAt = baseline_.committedAt()) // the baseline this result uses
        record.baselineCommittedAtUtc = toDateTimeUtc(*committedAt);

    // One spectrum feeds the result, the chart and the saved file:
    // acquire -> analyze -> save. A separate acquire after `measure` would
    // save a second, different spectrum next to the result.
    bridge_.send("setRecipe", {{"name", recipe.toStdString()}}, this,
                 [this, record, tid](const f20::Reply& recipeReply) {
        if (!recipeReply.ok) {
            failMeasurement(operatorText(recipeReply, record.recipeName), tid,
                            QString::fromStdString(recipeReply.errorCode));
            return;
        }
        bridge_.send("acquireSpectrum", {}, this,
                     [this, record, tid](const f20::Reply& spectrumReply) {
            if (!spectrumReply.ok) {
                failMeasurement(operatorText(spectrumReply), tid,
                                QString::fromStdString(spectrumReply.errorCode));
                return;
            }
            const auto spectrum = f20::spectrumFromJson(spectrumReply.result);
            if (!spectrum) {
                failMeasurement("Malformed spectrum from bridge", tid, "filmeasureError");
                return;
            }
            measureScreen_->showSpectrum(*spectrum);
            bridge_.send("analyzeSpectrum", {}, this,
                         [this, record, tid](const f20::Reply& analyzeReply) {
                if (!analyzeReply.ok) {
                    failMeasurement(operatorText(analyzeReply), tid,
                                    QString::fromStdString(analyzeReply.errorCode));
                    return;
                }
                const auto result = f20::measureResultFromJson(analyzeReply.result);
                if (!result) {
                    failMeasurement("Malformed result from bridge", tid, "filmeasureError");
                    return;
                }
                const QString file = spectrumFilePath(record.sampleId);
                bridge_.send("saveSpectrum", {{"path", file.toStdString()}}, this,
                             [this, record, result, file, tid](
                                 const f20::Reply& saveReply) mutable {
                    if (saveReply.ok)
                        record.spectrumFile = file; // only a file that exists
                    else
                        diagnosticsScreen_->appendLog("[measure] spectrum NOT saved: " +
                                                      operatorText(saveReply));
                    state_.onMeasureFinished();
                    measureScreen_->showResult(*result);
                    const auto resultId = storeResult(*result, record);
                    if (!resultId)
                        measureScreen_->showError("Result NOT saved to the database - see log");
                    publishResultToServer(*result, record, resultId.value_or(""), tid);
                }, measureTimeoutMs_);
            }, measureTimeoutMs_);
        }, measureTimeoutMs_);
    });
}

void MainWindow::failMeasurement(const QString& operatorMessage, const QString& transactionId,
                                 const QString& errorCode) {
    state_.onMeasureFinished(); // ignored if the bridge drop already made it Fault
    measureScreen_->showError(operatorMessage);
    if (!transactionId.isEmpty())
        serverLink_->sendResponse(transactionId, "measure",
                                  {{"error", errorCode.isEmpty() ? std::string("filmeasureError")
                                                                 : errorCode.toStdString()},
                                   {"message", operatorMessage.toStdString()}});
}

// The snapshot fields every stored result carries; returns the result id,
// or nullopt (and raises the alarm) when the database refused the result.
std::optional<QString> MainWindow::storeResult(const f20::MeasureResult& result,
                                               MeasurementRecord record) {
    record.channelSerial = channelSerial_;
    record.appVersion = QCoreApplication::applicationVersion();
    record.bridgeVersion = bridgeVersion_;
    const auto resultId = storage_.insertMeasurement(result, record);
    if (!resultId)
        serverLink_->publishAlarm(
            "fault", {{"message", ("result not saved: " + storage_.lastError()).toStdString()}});
    historyScreen_->refresh();
    return resultId;
}

// Spec 8.2.5.2: the result message after every measurement and
// re-analysis; the same data answers a server's measure request.
void MainWindow::publishResultToServer(const f20::MeasureResult& result,
                                       const MeasurementRecord& record, const QString& resultId,
                                       const QString& transactionId) {
    const auto now = std::chrono::system_clock::now();
    f20::ResultFacts facts;
    facts.resultId = resultId.toStdString();
    facts.measuredAtUtc = f20::isoUtc(now);
    facts.recipeName = record.recipeName.toStdString();
    facts.sampleId = record.sampleId.toStdString();
    facts.operatorName = record.operatorName.toStdString();
    facts.reanalyzedFrom = record.reanalyzedFromResultId.toStdString();
    if (record.baselineCommittedAtUtc)
        facts.baselineAgeMinutes =
            static_cast<int>(record.baselineCommittedAtUtc->secsTo(toDateTimeUtc(now)) / 60);
    const f20::json data = f20::serverResultData(result, facts);

    serverLink_->publishResult(data);
    if (!result.passed)
        serverLink_->publishAlarm(
            "fail", {{"message", QString("FAIL: recipe '%1', sample '%2'")
                                     .arg(record.recipeName, record.sampleId)
                                     .toStdString()},
                     {"result_id", facts.resultId}});
    if (!transactionId.isEmpty())
        serverLink_->sendResponse(transactionId, "measure", data);
}

void MainWindow::answerBaselineInvalidate(const QString& transactionId, const QString& reason) {
    invalidateBaseline("server: " + (reason.trimmed().isEmpty() ? QString("no reason given")
                                                                : reason.trimmed()));
    serverLink_->sendAck(transactionId, "baseline_invalidate", f20::json::object());
}

// Spec 8.2.5.4: the server's copy is the database of record; it backfills
// gaps from ours, page by page, oldest first.
void MainWindow::answerResults(const QString& transactionId, const QString& sinceUtc, int page) {
    QDateTime since;
    if (!sinceUtc.isEmpty()) {
        since = QDateTime::fromString(sinceUtc, Qt::ISODateWithMs);
        if (!since.isValid()) {
            serverLink_->sendResponse(transactionId, "get_results",
                                      {{"error", "badRequest"},
                                       {"message", "since must be an ISO 8601 UTC time"}});
            return;
        }
    }
    if (!storage_.isOpen()) {
        serverLink_->sendResponse(transactionId, "get_results",
                                  {{"error", "storageDown"}, {"message", "database not available"}});
        return;
    }
    const ResultPage found =
        storage_.resultsSince(since, (page - 1) * resultsPageSize_, resultsPageSize_);
    f20::json results = f20::json::array();
    for (const StoredResult& stored : found.results) {
        f20::ResultFacts facts;
        facts.resultId = stored.resultId.toStdString();
        facts.measuredAtUtc = stored.timeUtc.toStdString();
        facts.recipeName = stored.recipeName.toStdString();
        facts.sampleId = stored.sampleId.toStdString();
        facts.operatorName = stored.operatorName.toStdString();
        facts.baselineAgeMinutes = stored.baselineAgeMinutes;
        facts.reanalyzedFrom = stored.reanalyzedFromResultId.toStdString();
        results.push_back(f20::serverResultData(stored.result, facts));
    }
    const int pages = std::max(1, (found.total + resultsPageSize_ - 1) / resultsPageSize_);
    serverLink_->sendResponse(transactionId, "get_results",
                              {{"results", std::move(results)}, {"page", page}, {"pages", pages}});
}

// Spec 8.2.5.4: the saved spectrum of one result, as CSV text.
void MainWindow::answerSpectrum(const QString& transactionId, const QString& resultId) {
    const auto reply = [&](const char* error, const QString& message, const std::string& csv = {}) {
        f20::json data{{"result_id", resultId.toStdString()}, {"csv", csv}, {"error", error}};
        if (!message.isEmpty())
            data["message"] = message.toStdString();
        serverLink_->sendResponse(transactionId, "get_spectrum", data);
    };
    const auto file = storage_.spectrumFileForResult(resultId);
    if (!file)
        return reply("resultNotFound", "no result with this id");
    if (file->isEmpty())
        return reply("fileOpenFailed", "no spectrum was saved for this result");
    QFile csv(*file);
    if (!csv.open(QIODevice::ReadOnly | QIODevice::Text))
        return reply("fileOpenFailed", "cannot open " + *file);
    if (csv.size() > 900 * 1024) // one MQTT message stays under the 1 MiB limit
        return reply("fileOpenFailed", "spectrum file too large for one message");
    reply("", {}, csv.readAll().toStdString());
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
    record.reanalyzedFromResultId = storage_.resultIdForSpectrum(spectrumPath).value_or("");
    if (record.reanalyzedFromResultId.isEmpty())
        qInfo().noquote() << "[storage] no stored result owns" << spectrumPath
                          << "- reanalyzed_from is sent empty";

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
                const auto resultId = storeResult(*result, record);
                if (resultId)
                    historyScreen_->showStatus("Re-analysis stored (original kept)", false);
                else
                    historyScreen_->showStatus("Re-analysis NOT saved to the database - see log",
                                               true);
                publishResultToServer(*result, record, resultId.value_or(""), {});
            }, measureTimeoutMs_);
        });
    });
}

void MainWindow::failReanalysis(const QString& operatorMessage) {
    state_.onAnalyzeFinished();
    historyScreen_->showStatus("Re-analysis failed: " + operatorMessage, true);
}

void MainWindow::runBaselineWizard() {
    if (!warmUp_.isDone()) {
        measureScreen_->showError(QString("Lamp warming up - %1 min left (Skip warm-up... to override)")
                                      .arg(warmUp_.minutesLeft()));
        return;
    }
    if (!state_.canOpenBaselineWizard()) {
        measureScreen_->showError(QString("Baseline not possible now (%1)")
                                      .arg(f20app::toString(state_.state())));
        return;
    }
    // A full baseline replaces the one on offer.
    recoverableCommit_.reset();
    if (recoverPrompt_)
        recoverPrompt_->reject();
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
        syncBaselineState(); // a wizard left open after the commit ages too
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

        // Our last commit for this F20, judged by the current recipe's limits.
        const auto committedAt = storage_.lastValidBaseline(channelSerial_);
        std::optional<f20app::BaselineStatus> stored;
        if (committedAt) {
            f20app::BaselineTracker probe = baseline_;
            probe.committed(toTimePoint(*committedAt));
            stored = probe.status();
        }
        const bool bridgeHasBaseline = f20::boolAt(reply.result, "baselineValid").value_or(false);
        switch (f20app::startupBaseline(bridgeHasBaseline, stored)) {
        case f20app::StartupBaseline::restoreAge:
        case f20app::StartupBaseline::unknownAge:
            restoreBaselineAge();
            break;
        case f20app::StartupBaseline::offerRecover:
            recoverableCommit_ = committedAt;
            offerBaselineRecovery();
            break;
        case f20app::StartupBaseline::storedTooOld:
            logEvent("[baseline] FILMeasure has no active baseline; the last one (" +
                     committedAt->toLocalTime().toString("HH:mm") +
                     ") is too old to recover - run the baseline");
            break;
        case f20app::StartupBaseline::runWizard:
            break;
        }
        updateStatusBar();
    });
    refreshDiagnostics();
}

// Before the app goes to Fault (bridge lost, FILMeasure died). The wizard
// closes first, while the state is still Baselining: its finished handler
// then keeps a commit that already happened - closed after the drop, the
// state would be Fault and the commit ignored. An open recovery offer is
// stale too: the reconnect asks again.
void MainWindow::closeBaselineDialogs() {
    if (baselineWizard_)
        baselineWizard_->reject();
    recoverableCommit_.reset();
    if (recoverPrompt_)
        recoverPrompt_->reject();
}

// Events the bridge pushes without a request (spec 5.4).
void MainWindow::onBridgeEvent(const QString& name, const f20::json& data) {
    if (name == "filmeasureDied") {
        // Spec 7.1: FILMeasure crashed, so its active baseline is gone;
        // nothing measures until it is back. The bridge exits and is
        // restarted, which drops the connection: the reconnect finds no
        // active baseline and offers recovery. A bridge that stayed
        // connected would leave the app in Fault until "Reconnect bridge".
        const QString message = QString("FILMeasure stopped (exit code %1)")
                                    .arg(f20::intAt(data, "exitCode").value_or(-1));
        closeBaselineDialogs();
        baseline_.invalidate();
        state_.onBridgeDown();
        logEvent("[bridge] " + message);
        measureScreen_->showError(message + " - waiting for the bridge to start it again");
        serverLink_->publishAlarm("fault", {{"message", message.toStdString()}});
        updateStatusBar();
        return;
    }
    if (name == "startupWarning") {
        // FILMeasure started, but e.g. its startup recipe did not load.
        const QString warning = QString::fromStdString(f20::stringAt(data, "warning").value_or("warning"));
        const QString detail = QString::fromStdString(f20::stringAt(data, "message").value_or(""));
        const QString text = "FILMeasure startup warning: " + warning +
                             (detail.isEmpty() ? QString() : " - " + detail);
        logEvent("[bridge] " + text);
        measureScreen_->showError(text);
        return;
    }
    logEvent("[bridge] unknown event " + name);
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
    serverLink_->publishAlarm("fault", {{"message", message.toStdString()}});
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
    syncBaselineState();
    diagnosticsScreen_->appendLog("[baseline] restored, committed " +
                                  committedAt->toLocalTime().toString(Qt::ISODate));
}

// Spec 2.1 #4: after a restart or power cut FILMeasure may have lost its
// active baseline while the reference data is still on disk; recovering
// spares a full baseline. Offered only for a commit we stored ourselves,
// so the age stays known - and it keeps that original commit time. After a
// power cut the lamp is cold, so the offer waits for the warm-up.
void MainWindow::offerBaselineRecovery() {
    if (!recoverableCommit_ || recoverPrompt_ || !bridge_.isConnected() ||
        state_.state() != f20app::AppState::NoBaseline)
        return;
    if (!warmUp_.isDone()) {
        logEvent("[baseline] recovery of the last baseline is offered after the lamp warm-up");
        return;
    }
    f20app::BaselineTracker probe = baseline_; // judged by the limits right now
    probe.committed(toTimePoint(*recoverableCommit_));
    if (probe.status() == f20app::BaselineStatus::Stale) {
        logEvent("[baseline] the last baseline got too old while waiting - run the baseline");
        recoverableCommit_.reset();
        return;
    }

    auto* box = new QMessageBox(
        QMessageBox::Question, "Recover baseline",
        QString("FILMeasure has no active baseline.\n\nRecover the last baseline "
                "(committed %1, %2 min ago)?\nIts age counts from that commit.")
            .arg(recoverableCommit_->toLocalTime().toString("HH:mm"))
            .arg(probe.ageMinutes().value_or(0)),
        QMessageBox::Yes | QMessageBox::No, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    recoverPrompt_ = box;
    logEvent("[baseline] FILMeasure has no active baseline - offering to recover the one "
             "committed " + recoverableCommit_->toLocalTime().toString(Qt::ISODate));
    connect(box, &QDialog::finished, this, [this, box] {
        if (!recoverableCommit_)
            return; // withdrawn: bridge drop or wizard opened
        if (box->clickedButton() == box->button(QMessageBox::Yes)) {
            recoverBaseline();
        } else {
            logEvent("[baseline] recovery declined - run the baseline");
            recoverableCommit_.reset();
        }
    });
    box->open();
}

// Uses the Baselining state while it runs: like the wizard, it must not
// overlap a measurement.
void MainWindow::recoverBaseline() {
    if (!recoverableCommit_ || state_.state() != f20app::AppState::NoBaseline)
        return;
    const QDateTime commit = *recoverableCommit_;
    recoverableCommit_.reset();
    state_.onBaselineWizardOpened();
    logEvent("[baseline] recovering the baseline committed " +
             commit.toLocalTime().toString(Qt::ISODate));
    bridge_.send("baselineRecover", {}, this, [this, commit](const f20::Reply& reply) {
        if (state_.state() != f20app::AppState::Baselining)
            return; // the bridge dropped meanwhile: Fault, the reconnect asks again
        if (reply.ok) {
            baseline_.committed(toTimePoint(commit));
            state_.onBaselineWizardClosed(true);
            logEvent("[baseline] recovered - age counts from " +
                     commit.toLocalTime().toString(Qt::ISODate));
        } else {
            state_.onBaselineWizardClosed(false);
            const QString text = operatorText(reply);
            measureScreen_->showError(text);
            logEvent("[baseline] recovery failed: " + text);
            // Gone for good: never offer this one again. A timeout or a lost
            // connection says nothing about the baseline, so those don't.
            if (reply.errorCode == "baselineRecoverFailed")
                storage_.invalidateBaselines(channelSerial_, QDateTime::currentDateTimeUtc());
        }
        syncBaselineState();
        updateMeasurePermission();
        updateStatusBar();
    });
}

// Stale blocks measuring (spec §6.2); a recipe with longer limits makes the
// same baseline valid again. Run by the age timer, on a recipe change AND at
// every measure trigger, so there is no window between timer ticks.
void MainWindow::syncBaselineState() {
    switch (f20app::baselineChange(state_.state(), baseline_.status())) {
    case f20app::BaselineChange::nowStale:
        state_.onBaselineInvalid();
        serverLink_->publishAlarm(
            "baseline_stale",
            {{"message", QString("baseline older than %1 min, the limit of '%2'")
                             .arg(recipeLimits_.forRecipe(limitsRecipe_.toStdString()).blockMinutes)
                             .arg(limitsRecipe_)
                             .toStdString()}});
        break;
    case f20app::BaselineChange::nowValid:
        state_.onBaselineValid();
        diagnosticsScreen_->appendLog("[baseline] within the limits of '" + limitsRecipe_ +
                                      "' - measuring allowed again");
        break;
    case f20app::BaselineChange::none:
        break;
    }
}

// Baseline limits per recipe (spec 2.2 #3): [baseline] holds the defaults,
// each [baselineProfile_<name>] a group of recipes with its own limits; a
// key missing in a profile falls back to the default.
void MainWindow::loadRecipeLimits() {
    f20app::BaselineLimits defaults;
    defaults.warnMinutes = appSettings_.baselineWarnMinutes;
    defaults.blockMinutes = appSettings_.baselineBlockMinutes;
    defaults.warmUpMinutes = appSettings_.warmUpMinutes;
    if (const auto problem = f20app::checkLimits(defaults)) {
        qWarning().noquote() << "[baseline] default limits not usable:"
                             << QString::fromStdString(*problem) << "- using 20/30/15 min";
        defaults = {};
    }
    recipeLimits_ = f20app::RecipeLimits(defaults);

    const QString prefix = "baselineProfile_";
    for (const QString& group : settings_.childGroups()) {
        if (!group.startsWith(prefix))
            continue;
        f20app::LimitProfile profile;
        profile.name = group.mid(prefix.size()).toStdString();
        settings_.beginGroup(group);
        for (const QString& recipe : settings_.value("recipes").toStringList())
            if (!recipe.trimmed().isEmpty())
                profile.recipes.push_back(recipe.trimmed().toStdString());
        profile.limits.warnMinutes = settings_.value("warnMinutes", defaults.warnMinutes).toInt();
        profile.limits.blockMinutes = settings_.value("blockMinutes", defaults.blockMinutes).toInt();
        profile.limits.warmUpMinutes =
            settings_.value("warmUpMinutes", defaults.warmUpMinutes).toInt();
        settings_.endGroup();
        if (const auto problem = f20app::checkLimits(profile.limits)) {
            qWarning().noquote() << "[baseline]" << group << "skipped:"
                                 << QString::fromStdString(*problem);
            continue;
        }
        if (profile.recipes.empty()) {
            qWarning().noquote() << "[baseline]" << group << "skipped: no recipes listed";
            continue;
        }
        recipeLimits_.addProfile(std::move(profile));
    }
    for (const std::string& recipe : recipeLimits_.recipesInSeveralProfiles())
        qWarning().noquote() << "[baseline] recipe" << QString::fromStdString(recipe)
                             << "is in several profiles - using"
                             << QString::fromStdString(recipeLimits_.profileFor(recipe))
                             << "(first in name order)";
}

// The current recipe decides the baseline limits: applied when the
// operator picks a recipe and again right before each measurement.
void MainWindow::applyRecipeLimits(const QString& recipe) {
    const f20app::BaselineLimits limits = recipeLimits_.forRecipe(recipe.toStdString());
    baseline_.setThresholds(limits.warnMinutes, limits.blockMinutes);
    warmUp_.setRequiredMinutes(limits.warmUpMinutes);
    if (recipe != limitsRecipe_) {
        limitsRecipe_ = recipe;
        const QString line = QString("[baseline] limits for '%1' (%2): yellow after %3 min, "
                                     "red after %4 min, lamp warm-up %5 min")
                                 .arg(recipe, QString::fromStdString(
                                                  recipeLimits_.profileFor(recipe.toStdString())))
                                 .arg(limits.warnMinutes)
                                 .arg(limits.blockMinutes)
                                 .arg(limits.warmUpMinutes);
        diagnosticsScreen_->appendLog(line);
        qInfo().noquote() << line;
    }
    updateWarmUp();
    syncBaselineState();
    updateMeasurePermission();
    updateStatusBar();
}

// Skipping the warm-up takes one confirm click; the log records who
// skipped it and how many minutes were left, so a drifting result can be
// traced back to a cold lamp. Non-modal, like the wizard.
// Restart from the Settings screen (spec §6.7). Refused while a
// measurement or a baseline runs: it would be lost half way.
void MainWindow::restartApp() {
    const f20app::AppState now = state_.state();
    if (now == f20app::AppState::Measuring || now == f20app::AppState::Analyzing ||
        now == f20app::AppState::Baselining || baselineWizard_) {
        QMessageBox::information(this, "Restart",
                                 QString("Not now: the app is %1. Restart when it is done.")
                                     .arg(baselineWizard_ ? "in the baseline wizard"
                                                          : f20app::toString(now)));
        return;
    }
    restartRequested_ = true;
    QCoreApplication::quit(); // main() starts the new copy after this window is gone
}

void MainWindow::skipWarmUp() {
    if (warmUp_.isDone())
        return;
    auto* box = new QMessageBox(QMessageBox::Warning, "Skip lamp warm-up",
                                QString("The lamp needs %1 more min.\n"
                                        "A baseline with a cold lamp can drift.")
                                    .arg(warmUp_.minutesLeft()),
                                QMessageBox::NoButton, this);
    QPushButton* skip = box->addButton("Skip", QMessageBox::AcceptRole);
    box->addButton(QMessageBox::No);
    box->setDefaultButton(QMessageBox::No); // Enter does not skip by accident
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QDialog::finished, this, [this, box, skip] {
        if (box->clickedButton() != skip || warmUp_.isDone())
            return;
        const int minutesLeft = warmUp_.minutesLeft();
        warmUp_.skip();
        const QString who = measureScreen_->operatorName().trimmed();
        logEvent(QString("[warm-up] skipped with %1 min left by %2")
                     .arg(minutesLeft)
                     .arg(who.isEmpty() ? QString("(no operator name)") : who));
        updateWarmUp();
        updateStatusBar();
    });
    box->open();
}

// Notes the moment the lamp counts as warm. A recipe that needs a longer
// warm-up can make it cold again - the lamp has not been on long enough.
void MainWindow::updateWarmUp() {
    const bool done = warmUp_.isDone();
    const bool justDone = done && !warmUpWasDone_;
    warmUpWasDone_ = done;
    if (!justDone)
        return;
    if (!warmUp_.isSkipped())
        logEvent("[warm-up] lamp warm - baseline possible");
    offerBaselineRecovery(); // it waited for the lamp
}

// Diagnostics screen AND the log file - appendLog alone is screen only.
void MainWindow::logEvent(const QString& line) {
    diagnosticsScreen_->appendLog(line);
    qInfo().noquote() << line;
}

// Stored as invalid too, so a reconnect cannot bring this baseline back.
void MainWindow::invalidateBaseline(const QString& reason) {
    storage_.invalidateBaselines(channelSerial_, QDateTime::currentDateTimeUtc());
    baseline_.invalidate();
    state_.onBaselineInvalid();
    logEvent("[baseline] invalidated: " + reason);
    updateStatusBar();
}

// Spec 2.2 #5 and 6.2: the operator marks the baseline invalid when
// something the software cannot see has changed - the fiber moved, the room
// warmed up. FIRemote has no way to read the integration time either, so
// that change is reported here too. The reason is logged.
void MainWindow::askBaselineInvalid() {
    if (!baseline_.hasBaseline())
        return;
    auto* dialog = new QInputDialog(this);
    dialog->setWindowTitle("Baseline invalid");
    dialog->setLabelText("Why is the baseline no longer valid? (logged)");
    dialog->setComboBoxItems({"fiber moved", "room temperature changed > 5 °F",
                              "integration time changed", "lamp changed or power cut"});
    dialog->setComboBoxEditable(true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QInputDialog::textValueSelected, this, [this](const QString& text) {
        const QString reason = text.trimmed();
        if (reason.isEmpty() || !baseline_.hasBaseline())
            return;
        const QString who = measureScreen_->operatorName().trimmed();
        invalidateBaseline(QString("operator %1: %2")
                               .arg(who.isEmpty() ? QString("(no name)") : who, reason));
    });
    dialog->open();
}

void MainWindow::refreshDiagnostics() {
    diagnosticsScreen_->setBridgeState(
        bridge_.isConnected() ? "connected" : "disconnected",
        bridge_.isConnected());
    bridge_.send("getVersion", {}, this, [this](const f20::Reply& reply) {
        if (!reply.ok)
            return;
        bridgeOnlyVersion_ =
            QString::fromStdString(f20::stringAt(reply.result, "bridge").value_or("?"));
        filmeasureVersion_ =
            QString::fromStdString(f20::stringAt(reply.result, "filmeasure").value_or("?"));
        bridgeVersion_ = bridgeOnlyVersion_ + " / " + filmeasureVersion_;
        diagnosticsScreen_->setVersions(bridgeVersion_);
        publishStatus();
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

    const bool warm = warmUp_.isDone();
    warmUpLabel_->setVisible(!warm);
    warmUpLabel_->setText(QString("lamp warm-up: %1 min left").arg(warmUp_.minutesLeft()));
    warmUpLabel_->setStyleSheet("color: #b05000;");
    baselineAction_->setEnabled(warm);
    skipWarmUpAction_->setVisible(!warm);
    baselineInvalidAction_->setEnabled(baseline_.hasBaseline() &&
                                       state_.state() != f20app::AppState::Baselining &&
                                       state_.state() != f20app::AppState::Fault);
    const f20app::BaselineLimits limits = recipeLimits_.forRecipe(limitsRecipe_.toStdString());
    baselineLabel_->setToolTip(QString("limits for '%1': yellow after %2 min, red after %3 min")
                                   .arg(limitsRecipe_)
                                   .arg(limits.warnMinutes)
                                   .arg(limits.blockMinutes));
}

// Spec 8.2.5.1, repeated every second by the link.
void MainWindow::publishStatus() {
    const f20app::AppState state = state_.state();
    const bool usable = state == f20app::AppState::Ready || state == f20app::AppState::Measuring;
    const auto baseline = baseline_.status();
    const auto age = baseline_.ageMinutes();
    const f20::json baselineInfo{
        {"valid", usable && (baseline == f20app::BaselineStatus::Fresh ||
                             baseline == f20app::BaselineStatus::Aging)},
        {"age_minutes", age.value_or(0)}}; // never null on the wire (spec 6.6.3)
    const QDateTime midnightUtc = QDateTime(QDate::currentDate(), QTime(0, 0)).toUTC();
    const f20::json status{
        {"software_version", QCoreApplication::applicationVersion().toStdString()},
        {"bridge_version", bridgeOnlyVersion_.toStdString()},
        {"filmeasure_version", filmeasureVersion_.toStdString()},
        {"machine_status", f20app::machineStatus(state)},
        {"recipe_name", limitsRecipe_.toStdString()},
        {"baseline", baselineInfo},
        {"warm_up_left_minutes", warmUp_.minutesLeft()},
        {"processed_today", storage_.countResultsSince(midnightUtc)},
        {"error", state == f20app::AppState::Fault ? "bridgeFault" : ""}};
    serverLink_->publishStatus(status);
}
