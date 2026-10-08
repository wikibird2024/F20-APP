// SettingsScreen driven like an engineer would (spec §6.7): first password,
// wrong passwords, Save with a problem, Save, the TLS switch. Runs on the
// offscreen platform. With F20_SCREENSHOT_DIR set, it also saves pictures
// of the screen there (for reviews and the docs).
#include "settingsscreen.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>

namespace
{

struct Folder {
    QTemporaryDir dir;
    QString       defaults = dir.filePath("f20.ini");
    QString       changes = dir.filePath("f20app.ini");
    Folder()
    {
        QFile file(defaults);
        if (file.open(QIODevice::WriteOnly))
            file.write("[device]\nserial=F20:SIM001\n[recipes]\nfolder=recipes\n");
        QDir(dir.path()).mkpath("recipes");
    }
};

template <typename T> T *child(QWidget &screen, const char *name)
{
    T *widget = screen.findChild<T *>(name);
    if (!widget)
        qFatal("no widget named %s", name);
    return widget;
}

QPushButton *button(QWidget &screen, const QString &text)
{
    for (QPushButton *candidate : screen.findChildren<QPushButton *>())
        if (candidate->text() == text)
            return candidate;
    qFatal("no button %s", qPrintable(text));
}

void click(QWidget &screen, const QString &text)
{
    QTest::mouseClick(button(screen, text), Qt::LeftButton);
}

void screenshot(QWidget &screen, const QString &name)
{
    const QString folder = qEnvironmentVariable("F20_SCREENSHOT_DIR");
    if (!folder.isEmpty())
        screen.grab().save(QDir(folder).filePath(name + ".png"));
}

// A screen with its engineer password "1234" already set.
void setFirstPassword(SettingsScreen &screen)
{
    child<QLineEdit>(screen, "newPassword")->setText("1234");
    child<QLineEdit>(screen, "repeatPassword")->setText("1234");
    click(screen, "Set password");
}

} // namespace

class SettingsScreenTest : public QObject
{
    Q_OBJECT
  private slots:
    void firstUseChoosesThePassword()
    {
        Folder         folder;
        SettingsScreen screen(folder.defaults, folder.changes);
        screen.resize(900, 1000);
        screenshot(screen, "settings-first-use");
        QVERIFY(child<QLineEdit>(screen, "newPassword")->isVisibleTo(&screen));

        child<QLineEdit>(screen, "newPassword")->setText("12");
        child<QLineEdit>(screen, "repeatPassword")->setText("12");
        click(screen, "Set password");
        QVERIFY(child<QLabel>(screen, "lockMessage")->text().contains("at least 4"));

        setFirstPassword(screen);
        QVERIFY(child<QLineEdit>(screen, "broker")->isVisibleTo(&screen)); // the form is open
        QVERIFY(QSettings(folder.changes, QSettings::IniFormat)
                    .value("security/engineerPasswordHash")
                    .toString()
                    .startsWith("pbkdf2$600000$"));
        screenshot(screen, "settings-form");
    }

    void wrongPasswordsMakeTheNextTryWait()
    {
        Folder folder;
        {
            SettingsScreen first(folder.defaults, folder.changes);
            setFirstPassword(first);
        }
        SettingsScreen screen(folder.defaults, folder.changes);
        QVERIFY(child<QLineEdit>(screen, "passwordInput")->isVisibleTo(&screen));
        for (int i = 0; i < 5; ++i) {
            child<QLineEdit>(screen, "passwordInput")->setText("wrong");
            click(screen, "Unlock");
        }
        QVERIFY(child<QLabel>(screen, "lockMessage")->text().contains("wait 30 s"));
        child<QLineEdit>(screen, "passwordInput")->setText("1234"); // right, but too soon
        click(screen, "Unlock");
        QVERIFY(child<QLabel>(screen, "lockMessage")->text().contains("try again in"));
        QVERIFY(!child<QLineEdit>(screen, "broker")->isVisibleTo(&screen));
    }

    void saveRefusesProblemsThenSavesOnlyTheChanges()
    {
        Folder         folder;
        SettingsScreen screen(folder.defaults, folder.changes);
        screen.resize(900, 1000);
        QSignalSpy saved(&screen, &SettingsScreen::saved);
        setFirstPassword(screen);

        child<QSpinBox>(screen, "warnMinutes")->setValue(40); // more than block (30)
        child<QLineEdit>(screen, "serial")->clear();
        click(screen, "Save");
        QVERIFY(child<QLabel>(screen, "status")->text().startsWith("Not saved"));
        QVERIFY(child<QLabel>(screen, "problems")->text().contains("serial number"));
        QVERIFY(child<QLabel>(screen, "problems")->text().contains("Baseline"));
        screenshot(screen, "settings-problems");
        QCOMPARE(saved.count(), 0);

        child<QSpinBox>(screen, "warnMinutes")->setValue(25);
        child<QLineEdit>(screen, "serial")->setText("F20:SIM001");
        child<QLineEdit>(screen, "broker")->setText("broker.plant");
        click(screen, "Save");
        QCOMPARE(child<QLabel>(screen, "status")->text(), QString("Saved. Restart the app to use the new settings."));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved.first().first().toStringList(), QStringList({"mqtt/broker", "baseline/warnMinutes"}));
        QVERIFY(button(screen, "Restart the app now")->isVisibleTo(&screen));
        QCOMPARE(QSettings(folder.changes, QSettings::IniFormat).value("mqtt/broker").toString(), QString("broker.plant"));
        screenshot(screen, "settings-saved");

        QSignalSpy restart(&screen, &SettingsScreen::restartRequested);
        click(screen, "Restart the app now");
        QCOMPARE(restart.count(), 1);
    }

    void tlsShowsItsRowsAndMovesTheDefaultPort()
    {
        Folder         folder;
        SettingsScreen screen(folder.defaults, folder.changes);
        screen.resize(900, 1100);
        setFirstPassword(screen);
        QVERIFY(!child<QLineEdit>(screen, "caFile")->isVisibleTo(&screen));
        QCOMPARE(child<QSpinBox>(screen, "port")->value(), 1883);

        child<QCheckBox>(screen, "useTls")->setChecked(true);
        QVERIFY(child<QLineEdit>(screen, "caFile")->isVisibleTo(&screen));
        QCOMPARE(child<QSpinBox>(screen, "port")->value(), 8883);
        child<QLineEdit>(screen, "broker")->setText("broker.plant");
        click(screen, "Test connection");
        QVERIFY(child<QLabel>(screen, "testResult")->text().startsWith("Fix first: TLS is on"));
        screenshot(screen, "settings-tls");

        child<QSpinBox>(screen, "port")->setValue(9000); // IT's own port stays
        child<QCheckBox>(screen, "useTls")->setChecked(false);
        QCOMPARE(child<QSpinBox>(screen, "port")->value(), 9000);
        QVERIFY(child<QLabel>(screen, "topics")->text().contains("SIM001/ar/f20/send"));
    }

    void leavingTheTabLocks()
    {
        Folder         folder;
        SettingsScreen screen(folder.defaults, folder.changes);
        setFirstPassword(screen);
        screen.lock();
        QVERIFY(child<QLineEdit>(screen, "passwordInput")->isVisibleTo(&screen));
        QVERIFY(child<QLineEdit>(screen, "passwordInput")->text().isEmpty());
    }
};

int runSettingsScreenTests(int argc, char **argv)
{
    SettingsScreenTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_settingsscreen.moc"
