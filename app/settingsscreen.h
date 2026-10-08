#pragma once
#include "appsettings.h"
#include "unlockguard.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

// Settings screen (spec §6.7): the engineer's place to change the settings
// that change on site, without editing files. Locked by the engineer
// password (UnlockGuard: wait after 5 wrong tries, lock again after 10 min
// idle). Save checks every value (AppSettings::problems) and writes only
// the changes; the app uses them after a restart.
class SettingsScreen : public QWidget
{
    Q_OBJECT
  public:
    SettingsScreen(const QString &defaultsPath, const QString &changesPath, QWidget *parent = nullptr);

  public slots:
    void lock(); // MainWindow calls it when the engineer leaves the tab

  signals:
    void saved(const QStringList &changedKeys); // new values wait for a restart
    void restartRequested();
    void reloadRecipesRequested();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    QWidget    *buildLockPage();
    QWidget    *buildForm();
    QWidget    *withBrowseButton(QLineEdit *edit, const QString &title, bool isFolder);
    void        showLockPage(const QString &message = {});
    void        unlock();
    void        setPassword();
    void        showValues();
    AppSettings candidate() const;
    void        save();
    void        testConnection();
    void        tlsToggled(bool isOn);
    void        updateTlsRows();
    void        updateSerialHints();

    QString             defaultsPath_;
    QString             changesPath_;
    AppSettings         current_; // what the files hold now
    f20app::UnlockGuard guard_;
    QTimer              idleTimer_;

    QStackedWidget *pages_;

    // lock page
    QWidget   *unlockBox_;
    QWidget   *firstUseBox_;
    QLineEdit *passwordInput_;
    QLineEdit *newPassword_;
    QLineEdit *repeatPassword_;
    QLabel    *lockMessage_;

    // form
    QFormLayout *serverForm_;
    QLineEdit   *broker_;
    QSpinBox    *port_;
    QLineEdit   *username_;
    QLineEdit   *password_;
    QCheckBox   *useTls_;
    QLineEdit   *caFile_;
    QLineEdit   *clientCertFile_;
    QLineEdit   *clientKeyFile_;
    QLineEdit   *clientKeyPassword_;
    QWidget     *caRow_;
    QWidget     *clientCertRow_;
    QWidget     *clientKeyRow_;
    QLineEdit   *clientId_;
    QComboBox   *mqttVersion_;
    QPushButton *testButton_;
    QLabel      *testResult_;
    QLineEdit   *serial_;
    QLabel      *topics_;
    QSpinBox    *bridgePort_;
    QLineEdit   *recipesFolder_;
    QSpinBox    *warnMinutes_;
    QSpinBox    *blockMinutes_;
    QSpinBox    *warmUpMinutes_;
    QPushButton *restartButton_;
    QLabel      *status_;
    QLabel      *problems_;
};
