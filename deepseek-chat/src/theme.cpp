#include "theme.h"
#include "config.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPair>
#include <QPalette>
#include <QVector>

namespace {

struct Colors {
    QString window;
    QString sidebar;
    QString border;
    QString text;
    QString muted;
    QString userBubble;
    QString systemBubble;
    QString card;
    QString cardBorder;
    QString input;
    QString inputBorder;
    QString hover;
    QString selected;
    QString codeBg;
    QString reasoningBorder;
    QString errorText;
    QString link;
};

Colors lightColors()
{
    return {
        QStringLiteral("#ffffff"), QStringLiteral("#f9f9f9"), QStringLiteral("#e5e7eb"),
        QStringLiteral("#0d0d0d"), QStringLiteral("#6b7280"), QStringLiteral("#f4f4f4"),
        QStringLiteral("#fef3c7"), QStringLiteral("#ffffff"), QStringLiteral("#e5e7eb"),
        QStringLiteral("#ffffff"), QStringLiteral("#d1d5db"), QStringLiteral("#f3f4f6"),
        QStringLiteral("#e5e7eb"), QStringLiteral("#f6f8fa"), QStringLiteral("#d1d5db"),
        QStringLiteral("#b91c1c"), QStringLiteral("#2563eb")
    };
}

Colors darkColors()
{
    return {
        QStringLiteral("#212121"), QStringLiteral("#171717"), QStringLiteral("#2f2f2f"),
        QStringLiteral("#ececec"), QStringLiteral("#9ca3af"), QStringLiteral("#303030"),
        QStringLiteral("#3a3a2f"), QStringLiteral("#2a2a2a"), QStringLiteral("#3a3a3a"),
        QStringLiteral("#303030"), QStringLiteral("#3a3a3a"), QStringLiteral("#2a2a2a"),
        QStringLiteral("#2f2f2f"), QStringLiteral("#2a2a2a"), QStringLiteral("#4b5563"),
        QStringLiteral("#f87171"), QStringLiteral("#60a5fa")
    };
}

QString buildStyleSheet(const Colors &c)
{
    QString qss = QStringLiteral(
        "QWidget#sidebar { background: @sidebar; border-right: 1px solid @border; }"
        "QWidget#chatHeader { background: @window; border-bottom: 1px solid @border; }"
        "QWidget#messagesHost { background: @window; }"
        "QScrollArea { background: @window; border: none; }"
        "QMainWindow, QDialog { background: @window; color: @text; }"
        "QTreeWidget { background: transparent; border: none; color: @text; }"
        "QTreeWidget::item { padding: 5px 4px; border-radius: 6px; }"
        "QTreeWidget::item:selected { background: @selected; color: @text; }"
        "QPushButton { padding: 6px 10px; border-radius: 8px;"
        "  border: 1px solid @inputBorder; background: @input; color: @text; }"
        "QPushButton:hover { background: @hover; }"
        "QPlainTextEdit#composerInput { border: none; background: transparent;"
        "  color: @text; }"
        "QWidget#composer { background: @window; border-top: 1px solid @border; }"
        "QLineEdit#searchInput { border: 1px solid @inputBorder; border-radius: 8px;"
        "  padding: 6px 8px; background: @input; color: @text; }"
        "QComboBox, QLineEdit, QPlainTextEdit, QListWidget, QTableWidget,"
        " QSpinBox, QTextEdit {"
        "  background: @input; color: @text; border: 1px solid @inputBorder;"
        "  border-radius: 6px; }"
        "QListWidget::item:selected, QTableWidget::item:selected {"
        "  background: @selected; color: @text; }"
        "QLabel { color: @text; }"
        "QLabel#reasoningLabel { color: @muted; font-style: italic;"
        "  border-left: 3px solid @reasoningBorder; padding-left: 8px; }"
        "QLabel#errorLabel { color: @errorText; font-weight: bold; }"
        "QLabel#attachmentLabel { color: @muted; }"
        "QFrame#messageBubble[chatRole=\"user\"] {"
        "  background: @userBubble; border-radius: 18px; }"
        "QFrame#messageBubble[chatRole=\"assistant\"] { background: transparent; }"
        "QFrame#messageBubble[chatRole=\"system\"] {"
        "  background: @systemBubble; border-radius: 14px; }"
        "QFrame#messageBubble QTextBrowser { background: transparent; color: @text; }"
        "QTextBrowser { background: @window; color: @text; border: none; }"
        "QFrame#fileCard { background: @card; border: 1px solid @cardBorder;"
        "  border-radius: 10px; }"
        "QFrame#fileCard QLabel { color: @text; font-weight: bold; }"
        "QTabWidget::pane { background: @window; border: 1px solid @border; }"
        "QTabBar::tab { color: @text; padding: 6px 10px; }"
        "QTabBar::tab:selected { background: @selected; border-radius: 6px; }"
        "QToolTip { background: @card; color: @text; border: 1px solid @cardBorder; }"
        "QStatusBar { color: @muted; }"
        "a { color: @link; }");

    const QVector<QPair<QString, QString>> tokens = {
        {QStringLiteral("@window"), c.window},
        {QStringLiteral("@sidebar"), c.sidebar},
        {QStringLiteral("@border"), c.border},
        {QStringLiteral("@text"), c.text},
        {QStringLiteral("@muted"), c.muted},
        {QStringLiteral("@userBubble"), c.userBubble},
        {QStringLiteral("@systemBubble"), c.systemBubble},
        {QStringLiteral("@card"), c.card},
        {QStringLiteral("@cardBorder"), c.cardBorder},
        {QStringLiteral("@input"), c.input},
        {QStringLiteral("@inputBorder"), c.inputBorder},
        {QStringLiteral("@hover"), c.hover},
        {QStringLiteral("@selected"), c.selected},
        {QStringLiteral("@codeBg"), c.codeBg},
        {QStringLiteral("@reasoningBorder"), c.reasoningBorder},
        {QStringLiteral("@errorText"), c.errorText},
        {QStringLiteral("@link"), c.link}
    };

    for (const auto &token : tokens)
        qss.replace(token.first, token.second);
    return qss;
}

void applyPalette(QApplication &app, const Colors &c, const QString &mode)
{
    QPalette palette;
    const QColor window(c.window);
    const QColor text(c.text);
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor(c.input));
    palette.setColor(QPalette::AlternateBase, QColor(c.sidebar));
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, QColor(c.input));
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, QColor(c.errorText));
    palette.setColor(QPalette::Highlight, QColor(c.selected));
    palette.setColor(QPalette::HighlightedText, text);
    palette.setColor(QPalette::ToolTipBase, QColor(c.card));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, QColor(c.muted));
    palette.setColor(QPalette::Link, QColor(c.link));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(c.muted));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.muted));
    app.setPalette(palette);

    QFont font = app.font();
    font.setPointSize(Config::instance().ui().fontSize);
    app.setFont(font);

    app.setStyleSheet(buildStyleSheet(c));
    Q_UNUSED(mode);
}

}

namespace Theme {

QString normalize(const QString &mode)
{
    return mode.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("dark") : QStringLiteral("light");
}

bool isDark(const QString &mode)
{
    return normalize(mode) == QLatin1String("dark");
}

QString styleSheet(const QString &mode)
{
    return buildStyleSheet(isDark(mode) ? darkColors() : lightColors());
}

void apply(QApplication &app, const QString &mode)
{
    if (isDark(mode))
        applyPalette(app, darkColors(), mode);
    else
        applyPalette(app, lightColors(), mode);
}

}
