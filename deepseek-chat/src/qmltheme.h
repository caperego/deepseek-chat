#pragma once

#include <QColor>
#include <QObject>

class QmlTheme : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(int chatFontSize READ chatFontSize WRITE setChatFontSize NOTIFY changed)
    Q_PROPERTY(int sidebarFontSize READ sidebarFontSize NOTIFY changed)
    Q_PROPERTY(int codeFontSize READ codeFontSize NOTIFY changed)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(QString chatFontFamily READ chatFontFamily NOTIFY changed)
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor sidebar READ sidebar NOTIFY changed)
    Q_PROPERTY(QColor card READ card NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor userBubble READ userBubble NOTIFY changed)
    Q_PROPERTY(QColor systemBubble READ systemBubble NOTIFY changed)
    Q_PROPERTY(QColor inputBg READ inputBg NOTIFY changed)
    Q_PROPERTY(QColor selected READ selected NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)

public:
    explicit QmlTheme(QObject *parent = nullptr);

    void applyFromConfig();

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool dark() const { return m_dark; }
    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);
    int chatFontSize() const { return m_chatFontSize; }
    void setChatFontSize(int size);
    int sidebarFontSize() const { return m_sidebarFontSize; }
    int codeFontSize() const { return m_codeFontSize; }
    QString fontFamily() const { return m_fontFamily; }
    QString chatFontFamily() const { return m_chatFontFamily; }

    QColor bg() const { return m_bg; }
    QColor sidebar() const { return m_sidebar; }
    QColor card() const { return m_card; }
    QColor border() const { return m_border; }
    QColor text() const { return m_text; }
    QColor muted() const { return m_muted; }
    QColor accent() const { return m_accent; }
    QColor userBubble() const { return m_userBubble; }
    QColor systemBubble() const { return m_systemBubble; }
    QColor inputBg() const { return m_inputBg; }
    QColor selected() const { return m_selected; }
    QColor danger() const { return m_danger; }

signals:
    void changed();

private:
    void refresh();
    bool systemIsDark() const;

    QString m_mode = QStringLiteral("light");
    bool m_dark = false;
    int m_fontSize = 14;
    int m_chatFontSize = 15;
    int m_sidebarFontSize = 11;
    int m_codeFontSize = 12;
    QString m_fontFamily;
    QString m_chatFontFamily;
    QColor m_bg;
    QColor m_sidebar;
    QColor m_card;
    QColor m_border;
    QColor m_text;
    QColor m_muted;
    QColor m_accent;
    QColor m_userBubble;
    QColor m_systemBubble;
    QColor m_inputBg;
    QColor m_selected;
    QColor m_danger;
};
