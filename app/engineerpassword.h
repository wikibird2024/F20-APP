#pragma once
#include <QString>

// The Settings screen's password (spec §6.7), stored only as a salted hash:
// "pbkdf2$<iterations>$<salt base64>$<hash base64>", PBKDF2-HMAC-SHA256 -
// the same text as the C# app, so a hash made by either app works in the
// other. The settings file never holds the password itself.
//
// 600 000 iterations is OWASP's figure for PBKDF2-HMAC-SHA256 (Password
// Storage Cheat Sheet). The count is part of the stored text, so older
// hashes (the C# app's 100 000) still verify.
namespace EngineerPassword
{

constexpr int kIterations = 600000;
constexpr int kMinLength = 4;

QString hash(const QString &password, int iterations = kIterations);

// Same time for every wrong guess of the same length (no early exit).
bool verify(const QString &password, const QString &stored);

} // namespace EngineerPassword
