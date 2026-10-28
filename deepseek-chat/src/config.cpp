#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

QByteArray protectData(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()));
    input.cbData = static_cast<DWORD>(data.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"deepseek-chat", nullptr, nullptr, nullptr, 0,
                          &output))
        return QByteArray();
    QByteArray result(reinterpret_cast<const char *>(output.pbData),
                      static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    return data;
#endif
}

QByteArray unprotectData(const QByteArray &data)
{
#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()));
    input.cbData = static_cast<DWORD>(data.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output))
        return QByteArray();
    QByteArray result(reinterpret_cast<const char *>(output.pbData),
                      static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    return result;
#else
    return data;
#endif
}

QString encodeToken(const QString &token)
{
    const QByteArray protectedData = protectData(token.toUtf8());
    if (protectedData.isEmpty())
        return QString();
#ifdef Q_OS_WIN
    return QStringLiteral("dpapi:") + QString::fromLatin1(protectedData.toBase64());
#else
    return QStringLiteral("b64:") + QString::fromLatin1(protectedData.toBase64());
#endif
}

QString decodeToken(const QString &value)
{
    if (value.startsWith(QLatin1String("dpapi:"))) {
#ifdef Q_OS_WIN
        const QByteArray decoded = QByteArray::fromBase64(value.mid(6).toLatin1());
        return QString::fromUtf8(unprotectData(decoded));
#else
        return QString();
#endif
    }
    if (value.startsWith(QLatin1String("b64:")))
        return QString::fromUtf8(QByteArray::fromBase64(value.mid(4).toLatin1()));
    return value;
}

}

Config &Config::instance()
{
    static Config cfg;
    return cfg;
}

