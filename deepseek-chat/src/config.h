#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

inline QString defaultSttCommand()
{
#ifdef Q_OS_WIN
    return QStringLiteral(
        "whisper-cli.exe -m C:\\modelos\\ggml-small.bin -f {file} -l pt -nt");
#else
    return QStringLiteral(
        "whisper-cli -m /caminho/para/modelo.bin -f {file} -l pt -nt");
#endif
}

struct ApiConfig {
    QString baseUrl = QStringLiteral("https://SEU_DOMINIO/deepseek");
    QString token;
    qint64 userId = 0;
    QString username;
    QString displayName;
};

struct DbConfig {
    QString host = QStringLiteral("127.0.0.1");
    int port = 3306;
    QString name = QStringLiteral("deepseek_chat");
    QString user = QStringLiteral("deepseek");
    QString password;
};

struct DeepSeekConfig {
    QString apiKey;
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString defaultModel = QStringLiteral("deepseek-flash");
};

struct UiConfig {
    QString theme = QStringLiteral("light");
    int fontSize = 14;
    int chatFontSize = 15;
    int sidebarFontSize = 11;
    int codeFontSize = 12;
    QString fontFamily = QStringLiteral("Segoe UI");
    QString chatFontFamily = QStringLiteral("Segoe UI");
    int sidebarWidth = 284;
    bool sidebarCollapsed = false;
    bool enterToSend = true;
    bool showReasoning = true;
    QString permissionMode = QStringLiteral("ask");
    QString globalInstructions;
    bool notifyOnFinish = true;
    bool closeToTray = true;
    QStringList pinnedConversations;
    QHash<qint64, QStringList> conversationTags;
};

struct AudioConfig {
    bool ttsEnabled = false;
    QString ttsVoice;
    int ttsRate = 0;
    bool sttEnabled = false;
    QString sttCommand = defaultSttCommand();
};

struct PricingConfig {
    double flashInputHit = 0.003;
    double flashInputMiss = 0.15;
    double flashOutput = 0.60;
    double proInputHit = 0.022;
    double proInputMiss = 0.66;
    double proOutput = 1.98;
    double peakMultiplier = 2.0;
    bool peakEnabled = true;
};

class Config {
public:
    static Config &instance();

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    QString filePath() const { return m_filePath; }
    ApiConfig &api() { return m_api; }
    const ApiConfig &api() const { return m_api; }
    DbConfig &db() { return m_db; }
    const DbConfig &db() const { return m_db; }
    DeepSeekConfig &deepseek() { return m_ds; }
    const DeepSeekConfig &deepseek() const { return m_ds; }
    UiConfig &ui() { return m_ui; }
    const UiConfig &ui() const { return m_ui; }
    AudioConfig &audio() { return m_audio; }
    const AudioConfig &audio() const { return m_audio; }
    PricingConfig &pricing() { return m_pricing; }
    const PricingConfig &pricing() const { return m_pricing; }

    bool projectAllowsTool(qint64 projectId, const QString &tool) const;
    void allowToolForProject(qint64 projectId, const QString &tool);

private:
    Config() = default;

    QString m_filePath;
    ApiConfig m_api;
    DbConfig m_db;
    DeepSeekConfig m_ds;
    UiConfig m_ui;
    AudioConfig m_audio;
    PricingConfig m_pricing;
    QHash<qint64, QStringList> m_permissions;
};
