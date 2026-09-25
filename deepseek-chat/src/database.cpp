#include "database.h"
#include "config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

const char *kConnection = "deepseek_main";

QSqlDatabase mainDb()
{
    return QSqlDatabase::database(QString::fromLatin1(kConnection));
}

bool fail(QString *error, const QSqlQuery &query)
{
    if (error)
        *error = query.lastError().text();
    return false;
}

QDateTime toDateTime(const QVariant &value)
{
    QDateTime dt = value.toDateTime();
    if (!dt.isValid())
        dt = QDateTime::fromString(value.toString(), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return dt;
}

QString attachmentsToJson(const QVector<Attachment> &attachments)
{
    QJsonArray array;
    for (const Attachment &a : attachments) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), a.kind);
        o.insert(QStringLiteral("name"), a.name);
        o.insert(QStringLiteral("path"), a.path);
        o.insert(QStringLiteral("mime"), a.mime);
        if (!a.text.isEmpty())
            o.insert(QStringLiteral("text"), a.text);
        array.append(o);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QVector<Attachment> attachmentsFromJson(const QString &json)
{
    QVector<Attachment> result;
    if (json.isEmpty())
        return result;

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray())
        return result;

    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        Attachment a;
        a.kind = o.value(QStringLiteral("kind")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.path = o.value(QStringLiteral("path")).toString();
        a.mime = o.value(QStringLiteral("mime")).toString();
        a.text = o.value(QStringLiteral("text")).toString();
        result.append(a);
    }
    return result;
}

}

Database &Database::instance()
{
    static Database db;
    return db;
}

bool Database::connect(QString *error)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL"))) {
        if (error)
            *error = QStringLiteral("Driver QMYSQL não está disponível no Qt.");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::contains(QString::fromLatin1(kConnection))
        ? QSqlDatabase::database(QString::fromLatin1(kConnection))
        : QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), QString::fromLatin1(kConnection));

    const DbConfig &c = Config::instance().db();
    db.setHostName(c.host);
    db.setPort(c.port);
    db.setDatabaseName(c.name);
    db.setUserName(c.user);
    db.setPassword(c.password);
    db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    q.exec(QStringLiteral("SET NAMES utf8mb4"));
    return true;
}

bool Database::isOpen() const
{
    QSqlDatabase db = mainDb();
    return db.isOpen();
}

bool Database::reconnect(QString *error)
{
    const QString name = QString::fromLatin1(kConnection);
    {
        QSqlDatabase db = QSqlDatabase::database(name, false);
        if (db.isValid() && db.isOpen())
            db.close();
    }
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
    return connect(error);
}

