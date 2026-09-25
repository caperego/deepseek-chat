#include "streamclient.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

StreamClient::StreamClient(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this))
{
}

void StreamClient::send(qint64 conversationId, qint64 projectId,
                        const QString &title, const QString &model,
                        const QString &effort, bool allowImages,
                        const QVector<Message> &messages,
                        const QJsonArray &tools,
                        bool persistUser, bool persist)
{
    if (isStreaming())
        return;

    QJsonArray msgs;
    QVector<Attachment> lastUserAttachments;
    for (const Message &message : messages) {
        const bool isTool = message.role == QLatin1String("tool");
        if (!isTool && message.role != QLatin1String("user")
            && message.role != QLatin1String("assistant")
            && message.role != QLatin1String("system"))
            continue;
        if (message.role == QLatin1String("assistant") && message.content.isEmpty()
            && message.toolCalls.isEmpty())
            continue;
        if (isTool && message.toolCallId.isEmpty())
            continue;

        QJsonObject object;
        object.insert(QStringLiteral("role"), message.role);
        if (isTool) {
            object.insert(QStringLiteral("tool_call_id"), message.toolCallId);
            object.insert(QStringLiteral("content"), message.content);
        } else {
            object.insert(QStringLiteral("content"), buildContent(message, allowImages));
        }

        if (!message.toolCalls.isEmpty()) {
            QJsonArray calls;
            for (const ToolCall &call : message.toolCalls) {
                QJsonObject function;
                function.insert(QStringLiteral("name"), call.name);
                function.insert(QStringLiteral("arguments"), call.arguments);
                QJsonObject item;
                item.insert(QStringLiteral("id"), call.id);
                item.insert(QStringLiteral("type"), QStringLiteral("function"));
                item.insert(QStringLiteral("function"), function);
                calls.append(item);
            }
            object.insert(QStringLiteral("tool_calls"), calls);
        }

        msgs.append(object);

        if (message.role == QLatin1String("user"))
            lastUserAttachments = message.attachments;
    }

    QJsonObject body;
    body.insert(QStringLiteral("conversation_id"), static_cast<double>(conversationId));
    body.insert(QStringLiteral("project_id"), static_cast<double>(projectId));
    body.insert(QStringLiteral("title"), title);
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("effort"), effort);
    body.insert(QStringLiteral("messages"), msgs);
    if (!tools.isEmpty()) {
        body.insert(QStringLiteral("tools"), tools);
        body.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }
    if (!persistUser)
        body.insert(QStringLiteral("persist_user"), false);
    if (!persist)
        body.insert(QStringLiteral("persist"), false);

    if (!lastUserAttachments.isEmpty()) {
        QJsonArray attachments;
        for (const Attachment &a : lastUserAttachments) {
            QJsonObject item;
            item.insert(QStringLiteral("kind"), a.kind);
            item.insert(QStringLiteral("name"), a.name);
            item.insert(QStringLiteral("path"), a.path);
            item.insert(QStringLiteral("mime"), a.mime);
            if (!a.text.isEmpty())
                item.insert(QStringLiteral("text"), a.text);
            attachments.append(item);
        }
        body.insert(QStringLiteral("attachments"), attachments);
    }

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/chat/stream")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    if (!m_token.isEmpty())
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_token.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeout);

    m_fullText.clear();
    m_fullReasoning.clear();
    m_toolCalls.clear();
    m_promptTokens = 0;
    m_completionTokens = 0;
    m_cachedTokens = 0;
    m_buffer.clear();
    m_currentEvent.clear();
    m_cancelled = false;

    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QIODevice::readyRead, this, &StreamClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &StreamClient::onReplyFinished);
}

QJsonValue StreamClient::buildContent(const Message &message, bool allowImages) const
{
    QString text = message.content;
    QVector<const Attachment *> images;

    for (const Attachment &a : message.attachments) {
        if (a.isImage()) {
            if (allowImages)
                images.append(&a);
            else
                text += QStringLiteral("\n\n[imagem anexada: %1]").arg(a.name);
            continue;
        }
        if (!a.text.isEmpty())
            text += QStringLiteral("\n\n--- Arquivo: %1 ---\n```\n%2\n```")
                        .arg(a.name, a.text);
    }

    if (images.isEmpty())
        return QJsonValue(text);

    QJsonArray parts;
    QJsonObject textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), text);
    parts.append(textPart);

    for (const Attachment *a : images) {
        QByteArray data = a->data;
        if (data.isEmpty() && !a->path.isEmpty()) {
            QFile file(a->path);
            if (file.open(QIODevice::ReadOnly))
                data = file.readAll();
        }
        if (data.isEmpty())
            continue;

        QString mime = a->mime;
        if (mime.isEmpty())
            mime = QStringLiteral("image/png");

        QJsonObject imagePart;
        imagePart.insert(QStringLiteral("type"), QStringLiteral("image_url"));
        QJsonObject imageUrl;
        imageUrl.insert(QStringLiteral("url"),
                        QStringLiteral("data:%1;base64,%2")
                            .arg(mime, QString::fromLatin1(data.toBase64())));
        imagePart.insert(QStringLiteral("image_url"), imageUrl);
        parts.append(imagePart);
    }

    return QJsonValue(parts);
}

