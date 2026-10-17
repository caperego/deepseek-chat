#pragma once

#include "chattypes.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;

class ApiClient : public QObject {
    Q_OBJECT

public:
    explicit ApiClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    QString baseUrl() const { return m_baseUrl; }
    void setToken(const QString &token) { m_token = token; }
    QString token() const { return m_token; }
    void clearToken() { m_token.clear(); }
    void setTimeout(int ms) { m_timeoutMs = ms; }

    bool health(QString *error = nullptr);

    bool login(const QString &username, const QString &password, User *out,
               QString *error = nullptr);
    bool registerUser(const QString &username, const QString &displayName,
                      const QString &password, QString *error = nullptr);
    bool logout(QString *error = nullptr);
    bool me(User *out, QString *error = nullptr);
    bool updateProfile(const QString &displayName, QString *error = nullptr);
    bool changePassword(const QString &oldPassword, const QString &newPassword,
                        QString *error = nullptr);

    bool models(QVector<ModelInfo> *out, QString *error = nullptr);
    bool balance(QJsonObject *out, QString *error = nullptr);

    bool projects(QVector<Project> *out, QString *error = nullptr);
    bool createProject(const Project &project, qint64 *id, QString *error = nullptr);
    bool updateProject(const Project &project, QString *error = nullptr);
    bool deleteProject(qint64 id, QString *error = nullptr);

    bool conversations(QVector<Conversation> *out, QString *error = nullptr);
    bool createConversation(qint64 projectId, const QString &title,
                            const QString &model, const QString &effort,
                            qint64 *id, QString *error = nullptr);
    bool updateConversation(qint64 id, const QString &title, const QString &model,
                            const QString &effort, QString *error = nullptr);
    bool moveConversation(qint64 id, qint64 projectId, QString *error = nullptr);
    bool truncateConversation(qint64 conversationId, qint64 fromMessageId,
                              QString *error = nullptr);
    bool branchConversation(qint64 conversationId, qint64 throughMessageId,
                            const QString &title, qint64 *newId,
                            QString *error = nullptr);
    bool searchMessages(const QString &query, QVector<SearchHit> *out,
                        QString *error = nullptr);
    bool deleteConversation(qint64 id, QString *error = nullptr);

    bool messages(qint64 conversationId, QVector<Message> *out,
                  QString *error = nullptr);
    bool addMessage(qint64 conversationId, const QString &role,
                    const QString &content, const QString &reasoning,
                    const QString &model, const QVector<Attachment> &attachments,
                    QString *error = nullptr);

    bool commands(qint64 projectId, QVector<Command> *out, QString *error = nullptr);
    bool addCommand(qint64 projectId, const QString &name, const QString &prompt,
                    QString *error = nullptr);
    bool updateCommand(qint64 id, const QString &name, const QString &prompt,
                       QString *error = nullptr);
    bool removeCommand(qint64 id, QString *error = nullptr);

    bool projectItems(qint64 projectId, QVector<ProjectItem> *out,
                      QString *error = nullptr);
    bool addProjectItem(const ProjectItem &item, QString *error = nullptr);
    bool removeProjectItem(qint64 id, QString *error = nullptr);

    bool projectMemories(qint64 projectId, QVariantList *out,
                         QString *error = nullptr);
    bool addProjectMemory(qint64 projectId, const QString &content,
                          QString *error = nullptr);
    bool updateProjectMemory(qint64 id, const QString &content,
                             QString *error = nullptr);
    bool deleteProjectMemory(qint64 id, QString *error = nullptr);

    bool activities(QVector<Activity> *out, QString *error = nullptr);
    bool logActivity(const QString &action, const QString &details,
                     QString *error = nullptr);

    bool usage(qint64 conversationId, UsageTotals *conversation,
               QVector<UsageTotals> *byModel, QString *error = nullptr);

private:
    struct Response {
        bool ok = false;
        int status = 0;
        QJsonDocument doc;
        QString error;
    };

    Response call(const QByteArray &method, const QString &path,
                  const QJsonObject &body = QJsonObject());
    Response sendRequest(const QByteArray &method, const QString &path,
                         const QByteArray &payload, bool hasBody = false);

    static QVector<QJsonObject> arrayItems(const QJsonDocument &doc);
    static QString errorFrom(const QJsonDocument &doc, int status,
                             const QString &fallback);

    QNetworkAccessManager *m_nam = nullptr;
    QString m_baseUrl;
    QString m_token;
    int m_timeoutMs = 30000;
};
