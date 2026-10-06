#pragma once
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Diagnostics screen (spec §7.4): bridge/FILMeasure state, signal health,
// protocol log tail, bridge reconnect button.
class DiagnosticsScreen : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsScreen(QWidget* parent = nullptr);

    void setBridgeState(const QString& text, bool healthy);
    void setVersions(const QString& text);
    void setSignalHealth(int referenceCounts, int backgroundCounts);
    void appendLog(const QString& line);

signals:
    void refreshRequested();
    void restartBridgeRequested();

private:
    QLabel* bridgeState_;
    QLabel* versions_;
    QLabel* signalHealth_;
    QPlainTextEdit* log_;
};
