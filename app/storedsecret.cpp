#include "storedsecret.h"

#include <QByteArray>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#endif

namespace
{

const QString kPrefix = "dpapi:";

#ifdef Q_OS_WIN
// Extra input to DPAPI: another program using DPAPI on this PC cannot
// decrypt our values by accident. Same bytes as the C# app's entropy is
// not needed - the two apps keep their own files.
const QByteArray kEntropy = "F20 control settings";

DATA_BLOB blobOf(const QByteArray &bytes)
{
    DATA_BLOB blob;
    blob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(bytes.constData()));
    blob.cbData = static_cast<DWORD>(bytes.size());
    return blob;
}
#endif

} // namespace

namespace StoredSecret
{

QString protect(const QString &plain)
{
#ifdef Q_OS_WIN
    if (plain.isEmpty())
        return plain;
    const QByteArray bytes = plain.toUtf8();
    DATA_BLOB        in = blobOf(bytes);
    DATA_BLOB        entropy = blobOf(kEntropy);
    DATA_BLOB        out{};
    if (!CryptProtectData(
            &in, L"F20 settings", &entropy, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE | CRYPTPROTECT_UI_FORBIDDEN, &out))
        return plain; // never lose the value; it is stored plain instead
    const QByteArray encrypted(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return kPrefix + QString::fromLatin1(encrypted.toBase64());
#else
    return plain;
#endif
}

QString unprotect(const QString &stored, bool *readable)
{
    if (readable)
        *readable = true;
    if (!stored.startsWith(kPrefix))
        return stored;
#ifdef Q_OS_WIN
    const QByteArray encrypted = QByteArray::fromBase64(stored.mid(kPrefix.size()).toLatin1());
    DATA_BLOB        in = blobOf(encrypted);
    DATA_BLOB        entropy = blobOf(kEntropy);
    DATA_BLOB        out{};
    if (CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        const QString plain = QString::fromUtf8(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
        LocalFree(out.pbData);
        return plain;
    }
#endif
    if (readable)
        *readable = false;
    return {};
}

} // namespace StoredSecret