bool Database::ensureSchema(QString *error)
{
    const QStringList statements = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS users ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " username VARCHAR(64) NOT NULL UNIQUE,"
            " display_name VARCHAR(128) NOT NULL DEFAULT '',"
            " password_hash VARCHAR(128) NOT NULL,"
            " salt VARCHAR(64) NOT NULL,"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " last_login DATETIME NULL"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS projects ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " user_id BIGINT NOT NULL,"
            " name VARCHAR(160) NOT NULL,"
            " description VARCHAR(512) NOT NULL DEFAULT '',"
            " workdir VARCHAR(1024) NOT NULL DEFAULT '',"
            " system_prompt MEDIUMTEXT NULL,"
            " default_model VARCHAR(64) NOT NULL DEFAULT 'deepseek-flash',"
            " default_effort VARCHAR(16) NOT NULL DEFAULT 'high',"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
            "   ON UPDATE CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_project_user FOREIGN KEY (user_id) REFERENCES users(id)"
            "   ON DELETE CASCADE,"
            " INDEX idx_project_user (user_id, updated_at)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_items ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " project_id BIGINT NOT NULL,"
            " kind ENUM('file','image','note','folder') NOT NULL DEFAULT 'file',"
            " name VARCHAR(255) NOT NULL,"
            " path VARCHAR(1024) NOT NULL DEFAULT '',"
            " mime VARCHAR(128) NOT NULL DEFAULT '',"
            " size BIGINT NOT NULL DEFAULT 0,"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_item_project FOREIGN KEY (project_id)"
            "   REFERENCES projects(id) ON DELETE CASCADE,"
            " INDEX idx_item_project (project_id, id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS commands ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " project_id BIGINT NULL,"
            " name VARCHAR(80) NOT NULL,"
            " prompt MEDIUMTEXT NOT NULL,"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_cmd_project FOREIGN KEY (project_id)"
            "   REFERENCES projects(id) ON DELETE CASCADE,"
            " INDEX idx_cmd_project (project_id, id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conversations ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " user_id BIGINT NOT NULL,"
            " project_id BIGINT NULL,"
            " title VARCHAR(255) NOT NULL DEFAULT 'Nova conversa',"
            " model VARCHAR(64) NOT NULL DEFAULT 'deepseek-flash',"
            " effort VARCHAR(16) NOT NULL DEFAULT 'high',"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
            "   ON UPDATE CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_conv_user FOREIGN KEY (user_id) REFERENCES users(id)"
            "   ON DELETE CASCADE,"
            " INDEX idx_conv_user (user_id, updated_at),"
            " INDEX idx_conv_project (project_id, updated_at)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " conversation_id BIGINT NOT NULL,"
            " role ENUM('system','user','assistant') NOT NULL,"
            " content MEDIUMTEXT NOT NULL,"
            " reasoning MEDIUMTEXT NULL,"
            " attachments_json LONGTEXT NULL,"
            " model VARCHAR(64) NOT NULL DEFAULT '',"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_msg_conv FOREIGN KEY (conversation_id)"
            "   REFERENCES conversations(id) ON DELETE CASCADE,"
            " INDEX idx_msg_conv (conversation_id, id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS activities ("
            " id BIGINT AUTO_INCREMENT PRIMARY KEY,"
            " user_id BIGINT NOT NULL,"
            " action VARCHAR(64) NOT NULL,"
            " details VARCHAR(512) NOT NULL DEFAULT '',"
            " created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " CONSTRAINT fk_act_user FOREIGN KEY (user_id) REFERENCES users(id)"
            "   ON DELETE CASCADE,"
            " INDEX idx_act_user (user_id, id)"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"),
        QStringLiteral("ALTER TABLE conversations ADD COLUMN IF NOT EXISTS project_id BIGINT NULL"),
        QStringLiteral("ALTER TABLE conversations ADD COLUMN IF NOT EXISTS effort VARCHAR(16) NOT NULL DEFAULT 'high'"),
        QStringLiteral("ALTER TABLE messages ADD COLUMN IF NOT EXISTS attachments_json LONGTEXT NULL"),
        QStringLiteral("ALTER TABLE messages ADD COLUMN IF NOT EXISTS prompt_tokens INT NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE messages ADD COLUMN IF NOT EXISTS completion_tokens INT NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE messages ADD COLUMN IF NOT EXISTS cached_tokens INT NOT NULL DEFAULT 0"),
        QStringLiteral("ALTER TABLE commands MODIFY COLUMN project_id BIGINT NULL")
    };

    QSqlDatabase db = mainDb();
    for (const QString &sql : statements) {
        QSqlQuery q(db);
        if (!q.exec(sql)) {
            if (error)
                *error = q.lastError().text() + QStringLiteral("\nSQL: ") + sql.left(120);
            return false;
        }
    }
    return true;
}

bool Database::registerUser(const QString &username, const QString &displayName,
                            const QString &passwordHash, const QString &salt,
                            User *out, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO users (username, display_name, password_hash, salt)"
        " VALUES (:u, :d, :h, :s)"));
    q.bindValue(QStringLiteral(":u"), username);
    q.bindValue(QStringLiteral(":d"), displayName);
    q.bindValue(QStringLiteral(":h"), passwordHash);
    q.bindValue(QStringLiteral(":s"), salt);
    if (!q.exec())
        return fail(error, q);

    if (out) {
        out->id = q.lastInsertId().toLongLong();
        out->username = username;
        out->displayName = displayName;
    }
    return true;
}

