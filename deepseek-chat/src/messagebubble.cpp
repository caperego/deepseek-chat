#include "messagebubble.h"
#include "config.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

MessageBubble::MessageBubble(Role role, const QString &text, QWidget *parent)
    : QFrame(parent),
      m_role(role),
      m_text(text)
{
    setObjectName(QStringLiteral("messageBubble"));
    setProperty("chatRole",
                role == UserRole ? QStringLiteral("user")
                                 : (role == AssistantRole ? QStringLiteral("assistant")
                                                          : QStringLiteral("system")));
    setFrameShape(QFrame::NoFrame);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(role == UserRole ? 14 : 2, role == UserRole ? 10 : 4,
                               role == UserRole ? 14 : 2, role == UserRole ? 10 : 4);
    layout->setSpacing(6);

    m_attachmentLabel = new QLabel(this);
    m_attachmentLabel->setObjectName(QStringLiteral("attachmentLabel"));
    m_attachmentLabel->setWordWrap(true);
    m_attachmentLabel->hide();
    layout->addWidget(m_attachmentLabel);

    m_reasoningLabel = new QLabel(this);
    m_reasoningLabel->setObjectName(QStringLiteral("reasoningLabel"));
    m_reasoningLabel->setWordWrap(true);
    m_reasoningLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_reasoningLabel->hide();
    layout->addWidget(m_reasoningLabel);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorLabel->hide();
    layout->addWidget(m_errorLabel);

    m_browser = new QTextBrowser(this);
    m_browser->setFrameShape(QFrame::NoFrame);
    m_browser->setOpenExternalLinks(true);
    m_browser->setReadOnly(true);
    m_browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_browser->setTextInteractionFlags(Qt::TextSelectableByMouse
                                       | Qt::LinksAccessibleByMouse);
    m_browser->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_browser);

    m_fileLayout = new QVBoxLayout;
    m_fileLayout->setContentsMargins(0, 0, 0, 0);
    m_fileLayout->setSpacing(6);
    layout->addLayout(m_fileLayout);

    connect(m_browser, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                QMenu menu;
                QAction *copy = menu.addAction(QStringLiteral("Copiar mensagem"));
                if (menu.exec(m_browser->mapToGlobal(pos)) == copy)
                    QApplication::clipboard()->setText(m_text);
            });

    setStyleSheet(QString());

    refresh();
}

void MessageBubble::setText(const QString &text)
{
    m_text = text;
    refresh();
}

void MessageBubble::appendText(const QString &chunk)
{
    m_text += chunk;
    refresh();
}

void MessageBubble::setReasoning(const QString &text)
{
    m_reasoning = text;
    refresh();
}

void MessageBubble::appendReasoning(const QString &chunk)
{
    m_reasoning += chunk;
    refresh();
}

void MessageBubble::setAttachments(const QVector<Attachment> &attachments)
{
    m_attachments = attachments;
    if (attachments.isEmpty()) {
        m_attachmentLabel->hide();
        return;
    }
    QStringList names;
    for (const Attachment &a : attachments)
        names.append(a.isImage() ? QStringLiteral("[imagem] ") + a.name : a.name);
    m_attachmentLabel->setText(QStringLiteral("Anexos: ") + names.join(QStringLiteral(", ")));
    m_attachmentLabel->show();
    updateHeight();
}

void MessageBubble::showError(const QString &error)
{
    m_error = error;
    m_errorLabel->setText(QStringLiteral("Erro: ") + error);
    m_errorLabel->show();
    updateHeight();
}

