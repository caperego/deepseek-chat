#pragma once

#include "chattypes.h"

#include <QString>
#include <QVector>

class Database {
public:
    static Database &instance();

    bool connect(QString *error = nullptr);
    bool reconnect(QString *error = nullptr);
    bool ensureSchema(QString *error = nullptr);
    bool isOpen() const;

    bool registerUser(const QString &username, const QString &displayName,
                      const QString &passwordHash, const QString &salt,
                      User *out, QString *error = nullptr);
    bool findUserByUsername(const QString &username, User *out,
                            QString *hash, QString *salt, QString *error = nullptr);
    bool updateLastLogin(qint64 userId);

    QVector<Project> projects(qint64 userId, QString *error = nullptr);
    qint64 createProject(const Project &project, QString *error = nullptr);
    bool updateProject(const Project &project, QString *error = nullptr);
    bool deleteProject(qint64 projectId, QString *error = nullptr);

    QVector<ProjectItem> projectItems(qint64 projectId, QString *error = nullptr);
    qint64 addProjectItem(const ProjectItem &item, QString *error = nullptr);
    bool removeProjectItem(qint64 itemId, QString *error = nullptr);

    QVector<Command> commands(qint64 projectId, QString *error = nullptr);
    qint64 addCommand(qint64 projectId, const QString &name, const QString &prompt,
                      QString *error = nullptr);
    bool updateCommand(qint64 commandId, const QString &name, const QString &prompt,
                       QString *error = nullptr);
    bool removeCommand(qint64 commandId, QString *error = nullptr);
    bool seedDefaultCommands(QString *error = nullptr);

    bool updateDisplayName(qint64 userId, const QString &displayName);
    bool updatePassword(qint64 userId, const QString &passwordHash, const QString &salt);
    bool userCredentials(qint64 userId, QString *hash, QString *salt);

    QVector<Conversation> conversations(qint64 userId, QString *error = nullptr);
    qint64 createConversation(qint64 userId, qint64 projectId, const QString &title,
                              const QString &model, const QString &effort,
                              QString *error = nullptr);
    bool renameConversation(qint64 conversationId, const QString &title,
                            QString *error = nullptr);
    bool deleteConversation(qint64 conversationId, QString *error = nullptr);
    bool touchConversation(qint64 conversationId);
    bool setConversationModel(qint64 conversationId, const QString &model);
    bool setConversationEffort(qint64 conversationId, const QString &effort);
    QString conversationModel(qint64 conversationId);
    QString conversationEffort(qint64 conversationId);

    QVector<Message> messages(qint64 conversationId, QString *error = nullptr);
    qint64 addMessage(qint64 conversationId, const QString &role, const QString &content,
                      const QString &reasoning, const QString &model,
                      const QVector<Attachment> &attachments,
                      qint64 promptTokens = 0, qint64 completionTokens = 0,
                      qint64 cachedTokens = 0, QString *error = nullptr);
    qint64 messageCount(qint64 conversationId);

    UsageTotals conversationUsage(qint64 conversationId);
    QVector<UsageTotals> userUsageByModel(qint64 userId);

    bool logActivity(qint64 userId, const QString &action, const QString &details);
    QVector<Activity> activities(qint64 userId, int limit = 300,
                                 QString *error = nullptr);

private:
    Database() = default;
};
