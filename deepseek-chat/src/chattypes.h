#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantList>

struct User {
    qint64 id = 0;
    QString username;
    QString displayName;
};

struct ModelInfo {
    QString id;
    QString name;
    int contextWindow = 0;
    int maxOutputTokens = 0;
    bool supportsImages = false;
    QStringList efforts;
    QString defaultEffort;
};

struct Project {
    qint64 id = 0;
    qint64 userId = 0;
    QString name;
    QString description;
    QString workdir;
    QString systemPrompt;
    QString defaultModel;
    QString defaultEffort;
    QString permissionMode;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct ProjectItem {
    qint64 id = 0;
    qint64 projectId = 0;
    QString kind;
    QString name;
    QString path;
    QString mime;
    qint64 size = 0;
    QDateTime createdAt;

    bool isImage() const { return kind == QLatin1String("image"); }
    bool isFile() const { return kind == QLatin1String("file"); }
    bool isNote() const { return kind == QLatin1String("note"); }
};

struct Command {
    qint64 id = 0;
    qint64 projectId = 0;
    QString name;
    QString prompt;
};

struct Attachment {
    QString kind;
    QString name;
    QString path;
    QString mime;
    QString text;
    QByteArray data;

    bool isImage() const { return kind == QLatin1String("image"); }
};

struct ToolCall {
    QString id;
    QString name;
    QString arguments;
};

struct Conversation {
    qint64 id = 0;
    qint64 userId = 0;
    qint64 projectId = 0;
    qint64 parentId = 0;
    qint64 forkMessageId = 0;
    QString title;
    QString model;
    QString effort;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct Message {
    qint64 id = 0;
    qint64 conversationId = 0;
    QString role;
    QString content;
    QString reasoning;
    QString model;
    QString toolCallId;
    QString toolSummary; // Local presentation only; never sent as tool arguments.
    QVector<ToolCall> toolCalls;
    QVector<Attachment> attachments;
    QVariantList sources;
    qint64 promptTokens = 0;
    qint64 completionTokens = 0;
    qint64 cachedTokens = 0;
    QDateTime createdAt;
};

struct UsageTotals {
    QString model;
    qint64 prompt = 0;
    qint64 completion = 0;
    qint64 cached = 0;
    qint64 messages = 0;

    qint64 tokens() const { return prompt + completion; }
};

struct Activity {
    qint64 id = 0;
    QString action;
    QString details;
    QDateTime createdAt;
};

struct SearchHit {
    qint64 conversationId = 0;
    QString title;
    QString role;
    QString snippet;
    QDateTime createdAt;
};
