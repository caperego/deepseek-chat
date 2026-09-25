#include "apiclient.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace {

QDateTime parseDate(const QString &value)
{
    QDateTime dt = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!dt.isValid())
        dt = QDateTime::fromString(value, Qt::ISODate);
    return dt;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray())
        list.append(item.toString());
    return list;
}

QString sanitizeMessageContent(const QString &content)
{
    if (content.isEmpty())
        return content;

    const QString trimmed = content.trimmed();
    if (trimmed.startsWith(QLatin1Char('['))) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
            QStringList parts;
            for (const QJsonValue &value : doc.array()) {
                const QJsonObject part = value.toObject();
                const QString type = part.value(QStringLiteral("type")).toString();
                if (type == QLatin1String("text"))
                    parts.append(part.value(QStringLiteral("text")).toString());
                else if (type == QLatin1String("image_url"))
                    parts.append(QStringLiteral("[imagem]"));
            }
            if (!parts.isEmpty())
                return parts.join(QStringLiteral("\n\n")).trimmed();
        }
    }

    if (content.contains(QLatin1String("base64,"))) {
        static const QRegularExpression dataUri(
            QStringLiteral("data:[a-z0-9.+/-]+;base64,[A-Za-z0-9+/=\\s]+"),
            QRegularExpression::CaseInsensitiveOption);
        QString cleaned = content;
        cleaned.remove(dataUri);
        cleaned = cleaned.trimmed();
        return cleaned.isEmpty() ? QStringLiteral("[imagem]") : cleaned;
    }

    return content;
}

}

ApiClient::ApiClient(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this))
{
}

ApiClient::Response ApiClient::call(const QByteArray &method, const QString &path,
                                    const QJsonObject &body)
{
    const bool hasBody = method == "POST" || method == "PUT";
    return sendRequest(method, path,
                       hasBody
                           ? QJsonDocument(body).toJson(QJsonDocument::Compact)
                           : QByteArray(),
                       hasBody);
}

ApiClient::Response ApiClient::sendRequest(const QByteArray &method, const QString &path,
                                           const QByteArray &payload, bool hasBody)
{
    Response result;
    if (m_baseUrl.isEmpty()) {
        result.error = QStringLiteral("URL da API não configurada.");
        return result;
    }

    QNetworkRequest request(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeoutMs);

    QNetworkReply *reply = nullptr;
    if (method == "GET")
        reply = m_nam->get(request);
    else if (method == "POST")
        reply = m_nam->post(request, payload);
    else if (method == "PUT")
        reply = m_nam->put(request, payload);
    else if (method == "DELETE")
        reply = m_nam->deleteResource(request);

    if (!reply) {
        result.error = QStringLiteral("Método HTTP não suportado.");
        return result;
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, [&] {
        reply->abort();
        loop.quit();
    });
    timer.start(m_timeoutMs + 2000);
    loop.exec();
    Q_UNUSED(hasBody);

    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    result.doc = QJsonDocument::fromJson(data);
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    reply->deleteLater();

    if (result.status == 0 && netError != QNetworkReply::NoError) {
        result.error = netErrorText;
        return result;
    }

    if (result.status >= 200 && result.status < 300) {
        result.ok = true;
        return result;
    }

    result.error = errorFrom(result.doc, result.status, netErrorText);
    return result;
}

QString ApiClient::errorFrom(const QJsonDocument &doc, int status,
                             const QString &fallback)
{
    if (doc.isObject()) {
        const QString message = doc.object().value(QStringLiteral("error")).toString();
        if (!message.isEmpty())
            return message;
    }
    return QStringLiteral("HTTP %1: %2").arg(status).arg(fallback);
}

QVector<QJsonObject> ApiClient::arrayItems(const QJsonDocument &doc)
{
    QVector<QJsonObject> items;
    if (!doc.isArray())
        return items;
    const QJsonArray array = doc.array();
    for (const QJsonValue &value : array) {
        if (value.isObject())
            items.append(value.toObject());
    }
    return items;
}