bool Database::findUserByUsername(const QString &username, User *out,
                                  QString *hash, QString *salt, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, username, display_name, password_hash, salt"
        " FROM users WHERE username = :u"));
    q.bindValue(QStringLiteral(":u"), username);
    if (!q.exec())
        return fail(error, q);

    if (!q.next())
        return false;

    if (out) {
        out->id = q.value(0).toLongLong();
        out->username = q.value(1).toString();
        out->displayName = q.value(2).toString();
    }
    if (hash)
        *hash = q.value(3).toString();
    if (salt)
        *salt = q.value(4).toString();
    return true;
}

bool Database::updateLastLogin(qint64 userId)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("UPDATE users SET last_login = NOW() WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), userId);
    return q.exec();
}

QVector<Project> Database::projects(qint64 userId, QString *error)
{
    QVector<Project> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, user_id, name, description, workdir, system_prompt,"
        " default_model, default_effort, created_at, updated_at"
        " FROM projects WHERE user_id = :u ORDER BY updated_at DESC, id DESC"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        Project p;
        p.id = q.value(0).toLongLong();
        p.userId = q.value(1).toLongLong();
        p.name = q.value(2).toString();
        p.description = q.value(3).toString();
        p.workdir = q.value(4).toString();
        p.systemPrompt = q.value(5).toString();
        p.defaultModel = q.value(6).toString();
        p.defaultEffort = q.value(7).toString();
        p.createdAt = toDateTime(q.value(8));
        p.updatedAt = toDateTime(q.value(9));
        result.append(p);
    }
    return result;
}

qint64 Database::createProject(const Project &project, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO projects (user_id, name, description, workdir, system_prompt,"
        " default_model, default_effort) VALUES (:u, :n, :d, :w, :s, :m, :e)"));
    q.bindValue(QStringLiteral(":u"), project.userId);
    q.bindValue(QStringLiteral(":n"), project.name);
    q.bindValue(QStringLiteral(":d"), project.description);
    q.bindValue(QStringLiteral(":w"), project.workdir);
    q.bindValue(QStringLiteral(":s"), project.systemPrompt);
    q.bindValue(QStringLiteral(":m"), project.defaultModel);
    q.bindValue(QStringLiteral(":e"), project.defaultEffort);
    if (!q.exec()) {
        fail(error, q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::updateProject(const Project &project, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "UPDATE projects SET name = :n, description = :d, workdir = :w,"
        " system_prompt = :s, default_model = :m, default_effort = :e"
        " WHERE id = :id"));
    q.bindValue(QStringLiteral(":n"), project.name);
    q.bindValue(QStringLiteral(":d"), project.description);
    q.bindValue(QStringLiteral(":w"), project.workdir);
    q.bindValue(QStringLiteral(":s"), project.systemPrompt);
    q.bindValue(QStringLiteral(":m"), project.defaultModel);
    q.bindValue(QStringLiteral(":e"), project.defaultEffort);
    q.bindValue(QStringLiteral(":id"), project.id);
    if (!q.exec())
        return fail(error, q);
    return true;
}

bool Database::deleteProject(qint64 projectId, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("DELETE FROM projects WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), projectId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

QVector<ProjectItem> Database::projectItems(qint64 projectId, QString *error)
{
    QVector<ProjectItem> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, kind, name, path, mime, size, created_at"
        " FROM project_items WHERE project_id = :p ORDER BY id DESC"));
    q.bindValue(QStringLiteral(":p"), projectId);
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        ProjectItem item;
        item.id = q.value(0).toLongLong();
        item.projectId = q.value(1).toLongLong();
        item.kind = q.value(2).toString();
        item.name = q.value(3).toString();
        item.path = q.value(4).toString();
        item.mime = q.value(5).toString();
        item.size = q.value(6).toLongLong();
        item.createdAt = toDateTime(q.value(7));
        result.append(item);
    }
    return result;
}