void StreamClient::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void StreamClient::onReadyRead()
{
    if (!m_reply)
        return;
    m_buffer.append(m_reply->readAll());
    processBuffer();
}

void StreamClient::processBuffer()
{
    int index = -1;
    while ((index = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(index).trimmed();
        m_buffer.remove(0, index + 1);

        if (line.isEmpty())
            continue;

        if (line.startsWith(QByteArrayLiteral("event:"))) {
            m_currentEvent = QString::fromUtf8(line.mid(6)).trimmed();
            continue;
        }

        if (!line.startsWith(QByteArrayLiteral("data:")))
            continue;

        const QByteArray payload = line.mid(5).trimmed();
        if (payload.isEmpty() || payload == QByteArrayLiteral("[DONE]"))
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject object = doc.object();

        if (m_currentEvent == QLatin1String("meta")) {
            m_currentEvent.clear();
            const qint64 id = static_cast<qint64>(
                object.value(QStringLiteral("conversation_id")).toDouble());
            if (id > 0)
                emit metaReceived(id);
            continue;
        }

        const QJsonValue usageValue = object.value(QStringLiteral("usage"));
        if (usageValue.isObject()) {
            const QJsonObject usage = usageValue.toObject();
            m_promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
            m_completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
            m_cachedTokens = usage.value(QStringLiteral("prompt_tokens_details"))
                                 .toObject().value(QStringLiteral("cached_tokens")).toInt();
            if (m_cachedTokens == 0)
                m_cachedTokens = usage.value(QStringLiteral("prompt_cache_hit_tokens")).toInt();
        }

        const QJsonArray choices = object.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty())
            continue;

        const QJsonObject delta = choices.first().toObject()
                                      .value(QStringLiteral("delta")).toObject();

        const QJsonValue reasoning = delta.value(QStringLiteral("reasoning_content"));
        if (reasoning.isString()) {
            const QString piece = reasoning.toString();
            if (!piece.isEmpty()) {
                m_fullReasoning += piece;
                emit reasoningReceived(piece);
            }
        }

        const QJsonArray toolDelta = delta.value(QStringLiteral("tool_calls")).toArray();
        for (const QJsonValue &value : toolDelta) {
            const QJsonObject item = value.toObject();
            int index = item.value(QStringLiteral("index")).toInt(-1);
            if (index < 0)
                index = m_toolCalls.size();
            while (m_toolCalls.size() <= index)
                m_toolCalls.append(ToolCall());

            ToolCall &call = m_toolCalls[index];
            const QString id = item.value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                call.id = id;
            const QJsonObject function = item.value(QStringLiteral("function")).toObject();
            call.name += function.value(QStringLiteral("name")).toString();
            call.arguments += function.value(QStringLiteral("arguments")).toString();
        }

        const QJsonValue content = delta.value(QStringLiteral("content"));
        if (content.isString()) {
            const QString piece = content.toString();
            if (!piece.isEmpty()) {
                m_fullText += piece;
                emit chunkReceived(piece);
            }
        }
    }
}

void StreamClient::onReplyFinished()
{
    if (!m_reply)
        return;

    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const QNetworkReply::NetworkError netError = reply->error();
    const QByteArray extra = reply->readAll();
    if (!extra.isEmpty()) {
        m_buffer.append(extra);
        processBuffer();
    }
    const QString netErrorText = reply->errorString();
    reply->deleteLater();

    if (m_cancelled) {
        reset();
        if (m_promptTokens > 0 || m_completionTokens > 0)
            emit usageReceived(m_promptTokens, m_completionTokens, m_cachedTokens);
        emit finished(m_fullText, m_fullReasoning);
        return;
    }

    if (netError != QNetworkReply::NoError && m_fullText.isEmpty() && m_fullReasoning.isEmpty()) {
        QString detail = netErrorText;
        const QByteArray trimmed = extra.trimmed();
        if (!trimmed.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
            if (doc.isObject()) {
                const QString apiMessage = doc.object()
                                               .value(QStringLiteral("error")).toString();
                if (!apiMessage.isEmpty())
                    detail = apiMessage;
            }
        }
        reset();
        emit failed(detail);
        return;
    }

    reset();
    if (m_promptTokens > 0 || m_completionTokens > 0)
        emit usageReceived(m_promptTokens, m_completionTokens, m_cachedTokens);
    emit finished(m_fullText, m_fullReasoning);
}

void StreamClient::reset()
{
    m_buffer.clear();
    m_cancelled = false;
    m_currentEvent.clear();
}
