#pragma once
#include <QDate>
#include <QDir>
#include <QStringList>

// Old log files are deleted automatically, so the NUC disk never fills
// (spec 2.5, open item 13). Deletes f20app_<yyyy-MM-dd>.log files whose
// date - read from the NAME, not the file time - is more than keepDays
// before today. Other files are never touched; keepDays <= 0 keeps
// everything. Returns the names of the deleted files.
QStringList deleteOldLogs(const QDir& dir, const QDate& today, int keepDays);