bool ApiClient::health(QString *error)
{
    Response response = call("GET", QStringLiteral("/health"));
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::login(const QString &username, const QString &password, User *out,
                      QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("password"), password);
    Response response = call("POST", QStringLiteral("/auth/login"), body);
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }

    const QJsonObject root = response.doc.object();
    m_token = root.value(QStringLiteral("token")).toString();
    if (out) {
        const QJsonObject user = root.value(QStringLiteral("user")).toObject();
        out->id = static_cast<qint64>(user.value(QStringLiteral("id")).toDouble());
        out->username = user.value(QStringLiteral("username")).toString();
        out->displayName = user.value(QStringLiteral("display_name")).toString();
    }
    return true;
}

bool ApiClient::registerUser(const QString &username, const QString &displayName,
                             const QString &password, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("username"), username);
    body.insert(QStringLiteral("display_name"), displayName);
    body.insert(QStringLiteral("password"), password);
    Response response = call("POST", QStringLiteral("/auth/register"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::logout(QString *error)
{
    Response response = call("POST", QStringLiteral("/auth/logout"));
    if (!response.ok && error)
        *error = response.error;
    m_token.clear();
    return response.ok;
}

bool ApiClient::me(User *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/me"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        const QJsonObject user = response.doc.object()
                                     .value(QStringLiteral("user")).toObject();
        out->id = static_cast<qint64>(user.value(QStringLiteral("id")).toDouble());
        out->username = user.value(QStringLiteral("username")).toString();
        out->displayName = user.value(QStringLiteral("display_name")).toString();
    }
    return true;
}

bool ApiClient::updateProfile(const QString &displayName, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("display_name"), displayName);
    Response response = call("PUT", QStringLiteral("/me"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::changePassword(const QString &oldPassword, const QString &newPassword,
                               QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("old_password"), oldPassword);
    body.insert(QStringLiteral("new_password"), newPassword);
    Response response = call("PUT", QStringLiteral("/me/password"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::models(QVector<ModelInfo> *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/models"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }

    if (out) {
        out->clear();
        const QJsonArray array = response.doc.object()
                                     .value(QStringLiteral("data")).toArray();
        for (const QJsonValue &value : array) {
            const QJsonObject object = value.toObject();
            ModelInfo info;
            info.id = object.value(QStringLiteral("id")).toString();
            info.name = object.value(QStringLiteral("name")).toString();
            if (info.name.isEmpty())
                info.name = info.id;
            info.contextWindow = object.value(QStringLiteral("context_window")).toInt();
            info.maxOutputTokens = object.value(QStringLiteral("max_output_tokens")).toInt();
            for (const QJsonValue &modality :
                 object.value(QStringLiteral("input_modalities")).toArray()) {
                if (modality.toString() == QLatin1String("image"))
                    info.supportsImages = true;
            }
            const QJsonObject effort = object.value(QStringLiteral("effort")).toObject();
            info.efforts = stringList(effort.value(QStringLiteral("supported_levels")));
            info.defaultEffort = effort.value(QStringLiteral("default_level")).toString();
            if (!info.id.isEmpty())
                out->append(info);
        }
    }
    return true;
}

bool ApiClient::balance(QJsonObject *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/balance"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out)
        *out = response.doc.object();
    return true;
}

bool ApiClient::projects(QVector<Project> *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/projects"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            Project project;
            project.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
            project.userId = static_cast<qint64>(object.value(QStringLiteral("user_id")).toDouble());
            project.name = object.value(QStringLiteral("name")).toString();
            project.description = object.value(QStringLiteral("description")).toString();
            project.workdir = object.value(QStringLiteral("workdir")).toString();
            project.systemPrompt = object.value(QStringLiteral("system_prompt")).toString();
            project.defaultModel = object.value(QStringLiteral("default_model")).toString();
            project.defaultEffort = object.value(QStringLiteral("default_effort")).toString();
            project.permissionMode = object.value(QStringLiteral("permission_mode")).toString();
            if (project.permissionMode.isEmpty())
                project.permissionMode = QStringLiteral("ask");
            project.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());
            project.updatedAt = parseDate(object.value(QStringLiteral("updated_at")).toString());
            out->append(project);
        }
    }
    return true;
}

bool ApiClient::createProject(const Project &project, qint64 *id, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), project.name);
    body.insert(QStringLiteral("description"), project.description);
    body.insert(QStringLiteral("workdir"), project.workdir);
    body.insert(QStringLiteral("system_prompt"), project.systemPrompt);
    body.insert(QStringLiteral("default_model"), project.defaultModel);
    body.insert(QStringLiteral("default_effort"), project.defaultEffort);
    body.insert(QStringLiteral("permission_mode"),
                project.permissionMode.isEmpty() ? QStringLiteral("ask")
                                                 : project.permissionMode);
    Response response = call("POST", QStringLiteral("/projects"), body);
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (id)
        *id = static_cast<qint64>(response.doc.object()
                                      .value(QStringLiteral("id")).toDouble());
    return true;
}

