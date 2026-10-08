#include "settingsscreen.h"

#include "brokertest.h"
#include "engineerpassword.h"

#include <f20/envelope.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QtDebug>

namespace
{

constexpr int kPlainPort = 1883;
constexpr int kTlsPort = 8883;

QLabel *hint(const QString &text)
{
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet("color: #6e7781;");
    return label;
}

QSpinBox *numberBox(int min, int max)
{
    auto *box = new QSpinBox;
    box->setRange(min, max);
    return box;
}

QLineEdit *passwordBox(const QString &placeholder = {})
{
    auto *edit = new QLineEdit;
    edit->setEchoMode(QLineEdit::Password);
    edit->setPlaceholderText(placeholder);
    return edit;
}

f20app::UnlockGuard::Clock::time_point now()
{
    return f20app::UnlockGuard::Clock::now();
}

} // namespace

SettingsScreen::SettingsScreen(const QString &defaultsPath, const QString &changesPath, QWidget *parent)
    : QWidget(parent), defaultsPath_(defaultsPath), changesPath_(changesPath),
      current_(AppSettings::load(defaultsPath, changesPath))
{
    pages_ = new QStackedWidget;
    pages_->addWidget(buildLockPage());
    pages_->addWidget(buildForm());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pages_);

    // Idle lock: checked every 30 s; input on the form counts as activity
    // (eventFilter, installed on the whole app because a parent widget does
    // not see its children's key presses).
    idleTimer_.setInterval(30 * 1000);
    connect(&idleTimer_, &QTimer::timeout, this, [this] {
        if (pages_->currentIndex() == 1 && !guard_.isUnlocked(now()))
            showLockPage("Locked after 10 minutes without input.");
    });
    idleTimer_.start();
    qApp->installEventFilter(this);

    showLockPage();
}

// ---- lock page ----

QWidget *SettingsScreen::buildLockPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *title = new QLabel("Settings are for engineers. Unlock with the engineer password.");
    title->setStyleSheet("font-weight: bold;");
    layout->addWidget(title);

    unlockBox_ = new QWidget;
    auto *unlockLayout = new QHBoxLayout(unlockBox_);
    unlockLayout->setContentsMargins(0, 0, 0, 0);
    passwordInput_ = passwordBox("engineer password");
    passwordInput_->setObjectName("passwordInput");
    auto *unlockButton = new QPushButton("Unlock");
    unlockLayout->addWidget(passwordInput_);
    unlockLayout->addWidget(unlockButton);
    unlockLayout->addStretch();
    layout->addWidget(unlockBox_);

    firstUseBox_ = new QWidget;
    auto *firstUse = new QFormLayout(firstUseBox_);
    firstUse->setContentsMargins(0, 0, 0, 0);
    firstUse->addRow(
        hint(QString("No engineer password yet. Choose one (at least %1 characters):").arg(EngineerPassword::kMinLength)));
    newPassword_ = passwordBox();
    newPassword_->setObjectName("newPassword");
    repeatPassword_ = passwordBox();
    repeatPassword_->setObjectName("repeatPassword");
    auto *setButton = new QPushButton("Set password");
    firstUse->addRow("New password", newPassword_);
    firstUse->addRow("Repeat it", repeatPassword_);
    firstUse->addRow(setButton);
    layout->addWidget(firstUseBox_);

    lockMessage_ = new QLabel;
    lockMessage_->setObjectName("lockMessage");
    lockMessage_->setStyleSheet("color: #c62828;");
    layout->addWidget(lockMessage_);
    layout->addStretch();

    connect(unlockButton, &QPushButton::clicked, this, &SettingsScreen::unlock);
    connect(passwordInput_, &QLineEdit::returnPressed, this, &SettingsScreen::unlock);
    connect(setButton, &QPushButton::clicked, this, &SettingsScreen::setPassword);
    connect(repeatPassword_, &QLineEdit::returnPressed, this, &SettingsScreen::setPassword);
    return page;
}

