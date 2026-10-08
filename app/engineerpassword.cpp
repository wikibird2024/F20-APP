#include "engineerpassword.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QStringList>

namespace
{

constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;

QByteArray derive(const QString &password, const QByteArray &salt, int iterations, int bytes)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(), salt, iterations, bytes);
}

bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char difference = 0;
    for (int i = 0; i < a.size(); ++i)
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    return difference == 0;
}

} // namespace

namespace EngineerPassword
{

QString hash(const QString &password, int iterations)
{
    QByteArray salt(kSaltBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(salt.data()), kSaltBytes / 4);
    const QByteArray key = derive(password, salt, iterations, kHashBytes);
    return QString("pbkdf2$%1$%2$%3")
        .arg(iterations)
        .arg(QString::fromLatin1(salt.toBase64()), QString::fromLatin1(key.toBase64()));
}

bool verify(const QString &password, const QString &stored)
{
    const QStringList parts = stored.split('$');
    if (parts.size() != 4 || parts[0] != "pbkdf2")
        return false;
    bool      isNumber = false;
    const int iterations = parts[1].toInt(&isNumber);
    if (!isNumber || iterations < 1)
        return false;
    const auto salt = QByteArray::fromBase64Encoding(parts[2].toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    const auto expected = QByteArray::fromBase64Encoding(parts[3].toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!salt || !expected || expected.decoded.isEmpty())
        return false;
    return sameBytes(derive(password, salt.decoded, iterations, static_cast<int>(expected.decoded.size())), expected.decoded);
}

} // namespace EngineerPassword
