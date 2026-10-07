#pragma once
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// Diagnostics screen (spec §7.4): bridge/FILMeasure state, server (MQTT)
// connection, signal health,
// protocol log tail, bridge reconnect button.
class DiagnosticsScreen : public QWidget
{
    Q_OBJECT
  public:
    explicit DiagnosticsScreen(QWidget *parent = nullptr);

    void setBridgeState(const QString &text, bool healthy);
    void setServerState(const QString &text, const QString &color);
    void setServerDetails(const QString &text); // broker address and topics
    void setVersions(const QString &text);
    void setSignalHealth(int referenceCounts, int backgroundCounts);
    void appendLog(const QString &line);

  signals:
    void refreshRequested();
    void restartBridgeRequested();

  private:
    QLabel         *bridgeState_;
    QLabel         *serverState_;
    QLabel         *serverDetails_;
    QLabel         *versions_;
    QLabel         *signalHealth_;
    QPlainTextEdit *log_;
};