bool Config::load(QString *error)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/config.ini"),
        appDir + QStringLiteral("/../config.ini"),
        QDir::currentPath() + QStringLiteral("/config.ini")
    };

    m_filePath.clear();
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            m_filePath = QFileInfo(candidate).absoluteFilePath();
            break;
        }
    }

    if (m_filePath.isEmpty())
        m_filePath = QDir::currentPath() + QStringLiteral("/config.ini");

    QSettings s(m_filePath, QSettings::IniFormat);

    s.beginGroup(QStringLiteral("api"));
    m_api.baseUrl = s.value(QStringLiteral("base_url"), m_api.baseUrl).toString();
    const QString plainToken = s.value(QStringLiteral("token"), QString()).toString();
    const QString encryptedToken = s.value(QStringLiteral("token_enc"), QString()).toString();
    if (!encryptedToken.isEmpty())
        m_api.token = decodeToken(encryptedToken);
    else if (!plainToken.isEmpty())
        m_api.token = plainToken;
    m_api.userId = s.value(QStringLiteral("user_id"), m_api.userId).toLongLong();
    m_api.username = s.value(QStringLiteral("username"), m_api.username).toString();
    m_api.displayName = s.value(QStringLiteral("display_name"), m_api.displayName).toString();
    s.endGroup();

    m_ui.conversationTags.clear();
    s.beginGroup(QStringLiteral("conversation_tags"));
    for (const QString &key : s.childKeys()) {
        const qint64 id = key.toLongLong();
        const QStringList tags = s.value(key).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (id > 0 && !tags.isEmpty())
            m_ui.conversationTags.insert(id, tags);
    }
    s.endGroup();

    s.beginGroup(QStringLiteral("database"));
    m_db.host = s.value(QStringLiteral("host"), m_db.host).toString();
    m_db.port = s.value(QStringLiteral("port"), m_db.port).toInt();
    m_db.name = s.value(QStringLiteral("name"), m_db.name).toString();
    m_db.user = s.value(QStringLiteral("user"), m_db.user).toString();
    m_db.password = s.value(QStringLiteral("password"), m_db.password).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("deepseek"));
    m_ds.apiKey = s.value(QStringLiteral("api_key"), m_ds.apiKey).toString();
    m_ds.baseUrl = s.value(QStringLiteral("base_url"), m_ds.baseUrl).toString();
    m_ds.defaultModel = s.value(QStringLiteral("model"), m_ds.defaultModel).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("ui"));
    m_ui.theme = s.value(QStringLiteral("theme"), m_ui.theme).toString();
    m_ui.fontSize = s.value(QStringLiteral("font_size"), m_ui.fontSize).toInt();
    m_ui.sidebarFontSize = qBound(9, s.value(QStringLiteral("sidebar_font_size"), m_ui.sidebarFontSize).toInt(), 22);
    m_ui.codeFontSize = qBound(9, s.value(QStringLiteral("code_font_size"), m_ui.codeFontSize).toInt(), 28);
    m_ui.fontFamily = s.value(QStringLiteral("font_family"), m_ui.fontFamily).toString();
    m_ui.chatFontFamily = s.value(QStringLiteral("chat_font_family"), m_ui.chatFontFamily).toString();
    m_ui.sidebarWidth = qBound(220, s.value(QStringLiteral("sidebar_width"), m_ui.sidebarWidth).toInt(), 480);
    m_ui.sidebarCollapsed = s.value(QStringLiteral("sidebar_collapsed"), false).toBool();
    m_ui.pinnedConversations = s.value(QStringLiteral("pinned_conversations")).toString()
                                 .split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_ui.chatFontSize = s.value(QStringLiteral("chat_font_size"),
                                m_ui.chatFontSize).toInt();
    m_ui.enterToSend = s.value(QStringLiteral("enter_to_send"), m_ui.enterToSend).toBool();
    m_ui.showReasoning = s.value(QStringLiteral("show_reasoning"),
                                 m_ui.showReasoning).toBool();
    m_ui.permissionMode = s.value(QStringLiteral("permission_mode"),
                                  m_ui.permissionMode).toString();
    m_ui.globalInstructions = s.value(QStringLiteral("global_instructions"),
                                      m_ui.globalInstructions).toString();
    m_ui.notifyOnFinish = s.value(QStringLiteral("notify_on_finish"),
                                  m_ui.notifyOnFinish).toBool();
    m_ui.closeToTray = s.value(QStringLiteral("close_to_tray"),
                               m_ui.closeToTray).toBool();
    m_ui.openLinksInternally = s.value(QStringLiteral("open_links_internally"),
                                       m_ui.openLinksInternally).toBool();
    m_ui.browserPersistent = s.value(QStringLiteral("browser_persistent"),
                                     m_ui.browserPersistent).toBool();
    s.endGroup();

    m_permissions.clear();
    s.beginGroup(QStringLiteral("permissions"));
    for (const QString &key : s.childKeys()) {
        if (!key.startsWith(QLatin1String("project_")))
            continue;
        const qint64 projectId = key.mid(8).toLongLong();
        if (projectId <= 0)
            continue;
        const QStringList tools = s.value(key).toString()
                                      .split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!tools.isEmpty())
            m_permissions.insert(projectId, tools);
    }
    s.endGroup();

    s.beginGroup(QStringLiteral("audio"));
    m_audio.ttsEnabled = s.value(QStringLiteral("tts_enabled"),
                                 m_audio.ttsEnabled).toBool();
    m_audio.ttsVoice = s.value(QStringLiteral("tts_voice"), m_audio.ttsVoice).toString();
    m_audio.ttsRate = s.value(QStringLiteral("tts_rate"), m_audio.ttsRate).toInt();
    m_audio.sttEnabled = s.value(QStringLiteral("stt_enabled"),
                                 m_audio.sttEnabled).toBool();
    m_audio.sttCommand = s.value(QStringLiteral("stt_command"),
                                 m_audio.sttCommand).toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("pricing"));
    m_pricing.flashInputHit = s.value(QStringLiteral("flash_input_hit"),
                                      m_pricing.flashInputHit).toDouble();
    m_pricing.flashInputMiss = s.value(QStringLiteral("flash_input_miss"),
                                       m_pricing.flashInputMiss).toDouble();
    m_pricing.flashOutput = s.value(QStringLiteral("flash_output"),
                                    m_pricing.flashOutput).toDouble();
    m_pricing.proInputHit = s.value(QStringLiteral("pro_input_hit"),
                                    m_pricing.proInputHit).toDouble();
    m_pricing.proInputMiss = s.value(QStringLiteral("pro_input_miss"),
                                     m_pricing.proInputMiss).toDouble();
    m_pricing.proOutput = s.value(QStringLiteral("pro_output"),
                                  m_pricing.proOutput).toDouble();
    m_pricing.peakMultiplier = s.value(QStringLiteral("peak_multiplier"),
                                       m_pricing.peakMultiplier).toDouble();
    m_pricing.peakEnabled = s.value(QStringLiteral("peak_enabled"),
                                    m_pricing.peakEnabled).toBool();
    s.endGroup();

    if (error)
        error->clear();
    return true;
}