bool ApiClient::updateProject(const Project &project, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), project.name);
    body.insert(QStringLiteral("description"), project.description);
    body.insert(QStringLiteral("workdir"), project.workdir);
    body.insert(QStringLiteral("system_prompt"), project.systemPrompt);
    body.insert(QStringLiteral("default_model"), project.defaultModel);
    body.insert(QStringLiteral("default_effort"), project.defaultEffort);
    body.insert(QStringLiteral("permission_mode"),
                project.permissionMode.isEmpty() ? QStringLiteral("ask")
                                                 : project.permissionMode);
    Response response = call("PUT", QStringLiteral("/projects/%1").arg(project.id), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::deleteProject(qint64 id, QString *error)
{
    Response response = call("DELETE", QStringLiteral("/projects/%1").arg(id));
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::conversations(QVector<Conversation> *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/conversations"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            Conversation conversation;
            conversation.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
            conversation.userId = static_cast<qint64>(object.value(QStringLiteral("user_id")).toDouble());
            conversation.projectId = static_cast<qint64>(object.value(QStringLiteral("project_id")).toDouble());
            conversation.title = object.value(QStringLiteral("title")).toString();
            conversation.model = object.value(QStringLiteral("model")).toString();
            conversation.effort = object.value(QStringLiteral("effort")).toString();
            conversation.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());
            conversation.updatedAt = parseDate(object.value(QStringLiteral("updated_at")).toString());
            out->append(conversation);
        }
    }
    return true;
}

bool ApiClient::createConversation(qint64 projectId, const QString &title,
                                   const QString &model, const QString &effort,
                                   qint64 *id, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("project_id"), static_cast<double>(projectId));
    body.insert(QStringLiteral("title"), title);
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("effort"), effort);
    Response response = call("POST", QStringLiteral("/conversations"), body);
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (id)
        *id = static_cast<qint64>(response.doc.object()
                                      .value(QStringLiteral("id")).toDouble());
    return true;
}

bool ApiClient::updateConversation(qint64 id, const QString &title,
                                   const QString &model, const QString &effort,
                                   QString *error)
{
    QJsonObject body;
    if (!title.isEmpty())
        body.insert(QStringLiteral("title"), title);
    if (!model.isEmpty())
        body.insert(QStringLiteral("model"), model);
    if (!effort.isEmpty())
        body.insert(QStringLiteral("effort"), effort);
    Response response = call("PUT", QStringLiteral("/conversations/%1").arg(id), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::deleteConversation(qint64 id, QString *error)
{
    Response response = call("DELETE", QStringLiteral("/conversations/%1").arg(id));
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::moveConversation(qint64 id, qint64 projectId, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("project_id"), static_cast<double>(projectId));
    Response response = call("PUT", QStringLiteral("/conversations/%1").arg(id), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::truncateConversation(qint64 conversationId, qint64 fromMessageId,
                                     QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("from_message_id"), static_cast<double>(fromMessageId));
    Response response = call(
        "POST", QStringLiteral("/conversations/%1/truncate").arg(conversationId), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::searchMessages(const QString &query, QVector<SearchHit> *out,
                               QString *error)
{
    const QString path = QStringLiteral("/search?q=%1")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(query)));
    Response response = call("GET", path);
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            SearchHit hit;
            hit.conversationId = static_cast<qint64>(
                object.value(QStringLiteral("conversation_id")).toDouble());
            hit.title = object.value(QStringLiteral("title")).toString();
            hit.role = object.value(QStringLiteral("role")).toString();
            hit.snippet = object.value(QStringLiteral("snippet")).toString();
            hit.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());
            out->append(hit);
        }
    }
    return true;
}