qint64 Database::addProjectItem(const ProjectItem &item, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO project_items (project_id, kind, name, path, mime, size)"
        " VALUES (:p, :k, :n, :w, :m, :s)"));
    q.bindValue(QStringLiteral(":p"), item.projectId);
    q.bindValue(QStringLiteral(":k"), item.kind);
    q.bindValue(QStringLiteral(":n"), item.name);
    q.bindValue(QStringLiteral(":w"), item.path);
    q.bindValue(QStringLiteral(":m"), item.mime);
    q.bindValue(QStringLiteral(":s"), item.size);
    if (!q.exec()) {
        fail(error, q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::removeProjectItem(qint64 itemId, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("DELETE FROM project_items WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), itemId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

QVector<Command> Database::commands(qint64 projectId, QString *error)
{
    QVector<Command> result;
    QSqlQuery q(mainDb());
    if (projectId > 0) {
        q.prepare(QStringLiteral(
            "SELECT id, project_id, name, prompt FROM commands"
            " WHERE project_id = :p ORDER BY name ASC"));
        q.bindValue(QStringLiteral(":p"), projectId);
    } else {
        q.prepare(QStringLiteral(
            "SELECT id, project_id, name, prompt FROM commands"
            " WHERE project_id IS NULL ORDER BY name ASC"));
    }
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        Command c;
        c.id = q.value(0).toLongLong();
        c.projectId = q.value(1).toLongLong();
        c.name = q.value(2).toString();
        c.prompt = q.value(3).toString();
        result.append(c);
    }
    return result;
}

qint64 Database::addCommand(qint64 projectId, const QString &name, const QString &prompt,
                            QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO commands (project_id, name, prompt) VALUES (:p, :n, :x)"));
    if (projectId > 0)
        q.bindValue(QStringLiteral(":p"), projectId);
    else
        q.bindValue(QStringLiteral(":p"), QVariant());
    q.bindValue(QStringLiteral(":n"), name);
    q.bindValue(QStringLiteral(":x"), prompt);
    if (!q.exec()) {
        fail(error, q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::seedDefaultCommands(QString *error)
{
    if (!commands(0).isEmpty())
        return true;

    struct DefaultCommand {
        const char *name;
        const char *prompt;
    };
    const DefaultCommand defaults[] = {
        {"explicar", "Explique de forma clara e objetiva o seguinte conteúdo:\n"},
        {"resumir", "Resuma o texto a seguir em tópicos curtos:\n"},
        {"traduzir", "Traduza o texto a seguir para português mantendo o tom:\n"},
        {"revisar", "Revise o código a seguir, aponte problemas e sugira melhorias:\n"},
        {"corrigir", "Corrija os erros do código a seguir e explique o que mudou:\n"},
        {"testar", "Escreva testes automatizados para o código a seguir:\n"}
    };

    for (const DefaultCommand &dc : defaults) {
        if (addCommand(0, QString::fromLatin1(dc.name),
                       QString::fromLatin1(dc.prompt), error) <= 0)
            return false;
    }
    return true;
}

bool Database::updateDisplayName(qint64 userId, const QString &displayName)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("UPDATE users SET display_name = :d WHERE id = :id"));
    q.bindValue(QStringLiteral(":d"), displayName);
    q.bindValue(QStringLiteral(":id"), userId);
    return q.exec();
}

bool Database::updatePassword(qint64 userId, const QString &passwordHash,
                              const QString &salt)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "UPDATE users SET password_hash = :h, salt = :s WHERE id = :id"));
    q.bindValue(QStringLiteral(":h"), passwordHash);
    q.bindValue(QStringLiteral(":s"), salt);
    q.bindValue(QStringLiteral(":id"), userId);
    return q.exec();
}

bool Database::userCredentials(qint64 userId, QString *hash, QString *salt)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("SELECT password_hash, salt FROM users WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), userId);
    if (!q.exec() || !q.next())
        return false;
    if (hash)
        *hash = q.value(0).toString();
    if (salt)
        *salt = q.value(1).toString();
    return true;
}

bool Database::updateCommand(qint64 commandId, const QString &name, const QString &prompt,
                             QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "UPDATE commands SET name = :n, prompt = :x WHERE id = :id"));
    q.bindValue(QStringLiteral(":n"), name);
    q.bindValue(QStringLiteral(":x"), prompt);
    q.bindValue(QStringLiteral(":id"), commandId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

bool Database::removeCommand(qint64 commandId, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("DELETE FROM commands WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), commandId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

QVector<Conversation> Database::conversations(qint64 userId, QString *error)
{
    QVector<Conversation> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, user_id, project_id, title, model, effort, created_at, updated_at"
        " FROM conversations WHERE user_id = :u"
        " ORDER BY updated_at DESC, id DESC"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        Conversation c;
        c.id = q.value(0).toLongLong();
        c.userId = q.value(1).toLongLong();
        c.projectId = q.value(2).toLongLong();
        c.title = q.value(3).toString();
        c.model = q.value(4).toString();
        c.effort = q.value(5).toString();
        c.createdAt = toDateTime(q.value(6));
        c.updatedAt = toDateTime(q.value(7));
        result.append(c);
    }
    return result;
}

qint64 Database::createConversation(qint64 userId, qint64 projectId, const QString &title,
                                    const QString &model, const QString &effort,
                                    QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO conversations (user_id, project_id, title, model, effort)"
        " VALUES (:u, :p, :t, :m, :e)"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (projectId > 0)
        q.bindValue(QStringLiteral(":p"), projectId);
    else
        q.bindValue(QStringLiteral(":p"), QVariant());
    q.bindValue(QStringLiteral(":t"), title);
    q.bindValue(QStringLiteral(":m"), model);
    q.bindValue(QStringLiteral(":e"), effort);
    if (!q.exec()) {
        fail(error, q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::renameConversation(qint64 conversationId, const QString &title,
                                  QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("UPDATE conversations SET title = :t WHERE id = :id"));
    q.bindValue(QStringLiteral(":t"), title);
    q.bindValue(QStringLiteral(":id"), conversationId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

bool Database::deleteConversation(qint64 conversationId, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("DELETE FROM conversations WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), conversationId);
    if (!q.exec())
        return fail(error, q);
    return true;
}

bool Database::touchConversation(qint64 conversationId)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "UPDATE conversations SET updated_at = NOW() WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), conversationId);
    return q.exec();
}

bool Database::setConversationModel(qint64 conversationId, const QString &model)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("UPDATE conversations SET model = :m WHERE id = :id"));
    q.bindValue(QStringLiteral(":m"), model);
    q.bindValue(QStringLiteral(":id"), conversationId);
    return q.exec();
}

bool Database::setConversationEffort(qint64 conversationId, const QString &effort)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("UPDATE conversations SET effort = :e WHERE id = :id"));
    q.bindValue(QStringLiteral(":e"), effort);
    q.bindValue(QStringLiteral(":id"), conversationId);
    return q.exec();
}

QString Database::conversationModel(qint64 conversationId)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("SELECT model FROM conversations WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), conversationId);
    if (!q.exec() || !q.next())
        return QString();
    return q.value(0).toString();
}

QString Database::conversationEffort(qint64 conversationId)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral("SELECT effort FROM conversations WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), conversationId);
    if (!q.exec() || !q.next())
        return QString();
    return q.value(0).toString();
}

QVector<Message> Database::messages(qint64 conversationId, QString *error)
{
    QVector<Message> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, conversation_id, role, content, reasoning, attachments_json,"
        " model, created_at, prompt_tokens, completion_tokens, cached_tokens"
        " FROM messages WHERE conversation_id = :c ORDER BY id ASC"));
    q.bindValue(QStringLiteral(":c"), conversationId);
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        Message m;
        m.id = q.value(0).toLongLong();
        m.conversationId = q.value(1).toLongLong();
        m.role = q.value(2).toString();
        m.content = q.value(3).toString();
        m.reasoning = q.value(4).toString();
        m.attachments = attachmentsFromJson(q.value(5).toString());
        m.model = q.value(6).toString();
        m.createdAt = toDateTime(q.value(7));
        m.promptTokens = q.value(8).toLongLong();
        m.completionTokens = q.value(9).toLongLong();
        m.cachedTokens = q.value(10).toLongLong();
        result.append(m);
    }
    return result;
}