bool Config::save(QString *error) const
{
    if (m_filePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("Caminho do config.ini não definido.");
        return false;
    }

    QSettings s(m_filePath, QSettings::IniFormat);

    s.beginGroup(QStringLiteral("api"));
    s.setValue(QStringLiteral("base_url"), m_api.baseUrl);
    if (m_api.token.isEmpty()) {
        s.remove(QStringLiteral("token"));
        s.remove(QStringLiteral("token_enc"));
    } else {
        s.remove(QStringLiteral("token"));
        const QString encoded = encodeToken(m_api.token);
        if (encoded.isEmpty())
            s.setValue(QStringLiteral("token"), m_api.token);
        else
            s.setValue(QStringLiteral("token_enc"), encoded);
    }
    s.setValue(QStringLiteral("user_id"), m_api.userId);
    s.setValue(QStringLiteral("username"), m_api.username);
    s.setValue(QStringLiteral("display_name"), m_api.displayName);
    s.endGroup();

    s.beginGroup(QStringLiteral("database"));
    s.setValue(QStringLiteral("host"), m_db.host);
    s.setValue(QStringLiteral("port"), m_db.port);
    s.setValue(QStringLiteral("name"), m_db.name);
    s.setValue(QStringLiteral("user"), m_db.user);
    s.setValue(QStringLiteral("password"), m_db.password);
    s.endGroup();

    s.beginGroup(QStringLiteral("deepseek"));
    s.setValue(QStringLiteral("api_key"), m_ds.apiKey);
    s.setValue(QStringLiteral("base_url"), m_ds.baseUrl);
    s.setValue(QStringLiteral("model"), m_ds.defaultModel);
    s.endGroup();

    s.beginGroup(QStringLiteral("ui"));
    s.setValue(QStringLiteral("theme"), m_ui.theme);
    s.setValue(QStringLiteral("font_size"), m_ui.fontSize);
    s.setValue(QStringLiteral("chat_font_size"), m_ui.chatFontSize);
    s.setValue(QStringLiteral("sidebar_font_size"), m_ui.sidebarFontSize);
    s.setValue(QStringLiteral("code_font_size"), m_ui.codeFontSize);
    s.setValue(QStringLiteral("font_family"), m_ui.fontFamily);
    s.setValue(QStringLiteral("chat_font_family"), m_ui.chatFontFamily);
    s.setValue(QStringLiteral("sidebar_width"), m_ui.sidebarWidth);
    s.setValue(QStringLiteral("sidebar_collapsed"), m_ui.sidebarCollapsed);
    s.setValue(QStringLiteral("pinned_conversations"), m_ui.pinnedConversations.join(QLatin1Char(',')));
    s.setValue(QStringLiteral("enter_to_send"), m_ui.enterToSend);
    s.setValue(QStringLiteral("show_reasoning"), m_ui.showReasoning);
    s.setValue(QStringLiteral("permission_mode"), m_ui.permissionMode);
    s.setValue(QStringLiteral("global_instructions"), m_ui.globalInstructions);
    s.setValue(QStringLiteral("notify_on_finish"), m_ui.notifyOnFinish);
    s.setValue(QStringLiteral("close_to_tray"), m_ui.closeToTray);
    s.setValue(QStringLiteral("open_links_internally"), m_ui.openLinksInternally);
    s.setValue(QStringLiteral("browser_persistent"), m_ui.browserPersistent);
    s.endGroup();

    s.beginGroup(QStringLiteral("conversation_tags"));
    s.remove(QString());
    for (auto it = m_ui.conversationTags.constBegin(); it != m_ui.conversationTags.constEnd(); ++it)
        s.setValue(QString::number(it.key()), it.value().join(QLatin1Char(',')));
    s.endGroup();

    s.beginGroup(QStringLiteral("audio"));
    s.setValue(QStringLiteral("tts_enabled"), m_audio.ttsEnabled);
    s.setValue(QStringLiteral("tts_voice"), m_audio.ttsVoice);
    s.setValue(QStringLiteral("tts_rate"), m_audio.ttsRate);
    s.setValue(QStringLiteral("stt_enabled"), m_audio.sttEnabled);
    s.setValue(QStringLiteral("stt_command"), m_audio.sttCommand);
    s.endGroup();

    s.beginGroup(QStringLiteral("pricing"));
    s.setValue(QStringLiteral("flash_input_hit"), m_pricing.flashInputHit);
    s.setValue(QStringLiteral("flash_input_miss"), m_pricing.flashInputMiss);
    s.setValue(QStringLiteral("flash_output"), m_pricing.flashOutput);
    s.setValue(QStringLiteral("pro_input_hit"), m_pricing.proInputHit);
    s.setValue(QStringLiteral("pro_input_miss"), m_pricing.proInputMiss);
    s.setValue(QStringLiteral("pro_output"), m_pricing.proOutput);
    s.setValue(QStringLiteral("peak_multiplier"), m_pricing.peakMultiplier);
    s.setValue(QStringLiteral("peak_enabled"), m_pricing.peakEnabled);
    s.endGroup();

    s.beginGroup(QStringLiteral("permissions"));
    s.remove(QString());
    for (auto it = m_permissions.constBegin(); it != m_permissions.constEnd(); ++it) {
        if (it.value().isEmpty())
            continue;
        s.setValue(QStringLiteral("project_%1").arg(it.key()),
                   it.value().join(QLatin1Char(',')));
    }
    s.endGroup();

    s.sync();
    if (s.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("Falha ao gravar %1").arg(m_filePath);
        return false;
    }
#ifdef Q_OS_UNIX
    QFile::setPermissions(m_filePath, QFile::ReadOwner | QFile::WriteOwner);
#endif
    return true;
}

bool Config::projectAllowsTool(qint64 projectId, const QString &tool) const
{
    if (projectId <= 0)
        return false;
    return m_permissions.value(projectId).contains(tool);
}

void Config::allowToolForProject(qint64 projectId, const QString &tool)
{
    if (projectId <= 0 || tool.isEmpty())
        return;
    QStringList tools = m_permissions.value(projectId);
    if (!tools.contains(tool)) {
        tools.append(tool);
        m_permissions.insert(projectId, tools);
        save();
    }
}
