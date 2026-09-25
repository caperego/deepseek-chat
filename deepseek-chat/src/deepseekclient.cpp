#include "deepseekclient.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this))
{
}

void DeepSeekClient::fetchModels()
{
    if (m_modelsReply)
        return;

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/models")));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setTransferTimeout(30000);

    m_modelsReply = m_nam->get(request);
    connect(m_modelsReply, &QNetworkReply::finished, this,
            &DeepSeekClient::onModelsFinished);
}

void DeepSeekClient::send(const QString &model, const QString &effort, bool allowImages,
                          const QVector<Message> &messages)
{
    if (isStreaming())
        return;

    QJsonArray msgs;
    for (const Message &m : messages) {
        if (m.role != QLatin1String("user") && m.role != QLatin1String("assistant")
            && m.role != QLatin1String("system"))
            continue;
        if (m.role == QLatin1String("assistant") && m.content.isEmpty())
            continue;
        QJsonObject o;
        o.insert(QStringLiteral("role"), m.role);
        o.insert(QStringLiteral("content"), buildContent(m, allowImages));
        msgs.append(o);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), model);
    body.insert(QStringLiteral("messages"), msgs);
    body.insert(QStringLiteral("stream"), true);
    if (!effort.isEmpty())
        body.insert(QStringLiteral("effort"), effort);
    QJsonObject streamOptions;
    streamOptions.insert(QStringLiteral("include_usage"), true);
    body.insert(QStringLiteral("stream_options"), streamOptions);

    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    request.setRawHeader("Accept", "text/event-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeout);

    m_fullText.clear();
    m_fullReasoning.clear();
    m_promptTokens = 0;
    m_completionTokens = 0;
    m_cachedTokens = 0;
    m_buffer.clear();
    m_cancelled = false;

    m_reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QIODevice::readyRead, this, &DeepSeekClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &DeepSeekClient::onReplyFinished);
}

QJsonValue DeepSeekClient::buildContent(const Message &message, bool allowImages) const
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

void DeepSeekClient::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void DeepSeekClient::onReadyRead()
{
    if (!m_reply)
        return;
    m_buffer.append(m_reply->readAll());
    processBuffer();
}

void DeepSeekClient::processBuffer()
{
    int index = -1;
    while ((index = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(index).trimmed();
        m_buffer.remove(0, index + 1);

        if (line.isEmpty() || !line.startsWith(QByteArrayLiteral("data:")))
            continue;

        const QByteArray payload = line.mid(5).trimmed();
        if (payload.isEmpty() || payload == QByteArrayLiteral("[DONE]"))
            continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject obj = doc.object();

        const QJsonValue usageValue = obj.value(QStringLiteral("usage"));
        if (usageValue.isObject()) {
            const QJsonObject usage = usageValue.toObject();
            m_promptTokens = usage.value(QStringLiteral("prompt_tokens")).toInt();
            m_completionTokens = usage.value(QStringLiteral("completion_tokens")).toInt();
            m_cachedTokens = usage.value(QStringLiteral("prompt_tokens_details"))
                                 .toObject().value(QStringLiteral("cached_tokens")).toInt();
            if (m_cachedTokens == 0)
                m_cachedTokens = usage.value(QStringLiteral("prompt_cache_hit_tokens")).toInt();
        }

        const QJsonArray choices = obj.value(QStringLiteral("choices")).toArray();
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

void DeepSeekClient::onReplyFinished()
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

    if (netError != QNetworkReply::NoError) {
        QString detail = netErrorText;
        const QByteArray trimmed = extra.trimmed();
        if (!trimmed.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(trimmed);
            if (doc.isObject()) {
                const QJsonObject err = doc.object()
                                            .value(QStringLiteral("error")).toObject();
                const QString apiMessage = err.value(QStringLiteral("message")).toString();
                if (!apiMessage.isEmpty())
                    detail = apiMessage;
            } else {
                detail += QStringLiteral("\n") + QString::fromUtf8(trimmed.left(400));
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

void DeepSeekClient::onModelsFinished()
{
    if (!m_modelsReply)
        return;

    QNetworkReply *reply = m_modelsReply;
    m_modelsReply = nullptr;

    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    reply->deleteLater();

    if (netError != QNetworkReply::NoError) {
        emit modelsFailed(netErrorText);
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonArray list = doc.object().value(QStringLiteral("data")).toArray();

    QVector<ModelInfo> models;
    for (const QJsonValue &value : list) {
        const QJsonObject o = value.toObject();
        ModelInfo info;
        info.id = o.value(QStringLiteral("id")).toString();
        info.name = o.value(QStringLiteral("name")).toString();
        if (info.name.isEmpty())
            info.name = info.id;
        info.contextWindow = o.value(QStringLiteral("context_window")).toInt();
        info.maxOutputTokens = o.value(QStringLiteral("max_output_tokens")).toInt();

        const QJsonArray modalities = o.value(QStringLiteral("input_modalities")).toArray();
        for (const QJsonValue &m : modalities) {
            if (m.toString() == QLatin1String("image"))
                info.supportsImages = true;
        }

        const QJsonObject effort = o.value(QStringLiteral("effort")).toObject();
        const QJsonArray levels = effort.value(QStringLiteral("supported_levels")).toArray();
        for (const QJsonValue &l : levels)
            info.efforts.append(l.toString());
        info.defaultEffort = effort.value(QStringLiteral("default_level")).toString();

        if (!info.id.isEmpty())
            models.append(info);
    }

    emit modelsFetched(models);
}

void DeepSeekClient::reset()
{
    m_buffer.clear();
    m_cancelled = false;
}