void SettingsScreen::showLockPage(const QString &message)
{
    const bool hasPassword = !current_.engineerPasswordHash.isEmpty();
    unlockBox_->setVisible(hasPassword);
    firstUseBox_->setVisible(!hasPassword);
    passwordInput_->clear();
    newPassword_->clear();
    repeatPassword_->clear();
    lockMessage_->setText(message);
    pages_->setCurrentIndex(0);
}

void SettingsScreen::lock()
{
    guard_.lock();
    if (pages_->currentIndex() == 1)
        showLockPage();
}

void SettingsScreen::unlock()
{
    if (const int wait = guard_.waitSeconds(now()); wait > 0) {
        lockMessage_->setText(QString("Too many wrong passwords - try again in %1 s.").arg(wait));
        return;
    }
    // PBKDF2 with 600 000 rounds takes a moment on purpose: slow guessing.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool isRight = EngineerPassword::verify(passwordInput_->text(), current_.engineerPasswordHash);
    QApplication::restoreOverrideCursor();
    passwordInput_->clear();
    if (!isRight) {
        guard_.wrongPassword(now());
        const int wait = guard_.waitSeconds(now());
        lockMessage_->setText(wait > 0 ? QString("Wrong password. Too many tries - wait %1 s.").arg(wait)
                                       : QString("Wrong password."));
        qWarning().noquote() << "[settings] wrong engineer password";
        return;
    }
    guard_.unlocked(now());
    showValues();
    pages_->setCurrentIndex(1);
}

// First use: the password is chosen and saved at once (only its hash).
void SettingsScreen::setPassword()
{
    if (!current_.engineerPasswordHash.isEmpty())
        return; // only on first use; the button is hidden otherwise
    if (newPassword_->text().size() < EngineerPassword::kMinLength) {
        lockMessage_->setText(QString("The password needs at least %1 characters.").arg(EngineerPassword::kMinLength));
        return;
    }
    if (newPassword_->text() != repeatPassword_->text()) {
        lockMessage_->setText("The two passwords are not the same.");
        return;
    }
    AppSettings updated = current_;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    updated.engineerPasswordHash = EngineerPassword::hash(newPassword_->text());
    QApplication::restoreOverrideCursor();
    QString error;
    if (!updated.save(defaultsPath_, changesPath_, &error)) {
        lockMessage_->setText("Password not saved: " + error);
        return;
    }
    current_ = updated;
    qInfo().noquote() << "[settings] engineer password set";
    guard_.unlocked(now());
    showValues();
    pages_->setCurrentIndex(1);
}

bool SettingsScreen::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress) && pages_->currentIndex() == 1) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && isAncestorOf(widget))
            guard_.touched(now());
    }
    return QWidget::eventFilter(watched, event);
}

// ---- form ----

QWidget *SettingsScreen::withBrowseButton(QLineEdit *edit, const QString &title, bool isFolder)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *browse = new QPushButton("Browse...");
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, title, isFolder] {
        const QString start = current_.resolve(edit->text()).isEmpty() ? current_.folder : current_.resolve(edit->text());
        const QString picked = isFolder
                                   ? QFileDialog::getExistingDirectory(this, title, start)
                                   : QFileDialog::getOpenFileName(
                                         this, title, start, "Certificates and keys (*.crt *.pem *.cer *.key);;All files (*)");
        if (!picked.isEmpty())
            edit->setText(QDir::toNativeSeparators(picked));
    });
    return row;
}