bool ApiClient::messages(qint64 conversationId, QVector<Message> *out,
                         QString *error)
{
    Response response = call(
        "GET", QStringLiteral("/messages?conversation_id=%1").arg(conversationId));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            Message message;
            message.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
            message.conversationId = static_cast<qint64>(
                object.value(QStringLiteral("conversation_id")).toDouble());
            message.role = object.value(QStringLiteral("role")).toString();
            message.content = sanitizeMessageContent(
                object.value(QStringLiteral("content")).toString());
            message.reasoning = object.value(QStringLiteral("reasoning")).toString();
            message.model = object.value(QStringLiteral("model")).toString();
            message.promptTokens = static_cast<qint64>(
                object.value(QStringLiteral("prompt_tokens")).toDouble());
            message.completionTokens = static_cast<qint64>(
                object.value(QStringLiteral("completion_tokens")).toDouble());
            message.cachedTokens = static_cast<qint64>(
                object.value(QStringLiteral("cached_tokens")).toDouble());
            message.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());

            for (const QJsonValue &attachment :
                 object.value(QStringLiteral("attachments")).toArray()) {
                const QJsonObject item = attachment.toObject();
                Attachment a;
                a.kind = item.value(QStringLiteral("kind")).toString();
                a.name = item.value(QStringLiteral("name")).toString();
                a.path = item.value(QStringLiteral("path")).toString();
                a.mime = item.value(QStringLiteral("mime")).toString();
                a.text = item.value(QStringLiteral("text")).toString();
                message.attachments.append(a);
            }
            out->append(message);
        }
    }
    return true;
}

bool ApiClient::addMessage(qint64 conversationId, const QString &role,
                           const QString &content, const QString &reasoning,
                           const QString &model, const QVector<Attachment> &attachments,
                           QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("conversation_id"), static_cast<double>(conversationId));
    body.insert(QStringLiteral("role"), role);
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("reasoning"), reasoning);
    body.insert(QStringLiteral("model"), model);
    if (!attachments.isEmpty()) {
        QJsonArray array;
        for (const Attachment &a : attachments) {
            QJsonObject item;
            item.insert(QStringLiteral("kind"), a.kind);
            item.insert(QStringLiteral("name"), a.name);
            item.insert(QStringLiteral("path"), a.path);
            item.insert(QStringLiteral("mime"), a.mime);
            if (!a.text.isEmpty())
                item.insert(QStringLiteral("text"), a.text);
            array.append(item);
        }
        body.insert(QStringLiteral("attachments"), array);
    }
    Response response = call("POST", QStringLiteral("/messages"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::commands(qint64 projectId, QVector<Command> *out, QString *error)
{
    Response response = call(
        "GET", QStringLiteral("/commands?project_id=%1").arg(projectId));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            Command command;
            command.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
            command.projectId = static_cast<qint64>(
                object.value(QStringLiteral("project_id")).toDouble());
            command.name = object.value(QStringLiteral("name")).toString();
            command.prompt = object.value(QStringLiteral("prompt")).toString();
            out->append(command);
        }
    }
    return true;
}