qint64 Database::addMessage(qint64 conversationId, const QString &role,
                            const QString &content, const QString &reasoning,
                            const QString &model, const QVector<Attachment> &attachments,
                            qint64 promptTokens, qint64 completionTokens,
                            qint64 cachedTokens, QString *error)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO messages (conversation_id, role, content, reasoning,"
        " attachments_json, model, prompt_tokens, completion_tokens, cached_tokens)"
        " VALUES (:c, :r, :x, :z, :a, :m, :pt, :ct, :ca)"));
    q.bindValue(QStringLiteral(":c"), conversationId);
    q.bindValue(QStringLiteral(":r"), role);
    q.bindValue(QStringLiteral(":x"), content);
    q.bindValue(QStringLiteral(":z"), reasoning);
    q.bindValue(QStringLiteral(":a"), attachments.isEmpty()
                                         ? QVariant()
                                         : attachmentsToJson(attachments));
    q.bindValue(QStringLiteral(":m"), model);
    q.bindValue(QStringLiteral(":pt"), promptTokens);
    q.bindValue(QStringLiteral(":ct"), completionTokens);
    q.bindValue(QStringLiteral(":ca"), cachedTokens);
    if (!q.exec()) {
        fail(error, q);
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

UsageTotals Database::conversationUsage(qint64 conversationId)
{
    UsageTotals totals;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0),"
        " COALESCE(SUM(cached_tokens),0), COUNT(*)"
        " FROM messages WHERE conversation_id = :c"));
    q.bindValue(QStringLiteral(":c"), conversationId);
    if (!q.exec() || !q.next())
        return totals;
    totals.prompt = q.value(0).toLongLong();
    totals.completion = q.value(1).toLongLong();
    totals.cached = q.value(2).toLongLong();
    totals.messages = q.value(3).toLongLong();
    return totals;
}

