#include "logretention.h"

QStringList deleteOldLogs(const QDir& dir, const QDate& today, int keepDays) {
    QStringList deleted;
    if (keepDays <= 0)
        return deleted;
    const QDate oldestKept = today.addDays(-keepDays);
    const QStringList names = dir.entryList({"f20app_*.log"}, QDir::Files);
    for (const QString& name : names) {
        // "f20app_" + yyyy-MM-dd + ".log"; anything else is not ours.
        const QDate date = QDate::fromString(name.mid(7, 10), "yyyy-MM-dd");
        if (!date.isValid() || name.size() != 21 || date >= oldestKept)
            continue;
        if (QFile::remove(dir.filePath(name)))
            deleted << name;
    }
    return deleted;
}
