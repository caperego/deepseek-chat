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
        m_bg = QColor(QStringLiteral("#212121"));
        m_sidebar = QColor(QStringLiteral("#171717"));
        m_card = QColor(QStringLiteral("#2a2a2a"));
        m_border = QColor(QStringLiteral("#2f2f2f"));
        m_text = QColor(QStringLiteral("#ececec"));
        m_muted = QColor(QStringLiteral("#9ca3af"));
        m_accent = QColor(QStringLiteral("#4D6BFE"));
        m_userBubble = QColor(QStringLiteral("#303030"));
        m_systemBubble = QColor(QStringLiteral("#3a3a2f"));
        m_inputBg = QColor(QStringLiteral("#303030"));
        m_selected = QColor(QStringLiteral("#2f2f2f"));
        m_danger = QColor(QStringLiteral("#f87171"));
    } else {
        m_bg = QColor(QStringLiteral("#ffffff"));
        m_sidebar = QColor(QStringLiteral("#f9f9f9"));
        m_card = QColor(QStringLiteral("#ffffff"));
        m_border = QColor(QStringLiteral("#e5e7eb"));
        m_text = QColor(QStringLiteral("#0d0d0d"));
        m_muted = QColor(QStringLiteral("#6b7280"));
        m_accent = QColor(QStringLiteral("#4D6BFE"));
        m_userBubble = QColor(QStringLiteral("#f4f4f4"));
        m_systemBubble = QColor(QStringLiteral("#fef3c7"));
        m_inputBg = QColor(QStringLiteral("#ffffff"));
        m_selected = QColor(QStringLiteral("#e5e7eb"));
        m_danger = QColor(QStringLiteral("#b91c1c"));
    }

    QFont appFont = QGuiApplication::font();
    if (appFont.pointSize() != m_fontSize) {
        appFont.setPointSize(m_fontSize);
        QGuiApplication::setFont(appFont);
    }
}
