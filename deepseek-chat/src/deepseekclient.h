#pragma once

#include "chattypes.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

class DeepSeekClient : public QObject {
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);

    void setApiKey(const QString &key) { m_apiKey = key; }
    void setBaseUrl(const QString &url) { m_baseUrl = url; }
    void setTimeout(int milliseconds) { m_timeout = milliseconds; }

    void fetchModels();
    void send(const QString &model, const QString &effort, bool allowImages,
              const QVector<Message> &messages);
    void cancel();
    bool isStreaming() const { return m_reply != nullptr; }

signals:
    void chunkReceived(const QString &text);
    void reasoningReceived(const QString &text);
    void finished(const QString &fullText, const QString &fullReasoning);
    void failed(const QString &error);
    void modelsFetched(const QVector<ModelInfo> &models);
    void modelsFailed(const QString &error);
    void usageReceived(int promptTokens, int completionTokens, int cachedTokens);

private:
    void onReadyRead();
    void onReplyFinished();
    void onModelsFinished();
    void processBuffer();
    void reset();

    QJsonValue buildContent(const Message &message, bool allowImages) const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_modelsReply = nullptr;
    QByteArray m_buffer;
    QString m_apiKey;
    QString m_baseUrl = QStringLiteral("https://api.deepseek.com");
    QString m_fullText;
    QString m_fullReasoning;
    int m_promptTokens = 0;
    int m_completionTokens = 0;
    int m_cachedTokens = 0;
    bool m_cancelled = false;
    int m_timeout = 180000;
};
