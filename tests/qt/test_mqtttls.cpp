// MQTT over TLS end to end (spec 6.6.7): the test makes its own CA and
// certificates with the openssl tool and starts its own Mosquitto with two
// TLS listeners - one plain TLS, one that requires a client certificate.
// Each case goes through testBroker(), the Settings screen's Test
// connection, so the sentences the engineer reads are checked too.
// Skipped when openssl or mosquitto is not installed.
#include "brokertest.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace
{

quint16 freePort()
{
    QTcpServer server;
    server.listen(QHostAddress::LocalHost, 0);
    return server.serverPort();
}

bool portIsOpen(quint16 port)
{
    QTcpSocket probe;
    probe.connectToHost("127.0.0.1", port);
    return probe.waitForConnected(200);
}

} // namespace

class MqttTlsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir dir_;
    QProcess      broker_;
    quint16       tlsPort_ = 0;
    quint16       mutualPort_ = 0;
    QString       skipReason_;

    bool openssl(const QStringList &arguments)
    {
        QProcess tool;
        tool.setWorkingDirectory(dir_.path());
        tool.start("openssl", arguments);
        return tool.waitForFinished(20000) && tool.exitCode() == 0;
    }

    // A CA, a certificate signed by it for the name localhost (not for the
    // address 127.0.0.1 - that is the host-name check case).
    bool makeCertificates()
    {
        QFile ext(dir_.filePath("server.ext"));
        if (!ext.open(QIODevice::WriteOnly))
            return false;
        ext.write("subjectAltName=DNS:localhost\n");
        ext.close();
        const auto newCa = [this](const QString &name) {
            return openssl({"req",
                            "-x509",
                            "-newkey",
                            "rsa:2048",
                            "-nodes",
                            "-days",
                            "2",
                            "-subj",
                            "/CN=" + name,
                            "-keyout",
                            name + ".key",
                            "-out",
                            name + ".crt"});
        };
        const auto signedBy = [this](const QString &name, const QString &extFile) {
            QStringList sign = {"x509",
                                "-req",
                                "-in",
                                name + ".csr",
                                "-CA",
                                "ca.crt",
                                "-CAkey",
                                "ca.key",
                                "-CAcreateserial",
                                "-days",
                                "2",
                                "-out",
                                name + ".crt"};
            if (!extFile.isEmpty())
                sign << "-extfile" << extFile;
            return openssl({"req",
                            "-newkey",
                            "rsa:2048",
                            "-nodes",
                            "-subj",
                            "/CN=" + name,
                            "-keyout",
                            name + ".key",
                            "-out",
                            name + ".csr"}) &&
                   openssl(sign);
        };
        return newCa("ca") && newCa("otherca") && signedBy("server", "server.ext") && signedBy("client", {}) &&
               openssl({"pkey", "-in", "client.key", "-aes256", "-passout", "pass:keypass", "-out", "client-locked.key"});
    }

    // The sentence Test connection shows. Shared: if the wait gives up,
    // a late answer must not write into a finished test.
    QString testConnection(const AppSettings &settings)
    {
        auto sentence = std::make_shared<QString>();
        testBroker(settings, this, [sentence](const QString &text) { *sentence = text; });
        const bool answered = QTest::qWaitFor([&] { return !sentence->isEmpty(); }, 10000);
        qInfo().noquote() << "Test connection says:" << *sentence;
        return answered ? *sentence : QString("(no answer within 10 s)");
    }

    AppSettings tlsSettings(quint16 port) const
    {
        AppSettings settings;
        settings.folder = dir_.path();
        settings.deviceSerial = "F20:SIM001";
        settings.mqttBroker = "localhost";
        settings.mqttPort = port;
        settings.mqttUseTls = true;
        settings.mqttCaFile = "ca.crt"; // relative: resolved against folder
        return settings;
    }

  private slots:
    void initTestCase()
    {
        const QString mosquitto = QStandardPaths::findExecutable("mosquitto").isEmpty()
                                      ? QStandardPaths::findExecutable("mosquitto", {"/usr/sbin", "/usr/local/sbin"})
                                      : QStandardPaths::findExecutable("mosquitto");
        if (mosquitto.isEmpty() || QStandardPaths::findExecutable("openssl").isEmpty()) {
            skipReason_ = "needs mosquitto and openssl";
            return;
        }
        if (!makeCertificates()) {
            skipReason_ = "openssl could not make the test certificates";
            return;
        }
        tlsPort_ = freePort();
        mutualPort_ = freePort();
        QFile config(dir_.filePath("mosquitto.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        const QString tls = "allow_anonymous true\ncafile %1/ca.crt\ncertfile %1/server.crt\nkeyfile %1/server.key\n";
        config.write(QString("per_listener_settings true\n"
                             "listener %1\n%3"
                             "listener %2\n%3require_certificate true\n")
                         .arg(tlsPort_)
                         .arg(mutualPort_)
                         .arg(tls.arg(dir_.path()))
                         .toUtf8());
        config.close();
        broker_.setProcessChannelMode(QProcess::MergedChannels);
        broker_.start(mosquitto, {"-c", config.fileName()});
        QTRY_VERIFY_WITH_TIMEOUT(portIsOpen(tlsPort_) && portIsOpen(mutualPort_), 5000);
    }

    void cleanupTestCase()
    {
        if (broker_.state() != QProcess::Running)
            qWarning().noquote() << "test broker stopped early:" << broker_.readAll();
        broker_.kill();
        broker_.waitForFinished(2000);
    }

    void init()
    {
        if (!skipReason_.isEmpty())
            QSKIP(qPrintable(skipReason_));
    }

    void theCompanyCaIsTrusted()
    {
        QCOMPARE(testConnection(tlsSettings(tlsPort_)),
                 QString("Connected to localhost:%1 (TLS) - the broker works.").arg(tlsPort_));
    }

    void anotherCaIsRefused()
    {
        AppSettings settings = tlsSettings(tlsPort_);
        settings.mqttCaFile = "otherca.crt";
        QVERIFY(testConnection(settings).contains("certificate is not signed by the CA in"));
    }

    // The certificate is for "localhost": the same broker by address fails.
    void theHostNameIsChecked()
    {
        AppSettings settings = tlsSettings(tlsPort_);
        settings.mqttBroker = "127.0.0.1";
        QVERIFY(testConnection(settings).contains("certificate is not for the name '127.0.0.1'"));
    }

    void plainMqttOnTheTlsPortFails()
    {
        AppSettings settings = tlsSettings(tlsPort_);
        settings.mqttUseTls = false;
        settings.mqttCaFile.clear();
        QVERIFY(!testConnection(settings).startsWith("Connected"));
    }

    void mutualTlsNeedsTheClientCertificate()
    {
        AppSettings settings = tlsSettings(mutualPort_);
        QVERIFY(testConnection(settings).contains("client certificate"));

        settings.mqttClientCertFile = "client.crt";
        settings.mqttClientKeyFile = "client.key";
        QVERIFY(testConnection(settings).startsWith("Connected"));

        settings.mqttClientKeyFile = "client-locked.key";
        settings.mqttClientKeyPassword = "keypass";
        QVERIFY(testConnection(settings).startsWith("Connected"));
    }

    void mqtt5OverTls()
    {
        AppSettings settings = tlsSettings(tlsPort_);
        settings.mqttVersion = 500;
        QVERIFY(testConnection(settings).startsWith("Connected"));
    }

    void noBrokerNoTest()
    {
        AppSettings settings;
        QVERIFY(testConnection(settings).startsWith("No broker set"));
    }
};

int runMqttTlsTests(int argc, char **argv)
{
    MqttTlsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_mqtttls.moc"
