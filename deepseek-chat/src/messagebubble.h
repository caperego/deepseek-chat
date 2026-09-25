#pragma once

#include "chattypes.h"

#include <QFrame>
#include <QString>
#include <QVector>

class QLabel;
class QTextBrowser;
class QVBoxLayout;

class MessageBubble : public QFrame {
    Q_OBJECT

public:
    enum Role { UserRole, AssistantRole, SystemRole };

    explicit MessageBubble(Role role, const QString &text = QString(),
                           QWidget *parent = nullptr);

    void setText(const QString &text);
    void appendText(const QString &chunk);
    void setReasoning(const QString &text);
    void appendReasoning(const QString &chunk);
    void setAttachments(const QVector<Attachment> &attachments);
    void showError(const QString &error);

    QString text() const { return m_text; }
    QString reasoning() const { return m_reasoning; }
    Role role() const { return m_role; }

signals:
    void fileApplyRequested(const QString &path, const QString &content);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct FileBlock {
        QString path;
        QString content;
    };

    void refresh();
    void updateHeight();
    void clearFileCards();
    QVector<FileBlock> parseFileBlocks(QString *displayText) const;

    Role m_role;
    QString m_text;
    QString m_reasoning;
    QString m_error;
    QVector<Attachment> m_attachments;
    QLabel *m_reasoningLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QLabel *m_attachmentLabel = nullptr;
    QTextBrowser *m_browser = nullptr;
    QVBoxLayout *m_fileLayout = nullptr;
    QVector<FileBlock> m_fileBlocks;
};