QWidget *SettingsScreen::buildForm()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);

    auto *header = new QHBoxLayout;
    header->addWidget(hint("Saved to " + QDir::toNativeSeparators(changesPath_) + ". New values are used after a restart."), 1);
    auto *lockButton = new QPushButton("Lock");
    header->addWidget(lockButton);
    layout->addLayout(header);

    // Server (MQTT broker)
    auto *server = new QGroupBox("Server (MQTT broker)");
    serverForm_ = new QFormLayout(server);
    broker_ = new QLineEdit;
    broker_->setObjectName("broker");
    broker_->setPlaceholderText("empty = no server (messages are only logged)");
    port_ = numberBox(1, 65535);
    port_->setObjectName("port");
    username_ = new QLineEdit;
    username_->setPlaceholderText("empty = no login");
    password_ = passwordBox();
    useTls_ = new QCheckBox("Encrypted connection (TLS)");
    useTls_->setObjectName("useTls");
    caFile_ = new QLineEdit;
    caFile_->setObjectName("caFile");
    caFile_->setPlaceholderText("the company CA file (.crt / .pem)");
    clientCertFile_ = new QLineEdit;
    clientCertFile_->setPlaceholderText("empty = none; else this machine's certificate (PEM)");
    clientKeyFile_ = new QLineEdit;
    clientKeyFile_->setPlaceholderText("empty = the key is inside the certificate file");
    clientKeyPassword_ = passwordBox("empty = key not encrypted");
    clientId_ = new QLineEdit;
    mqttVersion_ = new QComboBox;
    mqttVersion_->addItem("MQTT 3.1.1 (most brokers)", 311);
    mqttVersion_->addItem("MQTT 5", 500);
    testButton_ = new QPushButton("Test connection");
    testResult_ = new QLabel;
    testResult_->setObjectName("testResult");
    testResult_->setWordWrap(true);
    testResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    caRow_ = withBrowseButton(caFile_, "CA certificate", false);
    clientCertRow_ = withBrowseButton(clientCertFile_, "Client certificate", false);
    clientKeyRow_ = withBrowseButton(clientKeyFile_, "Client key", false);
    serverForm_->addRow("Broker address", broker_);
    serverForm_->addRow("Port", port_);
    serverForm_->addRow("User name", username_);
    serverForm_->addRow("Password", password_);
    serverForm_->addRow("", useTls_);
    serverForm_->addRow("CA certificate", caRow_);
    serverForm_->addRow("Client certificate", clientCertRow_);
    serverForm_->addRow("Client key", clientKeyRow_);
    serverForm_->addRow("Key password", clientKeyPassword_);
    serverForm_->addRow("Client ID", clientId_);
    serverForm_->addRow("MQTT version", mqttVersion_);
    serverForm_->addRow(testButton_, testResult_);
    layout->addWidget(server);

    // Device
    auto *device = new QGroupBox("Device");
    auto *deviceForm = new QFormLayout(device);
    serial_ = new QLineEdit;
    serial_->setObjectName("serial");
    serial_->setPlaceholderText("e.g. F20:09A006, as the bridge reports it");
    topics_ = hint({});
    topics_->setObjectName("topics");
    deviceForm->addRow("F20 serial number", serial_);
    deviceForm->addRow("", topics_);
    layout->addWidget(device);

    // Bridge
    auto *bridge = new QGroupBox("Bridge");
    auto *bridgeForm = new QFormLayout(bridge);
    bridgePort_ = numberBox(1, 65535);
    bridgeForm->addRow("Port", bridgePort_);
    layout->addWidget(bridge);

    // Recipes
    auto *recipes = new QGroupBox("Recipes");
    auto *recipesForm = new QFormLayout(recipes);
    recipesFolder_ = new QLineEdit;
    auto *reload = new QPushButton("Reload recipes");
    reload->setToolTip("Reads the recipe list again from the folder in use (a new folder is used after a restart)");
    recipesForm->addRow("Recipes folder", withBrowseButton(recipesFolder_, "Recipes folder", true));
    recipesForm->addRow("", reload);
    layout->addWidget(recipes);

    // Baseline
    auto *baseline = new QGroupBox("Baseline (for recipes without their own limits)");
    auto *baselineForm = new QFormLayout(baseline);
    warnMinutes_ = numberBox(1, 1440);
    warnMinutes_->setObjectName("warnMinutes");
    blockMinutes_ = numberBox(1, 1440);
    blockMinutes_->setObjectName("blockMinutes");
    warmUpMinutes_ = numberBox(0, 240);
    for (QSpinBox *box : {warnMinutes_, blockMinutes_, warmUpMinutes_})
        box->setSuffix(" min");
    baselineForm->addRow("Warn after", warnMinutes_);
    baselineForm->addRow("Block measuring after", blockMinutes_);
    baselineForm->addRow("Lamp warm-up", warmUpMinutes_);
    layout->addWidget(baseline);

    // Save row and feedback
    auto *buttons = new QHBoxLayout;
    auto *saveButton = new QPushButton("Save");
    saveButton->setStyleSheet("font-weight: bold;");
    auto *undoButton = new QPushButton("Undo changes");
    restartButton_ = new QPushButton("Restart the app now");
    restartButton_->hide();
    buttons->addWidget(saveButton);
    buttons->addWidget(undoButton);
    buttons->addWidget(restartButton_);
    buttons->addStretch();
    layout->addLayout(buttons);
    status_ = new QLabel;
    status_->setObjectName("status");
    status_->setStyleSheet("font-weight: bold;");
    problems_ = new QLabel;
    problems_->setObjectName("problems");
    problems_->setWordWrap(true);
    problems_->setStyleSheet("color: #c62828;");
    layout->addWidget(status_);
    layout->addWidget(problems_);
    layout->addStretch();

    connect(lockButton, &QPushButton::clicked, this, &SettingsScreen::lock);
    connect(useTls_, &QCheckBox::toggled, this, &SettingsScreen::tlsToggled);
    connect(serial_, &QLineEdit::textChanged, this, &SettingsScreen::updateSerialHints);
    connect(testButton_, &QPushButton::clicked, this, &SettingsScreen::testConnection);
    connect(reload, &QPushButton::clicked, this, &SettingsScreen::reloadRecipesRequested);
    connect(saveButton, &QPushButton::clicked, this, &SettingsScreen::save);
    connect(undoButton, &QPushButton::clicked, this, &SettingsScreen::showValues);
    connect(restartButton_, &QPushButton::clicked, this, &SettingsScreen::restartRequested);

    // Many fields for a 720 px high screen: the form scrolls.
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    return scroll;
}