QVector<UsageTotals> Database::userUsageByModel(qint64 userId)
{
    QVector<UsageTotals> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT m.model, COALESCE(SUM(m.prompt_tokens),0),"
        " COALESCE(SUM(m.completion_tokens),0), COALESCE(SUM(m.cached_tokens),0),"
        " COUNT(*) FROM messages m"
        " JOIN conversations c ON c.id = m.conversation_id"
        " WHERE c.user_id = :u GROUP BY m.model"));
    q.bindValue(QStringLiteral(":u"), userId);
    if (!q.exec())
        return result;

    while (q.next()) {
        UsageTotals totals;
        totals.model = q.value(0).toString();
        totals.prompt = q.value(1).toLongLong();
        totals.completion = q.value(2).toLongLong();
        totals.cached = q.value(3).toLongLong();
        totals.messages = q.value(4).toLongLong();
        result.append(totals);
    }
    return result;
}

qint64 Database::messageCount(qint64 conversationId)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM messages WHERE conversation_id = :c"));
    q.bindValue(QStringLiteral(":c"), conversationId);
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toLongLong();
}

bool Database::logActivity(qint64 userId, const QString &action, const QString &details)
{
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "INSERT INTO activities (user_id, action, details) VALUES (:u, :a, :d)"));
    q.bindValue(QStringLiteral(":u"), userId);
    q.bindValue(QStringLiteral(":a"), action);
    q.bindValue(QStringLiteral(":d"), details.left(500));
    return q.exec();
}

QVector<Activity> Database::activities(qint64 userId, int limit, QString *error)
{
    QVector<Activity> result;
    QSqlQuery q(mainDb());
    q.prepare(QStringLiteral(
        "SELECT id, action, details, created_at FROM activities"
        " WHERE user_id = :u ORDER BY id DESC LIMIT :l"));
    q.bindValue(QStringLiteral(":u"), userId);
    q.bindValue(QStringLiteral(":l"), limit);
    if (!q.exec()) {
        fail(error, q);
        return result;
    }

    while (q.next()) {
        Activity a;
        a.id = q.value(0).toLongLong();
        a.action = q.value(1).toString();
        a.details = q.value(2).toString();
        a.createdAt = toDateTime(q.value(3));
        result.append(a);
    }
    return result;
}
