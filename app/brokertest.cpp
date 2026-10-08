#include "brokertest.h"

#ifdef F20_HAS_MQTT
#include "pahomqtttransport.h"

#include <QDir>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QUuid>

#include <memory>

namespace
{

// One TLS problem in the engineer's words; "" for one we do not explain.
QString describe(const QSslError &error, const MqttTransport::Settings &broker)
{
    switch (error.error()) {
        case QSslError::CertificateUntrusted:
        case QSslError::UnableToGetIssuerCertificate:
        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
            return "the broker's certificate is not signed by the CA in " + QDir::toNativeSeparators(broker.caFile);
        case QSslError::CertificateExpired:
            return "the broker's certificate has expired";
        case QSslError::CertificateNotYetValid:
            return "the broker's certificate is not valid yet - check this PC's date and time";
        case QSslError::HostNameMismatch:
            return "the broker's certificate is not for the name '" + broker.host +
                   "' - use the broker name written in its certificate";
        default:
            return {};
    }
}

// Why a TLS connect failed: Qt's own TLS check with the same CA file and
// host name as the app (Paho gives only a code - see sslOptions() in
// pahomqtttransport.cpp). done gets "" when it finds no reason.
void explainTlsFailure(const MqttTransport::Settings &broker, QObject *context, std::function<void(const QString &)> done)
{
    if (!QSslSocket::supportsSsl()) {
        done({});
        return;
    }
    auto             *socket = new QSslSocket(context);
    QSslConfiguration config = socket->sslConfiguration();
    config.setCaCertificates(QSslCertificate::fromPath(broker.caFile));
    config.setPeerVerifyMode(QSslSocket::VerifyPeer);
    socket->setSslConfiguration(config);

    auto answered = std::make_shared<bool>(false);
    auto answer = [socket, done, answered](const QString &why) {
        if (*answered)
            return;
        *answered = true;
        socket->abort();
        socket->deleteLater();
        done(why);
    };
    auto certificateProblem = std::make_shared<QString>();
    QObject::connect(socket, &QSslSocket::sslErrors, socket, [=](const QList<QSslError> &errors) {
        // The CA problem first: with an unknown CA the name says little.
        for (const QSslError &error : errors)
            if (certificateProblem->isEmpty() || error.error() != QSslError::HostNameMismatch)
                if (const QString text = describe(error, broker); !text.isEmpty())
                    *certificateProblem = text;
    });
    QObject::connect(socket, &QSslSocket::encrypted, socket, [=] {
        // TLS 1.3: a missing client certificate is refused after this point.
        answer(broker.clientCertFile.isEmpty()
                   ? "the TLS check passed, then the broker closed the connection - it probably needs a client "
                     "certificate"
                   : "the TLS check passed - the broker refused the client certificate, its key or key password, "
                     "or the login");
    });
    QObject::connect(socket, &QAbstractSocket::errorOccurred, socket, [=](QAbstractSocket::SocketError error) {
        if (!certificateProblem->isEmpty())
            answer(*certificateProblem);
        else if (error == QAbstractSocket::ConnectionRefusedError)
            answer("nothing listens on that port");
        else if (error == QAbstractSocket::HostNotFoundError)
            answer("the broker name is not known on this network");
        else if (error == QAbstractSocket::SslHandshakeFailedError)
            answer("the TLS handshake failed (" + socket->errorString() +
                   ") - is this the broker's TLS port, does it need a client certificate?");
        else
            answer({});
    });
    QTimer::singleShot(5000, socket, [answer] { answer({}); });
    socket->connectToHostEncrypted(broker.host, broker.port);
}

} // namespace
#endif

MqttTransport::Settings transportSettings(const AppSettings &settings)
{
    MqttTransport::Settings broker;
    broker.host = settings.mqttBroker.trimmed();
    broker.port = static_cast<quint16>(settings.mqttPort);
    broker.clientId = settings.clientIdOrDefault();
    broker.username = settings.mqttUsername.trimmed();
    broker.password = settings.mqttPassword;
    broker.mqttVersion = settings.mqttVersion;
    broker.useTls = settings.mqttUseTls;
    broker.caFile = settings.resolve(settings.mqttCaFile);
    broker.clientCertFile = settings.resolve(settings.mqttClientCertFile);
    broker.clientKeyFile = settings.resolve(settings.mqttClientKeyFile);
    broker.clientKeyPassword = settings.mqttClientKeyPassword;
    return broker;
}

void testBroker(const AppSettings &settings, QObject *context, std::function<void(const QString &)> done)
{
    if (settings.mqttBroker.trimmed().isEmpty()) {
        done("No broker set: the app runs without a server (messages are only logged).");
        return;
    }
#ifdef F20_HAS_MQTT
    MqttTransport::Settings broker = transportSettings(settings);
    broker.clientId = "f20-settings-test-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
    broker.reconnectFirstDelayMs = 60000; // one try: the timeout below ends it first
    const QString where = QString("%1:%2").arg(broker.host).arg(broker.port);
    const QString tls = broker.useTls ? " (TLS)" : "";

    auto *transport = new PahoMqttTransport(context);
    auto *timeout = new QTimer(transport);
    auto  finished = std::make_shared<bool>(false);
    auto  finish = [transport, done, finished](const QString &sentence) {
        if (*finished)
            return;
        *finished = true;
        transport->disconnectFromBroker();
        transport->deleteLater();
        done(sentence);
    };
    QObject::connect(transport, &MqttTransport::connected, transport, [finish, where, tls] {
        finish(QString("Connected to %1%2 - the broker works.").arg(where, tls));
    });
    QObject::connect(transport, &MqttTransport::connectAttemptFailed, transport, [=](const QString &reason) {
        const QString failed = QString("Cannot connect to %1%2: ").arg(where, tls);
        if (!broker.useTls) {
            finish(failed + reason + " - check the address and port (a TLS port needs TLS on)");
            return;
        }
        timeout->stop();
        transport->disconnectFromBroker(); // no retry while the TLS check runs
        explainTlsFailure(
            broker, transport, [finish, failed, reason](const QString &why) { finish(failed + (why.isEmpty() ? reason : why)); });
    });
    timeout->setSingleShot(true);
    QObject::connect(timeout, &QTimer::timeout, transport, [finish, where] {
        finish("No answer from " + where + " within 6 s - check the address, port and firewall.");
    });
    timeout->start(6000);
    transport->connectToBroker(broker);
#else
    Q_UNUSED(context)
    done("This build has no MQTT (Eclipse Paho not found) - nothing to test.");
#endif
}