// The fields from the files (also "Undo changes").
void SettingsScreen::showValues()
{
    const AppSettings &s = current_;
    broker_->setText(s.mqttBroker);
    useTls_->setChecked(s.mqttUseTls);
    port_->setValue(s.mqttPort); // after the TLS box: ticking it may have moved the port
    username_->setText(s.mqttUsername);
    password_->setText(s.mqttPassword);
    caFile_->setText(s.mqttCaFile);
    clientCertFile_->setText(s.mqttClientCertFile);
    clientKeyFile_->setText(s.mqttClientKeyFile);
    clientKeyPassword_->setText(s.mqttClientKeyPassword);
    clientId_->setText(s.mqttClientId);
    mqttVersion_->setCurrentIndex(std::max(0, mqttVersion_->findData(s.mqttVersion)));
    serial_->setText(s.deviceSerial);
    bridgePort_->setValue(s.bridgePort);
    recipesFolder_->setText(s.recipesFolder);
    warnMinutes_->setValue(s.baselineWarnMinutes);
    blockMinutes_->setValue(s.baselineBlockMinutes);
    warmUpMinutes_->setValue(s.warmUpMinutes);
    updateTlsRows();
    updateSerialHints();
    testResult_->clear();
    status_->clear();
    problems_->clear();
}