bool ApiClient::addCommand(qint64 projectId, const QString &name,
                           const QString &prompt, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("project_id"), static_cast<double>(projectId));
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("prompt"), prompt);
    Response response = call("POST", QStringLiteral("/commands"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::updateCommand(qint64 id, const QString &name, const QString &prompt,
                              QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("prompt"), prompt);
    Response response = call("PUT", QStringLiteral("/commands/%1").arg(id), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::removeCommand(qint64 id, QString *error)
{
    Response response = call("DELETE", QStringLiteral("/commands/%1").arg(id));
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::projectItems(qint64 projectId, QVector<ProjectItem> *out,
                             QString *error)
{
    Response response = call(
        "GET", QStringLiteral("/project-items?project_id=%1").arg(projectId));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            ProjectItem item;
            item.id = static_cast<qint64>(object.value(QStringLiteral("id")).toDouble());
            item.projectId = static_cast<qint64>(
                object.value(QStringLiteral("project_id")).toDouble());
            item.kind = object.value(QStringLiteral("kind")).toString();
            item.name = object.value(QStringLiteral("name")).toString();
            item.path = object.value(QStringLiteral("path")).toString();
            item.mime = object.value(QStringLiteral("mime")).toString();
            item.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
            item.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());
            out->append(item);
        }
    }
    return true;
}

bool ApiClient::addProjectItem(const ProjectItem &item, QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("project_id"), static_cast<double>(item.projectId));
    body.insert(QStringLiteral("kind"), item.kind);
    body.insert(QStringLiteral("name"), item.name);
    body.insert(QStringLiteral("path"), item.path);
    body.insert(QStringLiteral("mime"), item.mime);
    body.insert(QStringLiteral("size"), static_cast<double>(item.size));
    Response response = call("POST", QStringLiteral("/project-items"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::removeProjectItem(qint64 id, QString *error)
{
    Response response = call("DELETE", QStringLiteral("/project-items/%1").arg(id));
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::activities(QVector<Activity> *out, QString *error)
{
    Response response = call("GET", QStringLiteral("/activities"));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }
    if (out) {
        out->clear();
        for (const QJsonObject &object : arrayItems(response.doc)) {
            Activity activity;
            activity.action = object.value(QStringLiteral("action")).toString();
            activity.details = object.value(QStringLiteral("details")).toString();
            activity.createdAt = parseDate(object.value(QStringLiteral("created_at")).toString());
            out->append(activity);
        }
    }
    return true;
}

bool ApiClient::logActivity(const QString &action, const QString &details,
                            QString *error)
{
    QJsonObject body;
    body.insert(QStringLiteral("action"), action);
    body.insert(QStringLiteral("details"), details);
    Response response = call("POST", QStringLiteral("/activities"), body);
    if (!response.ok && error)
        *error = response.error;
    return response.ok;
}

bool ApiClient::usage(qint64 conversationId, UsageTotals *conversation,
                      QVector<UsageTotals> *byModel, QString *error)
{
    Response response = call(
        "GET", QStringLiteral("/usage?conversation_id=%1").arg(conversationId));
    if (!response.ok) {
        if (error)
            *error = response.error;
        return false;
    }

    const QJsonObject root = response.doc.object();
    if (conversation) {
        const QJsonObject object = root.value(QStringLiteral("conversation")).toObject();
        conversation->prompt = static_cast<qint64>(object.value(QStringLiteral("prompt")).toDouble());
        conversation->completion = static_cast<qint64>(object.value(QStringLiteral("completion")).toDouble());
        conversation->cached = static_cast<qint64>(object.value(QStringLiteral("cached")).toDouble());
        conversation->messages = static_cast<qint64>(object.value(QStringLiteral("messages")).toDouble());
    }
    if (byModel) {
        byModel->clear();
        for (const QJsonValue &value : root.value(QStringLiteral("by_model")).toArray()) {
            const QJsonObject object = value.toObject();
            UsageTotals totals;
            totals.model = object.value(QStringLiteral("model")).toString();
            totals.prompt = static_cast<qint64>(object.value(QStringLiteral("prompt")).toDouble());
            totals.completion = static_cast<qint64>(object.value(QStringLiteral("completion")).toDouble());
            totals.cached = static_cast<qint64>(object.value(QStringLiteral("cached")).toDouble());
            totals.messages = static_cast<qint64>(object.value(QStringLiteral("messages")).toDouble());
            byModel->append(totals);
        }
    }
    return true;
}
