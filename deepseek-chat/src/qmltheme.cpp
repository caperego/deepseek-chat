#include "qmltheme.h"
#include "config.h"

#include <QFont>
#include <QGuiApplication>
#include <QStyleHints>

QmlTheme::QmlTheme(QObject *parent)
    : QObject(parent)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (m_mode == QLatin1String("system")) {
                    refresh();
                    emit changed();
                }
            });
    applyFromConfig();
}

void QmlTheme::applyFromConfig()
{
    m_mode = Config::instance().ui().theme.toLower();
    if (m_mode != QLatin1String("dark") && m_mode != QLatin1String("system"))
        m_mode = QStringLiteral("light");
    m_fontSize = Config::instance().ui().fontSize;
    m_chatFontSize = Config::instance().ui().chatFontSize;
    m_sidebarFontSize = Config::instance().ui().sidebarFontSize;
    m_codeFontSize = Config::instance().ui().codeFontSize;
    m_fontFamily = Config::instance().ui().fontFamily;
    m_chatFontFamily = Config::instance().ui().chatFontFamily;
    refresh();
    emit changed();
}

void QmlTheme::setMode(const QString &mode)
{
    const QString normalized = mode.toLower();
    if (m_mode == normalized)
        return;
    m_mode = normalized;
    refresh();
    emit changed();
}

void QmlTheme::setFontSize(int size)
{
    if (m_fontSize == size)
        return;
    m_fontSize = size;
    refresh();
    emit changed();
}

void QmlTheme::setChatFontSize(int size)
{
    if (m_chatFontSize == size)
        return;
    m_chatFontSize = size;
    emit changed();
}

bool QmlTheme::systemIsDark() const
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

void QmlTheme::refresh()
{
    if (m_mode == QLatin1String("dark"))
        m_dark = true;
    else if (m_mode == QLatin1String("system"))
        m_dark = systemIsDark();
    else
        m_dark = false;

    if (m_dark) {
        m_bg = QColor(QStringLiteral("#1b1c20"));
        m_sidebar = QColor(QStringLiteral("#151619"));
        m_card = QColor(QStringLiteral("#222327"));
        m_border = QColor(QStringLiteral("#34353a"));
        m_text = QColor(QStringLiteral("#f2f3f5"));
        m_muted = QColor(QStringLiteral("#a6acba"));
        m_accent = QColor(QStringLiteral("#6d87f7"));
        m_userBubble = QColor(QStringLiteral("#292d3b"));
        m_systemBubble = QColor(QStringLiteral("#393426"));
        m_inputBg = QColor(QStringLiteral("#22242a"));
        m_selected = QColor(QStringLiteral("#2b2e37"));
        m_danger = QColor(QStringLiteral("#f87171"));
    } else {
        m_bg = QColor(QStringLiteral("#ffffff"));
        m_sidebar = QColor(QStringLiteral("#f7f7f8"));
        m_card = QColor(QStringLiteral("#ffffff"));
        m_border = QColor(QStringLiteral("#e7e7e9"));
        m_text = QColor(QStringLiteral("#16181d"));
        m_muted = QColor(QStringLiteral("#687083"));
        m_accent = QColor(QStringLiteral("#4d6bfe"));
        m_userBubble = QColor(QStringLiteral("#f2f3f5"));
        m_systemBubble = QColor(QStringLiteral("#fef3c7"));
        m_inputBg = QColor(QStringLiteral("#ffffff"));
        m_selected = QColor(QStringLiteral("#eaeaec"));
        m_danger = QColor(QStringLiteral("#b91c1c"));
    }

    QFont appFont = QGuiApplication::font();
    if (appFont.pointSize() != m_fontSize || appFont.family() != m_fontFamily) {
        appFont.setPointSize(m_fontSize);
        appFont.setFamily(m_fontFamily);
        QGuiApplication::setFont(appFont);
    }
}
