#include "auth.h"
#include "database.h"

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>

namespace {

constexpr int kIterations = 120000;
constexpr int kKeyLength = 32;

bool constantTimeEquals(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i) ^ b.at(i));
    return diff == 0;
}

}

QByteArray Auth::randomSalt()
{
    QByteArray salt(16, '\0');
    auto *words = reinterpret_cast<quint32 *>(salt.data());
    for (int i = 0; i < 4; ++i)
        words[i] = QRandomGenerator::system()->generate();
    return salt;
}

QString Auth::hashPassword(const QString &password, const QByteArray &salt)
{
    const QByteArray key = QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, password.toUtf8(), salt, kIterations, kKeyLength);
    return QString::fromLatin1(key.toHex());
}

bool Auth::registerUser(const QString &username, const QString &displayName,
                        const QString &password, User *out, QString *error)
{
    const QString user = username.trimmed();
    if (user.size() < 3) {
        if (error)
            *error = QStringLiteral("O usuário precisa ter pelo menos 3 caracteres.");
        return false;
    }
    if (password.size() < 4) {
        if (error)
            *error = QStringLiteral("A senha precisa ter pelo menos 4 caracteres.");
        return false;
    }

    User existing;
    if (Database::instance().findUserByUsername(user, &existing, nullptr, nullptr)) {
        if (error)
            *error = QStringLiteral("Este usuário já existe.");
        return false;
    }

    const QByteArray salt = randomSalt();
    const QString hash = hashPassword(password, salt);
    const QString name = displayName.trimmed().isEmpty() ? user : displayName.trimmed();

    if (!Database::instance().registerUser(user, name, hash,
                                           QString::fromLatin1(salt.toHex()),
                                           out, error))
        return false;

    Database::instance().logActivity(out->id, QStringLiteral("register"),
                                     QStringLiteral("Conta criada para %1").arg(user));
    return true;
}

bool Auth::login(const QString &username, const QString &password, User *out,
                 QString *error)
{
    const QString user = username.trimmed();
    if (user.isEmpty() || password.isEmpty()) {
        if (error)
            *error = QStringLiteral("Informe usuário e senha.");
        return false;
    }

    User found;
    QString storedHash;
    QString storedSalt;
    if (!Database::instance().findUserByUsername(user, &found, &storedHash,
                                                 &storedSalt)) {
        if (error)
            *error = QStringLiteral("Usuário ou senha inválidos.");
        return false;
    }

    const QString computed = hashPassword(password,
                                          QByteArray::fromHex(storedSalt.toLatin1()));
    if (!constantTimeEquals(computed.toLatin1(), storedHash.toLatin1())) {
        if (error)
            *error = QStringLiteral("Usuário ou senha inválidos.");
        return false;
    }

    if (out)
        *out = found;
    Database::instance().updateLastLogin(found.id);
    Database::instance().logActivity(found.id, QStringLiteral("login"),
                                     QStringLiteral("Sessão iniciada"));
    return true;
}

bool Auth::changePassword(qint64 userId, const QString &oldPassword,
                          const QString &newPassword, QString *error)
{
    if (newPassword.size() < 4) {
        if (error)
            *error = QStringLiteral("A nova senha precisa ter pelo menos 4 caracteres.");
        return false;
    }

    QString storedHash;
    QString storedSalt;
    if (!Database::instance().userCredentials(userId, &storedHash, &storedSalt)) {
        if (error)
            *error = QStringLiteral("Usuário não encontrado.");
        return false;
    }

    const QString computed = hashPassword(
        oldPassword, QByteArray::fromHex(storedSalt.toLatin1()));
    if (!constantTimeEquals(computed.toLatin1(), storedHash.toLatin1())) {
        if (error)
            *error = QStringLiteral("Senha atual incorreta.");
        return false;
    }

    const QByteArray newSalt = randomSalt();
    const QString newHash = hashPassword(newPassword, newSalt);
    if (!Database::instance().updatePassword(userId, newHash,
                                             QString::fromLatin1(newSalt.toHex()))) {
        if (error)
            *error = QStringLiteral("Não foi possível atualizar a senha.");
        return false;
    }

    Database::instance().logActivity(userId, QStringLiteral("password_change"),
                                     QStringLiteral("Senha alterada"));
    return true;
}
