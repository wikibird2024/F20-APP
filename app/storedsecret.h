#pragma once
#include <QString>

// Passwords in the settings file (MQTT login, client key), as in the C#
// app's StoredSecret: on Windows "dpapi:<base64>", encrypted with DPAPI for
// this PC (machine scope - the app runs for any Windows user), so a copied
// file is useless on another PC. A local administrator can still read it.
// Elsewhere (Linux development) the text stays plain.
namespace StoredSecret
{

QString protect(const QString &plain);

// Plain text as is; "dpapi:..." decrypted. A value this PC cannot decrypt
// (another PC, or not Windows) gives "" and *readable = false.
QString unprotect(const QString &stored, bool *readable = nullptr);

} // namespace StoredSecret
