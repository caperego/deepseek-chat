#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace PathPolicy {

inline QString canonicalPathForCheck(const QString &path)
{
    QFileInfo info(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    QStringList missing;
    while (!info.exists()) {
        if (info.isSymLink() || info.fileName().isEmpty())
            return QString();
        missing.prepend(info.fileName());
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            return QString();
        info.setFile(parent);
    }
    QString resolved = info.canonicalFilePath();
    if (resolved.isEmpty())
        return QString();
    for (const QString &part : missing)
        resolved = QDir(resolved).filePath(part);
    return QDir::cleanPath(resolved);
}

inline bool insideDir(const QString &workdir, const QString &path)
{
    if (workdir.isEmpty() || path.isEmpty())
        return false;
    const QString base = canonicalPathForCheck(workdir);
    const QString target = canonicalPathForCheck(path);
    if (base.isEmpty() || target.isEmpty())
        return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return target.compare(base, cs) == 0
        || target.startsWith(base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/'), cs);
}

}