// The settings the fields describe; checked by problems() before Save.
AppSettings SettingsScreen::candidate() const
{
    AppSettings s = current_;
    s.mqttBroker = broker_->text().trimmed();
    s.mqttPort = port_->value();
    s.mqttUsername = username_->text().trimmed();
    s.mqttPassword = password_->text();
    s.mqttUseTls = useTls_->isChecked();
    s.mqttCaFile = caFile_->text().trimmed();
    s.mqttClientCertFile = clientCertFile_->text().trimmed();
    s.mqttClientKeyFile = clientKeyFile_->text().trimmed();
    s.mqttClientKeyPassword = clientKeyPassword_->text();
    s.mqttClientId = clientId_->text().trimmed();
    s.mqttVersion = mqttVersion_->currentData().toInt();
    s.deviceSerial = serial_->text().trimmed();
    s.bridgePort = bridgePort_->value();
    s.recipesFolder = recipesFolder_->text().trimmed();
    s.baselineWarnMinutes = warnMinutes_->value();
    s.baselineBlockMinutes = blockMinutes_->value();
    s.warmUpMinutes = warmUpMinutes_->value();
    return s;
}

void SettingsScreen::save()
{
    if (!guard_.isUnlocked(now())) {
        showLockPage("Locked after 10 minutes without input - nothing was saved.");
        return;
    }
    const AppSettings next = candidate();
    const QStringList found = next.problems();
    if (!found.isEmpty()) {
        status_->setText("Not saved - fix the problems listed below.");
        problems_->setText("• " + found.join("\n• "));
        return;
    }
    problems_->clear();
    const QStringList changed = next.changedKeys(current_);
    if (changed.isEmpty()) {
        status_->setText("Nothing changed.");
        return;
    }
    QString error;
    if (!next.save(defaultsPath_, changesPath_, &error)) {
        status_->setText("Not saved - " + error);
        return;
    }
    current_ = next;
    // Names only, never values: passwords must not reach the log.
    qInfo().noquote() << "[settings] saved, changed:" << changed.join(", ");
    status_->setText("Saved. Restart the app to use the new settings.");
    restartButton_->show();
    emit saved(changed);
}

void SettingsScreen::testConnection()
{
    const AppSettings next = candidate();
    // The same checks as Save: a missing CA file would only give a code.
    QStringList found;
    for (const QString &problem : next.problems())
        if (problem.contains("MQTT") || problem.contains("TLS") || problem.contains("certificate") ||
            problem.contains("key file"))
            found << problem;
    if (!found.isEmpty()) {
        testResult_->setText("Fix first: " + found.join("; "));
        return;
    }
    testButton_->setEnabled(false);
    testResult_->setText(QString("Connecting to %1:%2 ...").arg(next.mqttBroker).arg(next.mqttPort));
    testBroker(next, this, [this](const QString &sentence) {
        testResult_->setText(sentence);
        testButton_->setEnabled(true);
    });
}

// The usual ports: 1883 plain, 8883 TLS. Only a port still at the other
// default moves; a port IT chose stays.
void SettingsScreen::tlsToggled(bool isOn)
{
    if (isOn && port_->value() == kPlainPort)
        port_->setValue(kTlsPort);
    else if (!isOn && port_->value() == kTlsPort)
        port_->setValue(kPlainPort);
    updateTlsRows();
}

void SettingsScreen::updateTlsRows()
{
    const bool isOn = useTls_->isChecked();
    for (QWidget *row : {caRow_, clientCertRow_, clientKeyRow_, static_cast<QWidget *>(clientKeyPassword_)})
        serverForm_->setRowVisible(row, isOn);
}

void SettingsScreen::updateSerialHints()
{
    const std::string serial = serial_->text().trimmed().toStdString();
    topics_->setText(serial.empty() ? QString("No serial: the MQTT topics cannot be named.")
                                    : QString("Publishes to %1, listens on %2")
                                          .arg(QString::fromStdString(f20::sendTopic(serial)),
                                               QString::fromStdString(f20::receiveTopic(serial))));
    clientId_->setPlaceholderText(serial.empty() ? QString("empty = f20-<serial>")
                                                 : "empty = " + candidate().clientIdOrDefault());
}
