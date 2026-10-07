#include "diagnosticsscreen.h"

#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

DiagnosticsScreen::DiagnosticsScreen(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    bridgeState_ = new QLabel("bridge: unknown");
    versions_ = new QLabel("versions: -");
    signalHealth_ = new QLabel("signal: -");
    layout->addWidget(bridgeState_);
    layout->addWidget(versions_);
    layout->addWidget(signalHealth_);

    auto *buttons = new QHBoxLayout;
    auto *refresh = new QPushButton("Refresh");
    auto *restart = new QPushButton("Reconnect bridge");
    buttons->addWidget(refresh);
    buttons->addWidget(restart);
    buttons->addStretch();
    layout->addLayout(buttons);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(200); // keep the last 200 lines (spec §7.4)
    layout->addWidget(log_, 1);

    connect(refresh, &QPushButton::clicked, this, &DiagnosticsScreen::refreshRequested);
    connect(restart, &QPushButton::clicked, this, &DiagnosticsScreen::restartBridgeRequested);
}

void DiagnosticsScreen::setBridgeState(const QString &text, bool healthy)
{
    bridgeState_->setText("bridge: " + text);
    bridgeState_->setStyleSheet(healthy ? "color: #1a7f37;" : "color: #c62828;");
}

void DiagnosticsScreen::setVersions(const QString &text)
{
    versions_->setText("versions: " + text);
}

void DiagnosticsScreen::setSignalHealth(int referenceCounts, int backgroundCounts)
{
    // Good signal 2500-3500 counts; near 4095 = saturation (spec §7.4).
    QString judgement = "good";
    if (referenceCounts > 3900)
        judgement = "SATURATION";
    else if (referenceCounts < 2500 || referenceCounts > 3500)
        judgement = "check lamp/fiber";
    signalHealth_->setText(
        QString("signal: reference %1, background %2 (%3)").arg(referenceCounts).arg(backgroundCounts).arg(judgement));
}

void DiagnosticsScreen::appendLog(const QString &line)
{
    log_->appendPlainText(line);
}
