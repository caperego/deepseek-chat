#pragma once

#include "chattypes.h"

#include <QByteArray>
#include <QString>

class Auth {
public:
    static bool registerUser(const QString &username, const QString &displayName,
                             const QString &password, User *out,
                             QString *error = nullptr);
    static bool login(const QString &username, const QString &password, User *out,
                      QString *error = nullptr);
    static bool changePassword(qint64 userId, const QString &oldPassword,
                               const QString &newPassword, QString *error = nullptr);

    static QByteArray randomSalt();
    static QString hashPassword(const QString &password, const QByteArray &salt);
};