QVector<MessageBubble::FileBlock> MessageBubble::parseFileBlocks(QString *displayText) const
{
    QVector<FileBlock> blocks;
    static const QRegularExpression re(
        QStringLiteral("```file:([^\\n`]+)\\n([\\s\\S]*?)```"),
        QRegularExpression::MultilineOption);

    QString display = m_text;
    QRegularExpressionMatchIterator it = re.globalMatch(m_text);
    int removed = 0;
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        FileBlock block;
        block.path = match.captured(1).trimmed();
        block.content = match.captured(2);
        if (block.content.endsWith(QLatin1Char('\n')))
            block.content.chop(1);
        blocks.append(block);
        display.remove(match.capturedStart() - removed, match.capturedLength());
        removed += match.capturedLength();
    }

    const int partial = display.lastIndexOf(QStringLiteral("```file:"));
    if (partial >= 0 && display.indexOf(QStringLiteral("```"), partial + 3) < 0)
        display.truncate(partial);

    if (displayText)
        *displayText = display;
    return blocks;
}

void MessageBubble::clearFileCards()
{
    while (m_fileLayout->count() > 0) {
        QLayoutItem *item = m_fileLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_fileBlocks.clear();
}

void MessageBubble::refresh()
{
    QString display;
    const QVector<FileBlock> blocks = parseFileBlocks(&display);
    m_browser->setMarkdown(display);

    bool sameBlocks = blocks.size() == m_fileBlocks.size();
    if (sameBlocks) {
        for (int i = 0; i < blocks.size(); ++i) {
            if (blocks.at(i).path != m_fileBlocks.at(i).path
                || blocks.at(i).content != m_fileBlocks.at(i).content) {
                sameBlocks = false;
                break;
            }
        }
    }

    if (!sameBlocks) {
        clearFileCards();
        m_fileBlocks = blocks;
        for (const FileBlock &block : blocks) {
            auto *card = new QFrame(m_browser->parentWidget());
            card->setObjectName(QStringLiteral("fileCard"));
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(10, 8, 10, 8);

            auto *header = new QLabel(QStringLiteral("Arquivo sugerido: %1")
                                          .arg(QFileInfo(block.path).fileName()),
                                      card);
            header->setToolTip(block.path);
            cardLayout->addWidget(header);

            auto *pathLabel = new QLabel(block.path, card);
            pathLabel->setStyleSheet(QStringLiteral("font-weight: normal; color: #6b7280;"));
            cardLayout->addWidget(pathLabel);

            auto *actions = new QHBoxLayout;
            auto *applyButton = new QPushButton(QStringLiteral("Salvar no projeto"), card);
            auto *viewButton = new QPushButton(QStringLiteral("Prévia"), card);
            actions->addWidget(applyButton);
            actions->addWidget(viewButton);
            actions->addStretch(1);
            cardLayout->addLayout(actions);

            const QString path = block.path;
            const QString content = block.content;
            connect(applyButton, &QPushButton::clicked, this,
                    [this, path, content] { emit fileApplyRequested(path, content); });
            connect(viewButton, &QPushButton::clicked, this,
                    [this, path, content] {
                        QDialog dialog(this);
                        dialog.setWindowTitle(path);
                        dialog.resize(760, 520);
                        auto *layout = new QVBoxLayout(&dialog);
                        auto *editor = new QPlainTextEdit(content, &dialog);
                        editor->setReadOnly(true);
                        layout->addWidget(editor);
                        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close,
                                                             &dialog);
                        connect(buttons, &QDialogButtonBox::rejected, &dialog,
                                &QDialog::reject);
                        layout->addWidget(buttons);
                        dialog.exec();
                    });

            m_fileLayout->addWidget(card);
        }
    } else {
        m_fileBlocks = blocks;
    }

    if (!m_reasoning.isEmpty() && Config::instance().ui().showReasoning) {
        QString preview = m_reasoning;
        if (preview.size() > 4000)
            preview = preview.left(4000) + QStringLiteral("...");
        m_reasoningLabel->setText(QStringLiteral("Raciocínio: ") + preview);
        m_reasoningLabel->show();
    } else {
        m_reasoningLabel->hide();
    }

    updateHeight();
}

void MessageBubble::updateHeight()
{
    const int width = qMax(200, m_browser->viewport()->width());
    m_browser->document()->setTextWidth(width);
    const int height = qCeil(m_browser->document()->size().height()) + 6;
    m_browser->setFixedHeight(height);
    updateGeometry();
}

void MessageBubble::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateHeight();
}
