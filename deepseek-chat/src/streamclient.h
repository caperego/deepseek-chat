#pragma once

#include "chattypes.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

class StreamClient : public QObject {
    Q_OBJECT

public:
    explicit StreamClient(QObject *parent = nullptr);

    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setToken(const QString &token) { m_token = token; }

    void send(qint64 conversationId, qint64 projectId, const QString &title,
              const QString &model, const QString &effort, bool allowImages,
              const QVector<Message> &messages,
              const QJsonArray &tools = QJsonArray(),
              bool persistUser = true, bool persist = true,
              const QJsonArray &sources = QJsonArray());
    void cancel();
    bool isStreaming() const { return m_reply != nullptr; }
    QVector<ToolCall> toolCalls() const { return m_toolCalls; }
    QString fullText() const { return m_fullText; }
    QString fullReasoning() const { return m_fullReasoning; }

signals:
    void metaReceived(qint64 conversationId);
    void chunkReceived(const QString &text);
    void reasoningReceived(const QString &text);
    void usageReceived(int promptTokens, int completionTokens, int cachedTokens);
    void finished(const QString &fullText, const QString &fullReasoning);
    void failed(const QString &error);

private:
    void onReadyRead();
    void onReplyFinished();
    void processBuffer();
    QJsonValue buildContent(const Message &message, bool allowImages) const;
    void reset();

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QByteArray m_buffer;
    QByteArray m_responsePrefix;
    QString m_baseUrl;
    QString m_token;
    QString m_currentEvent;
    QString m_fullText;
    QString m_fullReasoning;
    QString m_streamError;
    QString m_finishReason;
    QVector<ToolCall> m_toolCalls;
    int m_promptTokens = 0;
    int m_completionTokens = 0;
    int m_cachedTokens = 0;
    bool m_cancelled = false;
    bool m_done = false;
    int m_timeout = 600000;
};
