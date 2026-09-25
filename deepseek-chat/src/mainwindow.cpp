#include "mainwindow.h"
#include "assets.h"
#include "config.h"
#include "database.h"
#include "deepseekclient.h"
#include "settingsdialog.h"
#include "theme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kTypeRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kChatPage = 0;
constexpr int kProjectPage = 1;

bool isImageSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    return s == QLatin1String("png") || s == QLatin1String("jpg")
        || s == QLatin1String("jpeg") || s == QLatin1String("webp")
        || s == QLatin1String("gif") || s == QLatin1String("bmp");
}

QString mimeForSuffix(const QString &suffix)
{
    const QString s = suffix.toLower();
    if (s == QLatin1String("png"))
        return QStringLiteral("image/png");
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (s == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    if (s == QLatin1String("gif"))
        return QStringLiteral("image/gif");
    if (s == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    return QStringLiteral("application/octet-stream");
}

bool makeAttachment(const QString &path, Attachment *out, QString *error)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (error)
            *error = QStringLiteral("Arquivo não encontrado: %1").arg(path);
        return false;
    }

    out->path = info.absoluteFilePath();
    out->name = info.fileName();

    if (isImageSuffix(info.suffix())) {
        if (info.size() > 10LL * 1024 * 1024) {
            if (error)
                *error = QStringLiteral("Imagem maior que 10 MB: %1").arg(info.fileName());
            return false;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("Não foi possível ler %1").arg(path);
            return false;
        }
        out->kind = QStringLiteral("image");
        out->mime = mimeForSuffix(info.suffix());
        out->data = file.readAll();
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Não foi possível ler %1").arg(path);
        return false;
    }
    const QByteArray raw = file.read(420 * 1024);
    if (raw.contains('\0')) {
        if (error)
            *error = QStringLiteral("Arquivo binário não suportado: %1").arg(info.fileName());
        return false;
    }

    out->kind = QStringLiteral("file");
    out->mime = QStringLiteral("text/plain");
    out->text = QString::fromUtf8(raw);
    if (info.size() > raw.size())
        out->text += QStringLiteral("\n\n[conteúdo truncado em %1 KB]").arg(raw.size() / 1024);
    return true;
}

}

MainWindow::MainWindow(const User &user, QWidget *parent)
    : QMainWindow(parent),
      m_user(user)
{
    setWindowTitle(QStringLiteral("DeepSeek Chat — %1")
                       .arg(user.displayName.isEmpty() ? user.username
                                                       : user.displayName));
    setWindowIcon(Assets::appIcon());
    resize(1280, 840);

    buildUi();

    m_client = new DeepSeekClient(this);
    m_client->setApiKey(Config::instance().deepseek().apiKey);
    m_client->setBaseUrl(Config::instance().deepseek().baseUrl);

    connect(m_client, &DeepSeekClient::chunkReceived, this, &MainWindow::onChunk);
    connect(m_client, &DeepSeekClient::reasoningReceived, this,
            &MainWindow::onReasoning);
    connect(m_client, &DeepSeekClient::finished, this, &MainWindow::onFinished);
    connect(m_client, &DeepSeekClient::failed, this, &MainWindow::onFailed);
    connect(m_client, &DeepSeekClient::modelsFetched, this,
            &MainWindow::onModelsFetched);
    connect(m_client, &DeepSeekClient::modelsFailed, this,
            &MainWindow::onModelsFailed);

    reloadProjects();
    reloadTree();
    refreshModels();
    newConversation(0);
    m_input->setFocus();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *sidebar = new QWidget(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(280);
    buildSidebar(sidebar);
    layout->addWidget(sidebar);

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(buildChatPage());
    m_stack->addWidget(buildProjectPage());
    layout->addWidget(m_stack, 1);

    setCentralWidget(central);
}

void MainWindow::buildSidebar(QWidget *sidebar)
{
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(10, 12, 10, 12);
    layout->setSpacing(8);

    auto *brand = new QLabel(QStringLiteral("DeepSeek Chat"), sidebar);
    brand->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: bold;"));
    layout->addWidget(brand);

    m_userLabel = new QLabel(sidebar);
    m_userLabel->setText(QStringLiteral("Conectado como %1")
                             .arg(m_user.displayName.isEmpty() ? m_user.username
                                                               : m_user.displayName));
    m_userLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(m_userLabel);

    m_newChatButton = new QPushButton(QStringLiteral("+  Nova conversa"), sidebar);
    layout->addWidget(m_newChatButton);

    m_newProjectButton = new QPushButton(QStringLiteral("+  Novo projeto"), sidebar);
    layout->addWidget(m_newProjectButton);

    m_search = new QLineEdit(sidebar);
    m_search->setObjectName(QStringLiteral("searchInput"));
    m_search->setPlaceholderText(QStringLiteral("Pesquisar..."));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget(sidebar);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_tree, 1);

    auto *bottomRow = new QHBoxLayout;
    auto *activitiesButton = new QPushButton(QStringLiteral("Atividades"), sidebar);
    auto *settingsButton = new QPushButton(QStringLiteral("Config."), sidebar);
    auto *logoutButton = new QPushButton(QStringLiteral("Sair"), sidebar);
    bottomRow->addWidget(activitiesButton);
    bottomRow->addWidget(settingsButton);
    bottomRow->addWidget(logoutButton);
    layout->addLayout(bottomRow);

    connect(m_newChatButton, &QPushButton::clicked, this,
            [this] { newConversation(m_activeProjectId); });
    connect(m_newProjectButton, &QPushButton::clicked, this, &MainWindow::newProject);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::filterTree);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &MainWindow::onTreeSelection);
    connect(m_tree, &QWidget::customContextMenuRequested, this,
            &MainWindow::onTreeContextMenu);
    connect(activitiesButton, &QPushButton::clicked, this, &MainWindow::showActivities);
    connect(settingsButton, &QPushButton::clicked, this, &MainWindow::openSettings);
    connect(logoutButton, &QPushButton::clicked, this, &MainWindow::doLogout);
}

QWidget *MainWindow::buildChatPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(page);
    header->setObjectName(QStringLiteral("chatHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);

    m_chatTitle = new QLabel(QStringLiteral("Nova conversa"), header);
    m_chatTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    headerLayout->addWidget(m_chatTitle, 1);

    auto *modelLabel = new QLabel(QStringLiteral("Modelo:"), header);
    headerLayout->addWidget(modelLabel);
    m_modelCombo = new QComboBox(header);
    m_modelCombo->setMinimumWidth(180);
    headerLayout->addWidget(m_modelCombo);

    auto *effortLabel = new QLabel(QStringLiteral("Esforço:"), header);
    headerLayout->addWidget(effortLabel);
    m_effortCombo = new QComboBox(header);
    headerLayout->addWidget(m_effortCombo);

    layout->addWidget(header);

    m_scroll = new QScrollArea(page);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_messagesHost = new QWidget(m_scroll);
    m_messagesHost->setObjectName(QStringLiteral("messagesHost"));
    m_messagesLayout = new QVBoxLayout(m_messagesHost);
    m_messagesLayout->setContentsMargins(18, 16, 18, 16);
    m_messagesLayout->setSpacing(14);
    m_messagesLayout->addStretch(1);
    m_scroll->setWidget(m_messagesHost);
    layout->addWidget(m_scroll, 1);

    auto *composer = new QWidget(page);
    composer->setObjectName(QStringLiteral("composer"));
    auto *composerLayout = new QVBoxLayout(composer);
    composerLayout->setContentsMargins(16, 8, 16, 12);
    composerLayout->setSpacing(6);

    m_chipBar = new QWidget(composer);
    m_chipLayout = new QHBoxLayout(m_chipBar);
    m_chipLayout->setContentsMargins(0, 0, 0, 0);
    m_chipLayout->setSpacing(6);
    m_chipLayout->addStretch(1);
    m_chipBar->hide();
    composerLayout->addWidget(m_chipBar);

    auto *row = new QHBoxLayout;
    m_attachButton = new QPushButton(QStringLiteral("Anexar"), composer);
    m_attachButton->setToolTip(QStringLiteral("Anexar arquivos ou imagens"));
    row->addWidget(m_attachButton);

    m_commandsButton = new QPushButton(QStringLiteral("/ Comandos"), composer);
    m_commandsButton->setToolTip(QStringLiteral("Inserir comando do projeto"));
    row->addWidget(m_commandsButton);

    m_input = new QPlainTextEdit(composer);
    m_input->setObjectName(QStringLiteral("composerInput"));
    m_input->setPlaceholderText(
        QStringLiteral("Escreva sua mensagem... (Enter envia, Shift+Enter quebra linha)"));
    m_input->setFixedHeight(84);
    m_input->installEventFilter(this);
    row->addWidget(m_input, 1);

    m_sendButton = new QPushButton(QStringLiteral("Enviar"), composer);
    m_sendButton->setMinimumWidth(90);
    m_stopButton = new QPushButton(QStringLiteral("Parar"), composer);
    m_stopButton->setMinimumWidth(90);
    m_stopButton->setVisible(false);
    row->addWidget(m_sendButton);
    row->addWidget(m_stopButton);

    composerLayout->addLayout(row);
    layout->addWidget(composer);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendMessage);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopStreaming);
    connect(m_attachButton, &QPushButton::clicked, this, &MainWindow::attachFiles);
    connect(m_commandsButton, &QPushButton::clicked, this,
            &MainWindow::showCommandsMenu);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &modelId) {
                const ModelInfo *info = modelInfo(modelId);
                updateEffortCombo(modelId);
                if (m_currentConversationId > 0)
                    Database::instance().setConversationModel(m_currentConversationId,
                                                              modelId);
                if (info && !info->supportsImages)
                    m_attachButton->setToolTip(
                        QStringLiteral("Anexar arquivos (este modelo não aceita imagens)"));
            });
    connect(m_effortCombo, &QComboBox::currentTextChanged, this,
            [this](const QString &effort) {
                if (m_currentConversationId > 0)
                    Database::instance().setConversationEffort(m_currentConversationId,
                                                               effort);
            });

    return page;
}

QWidget *MainWindow::buildProjectPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    m_projectTitle = new QLabel(QStringLiteral("Projeto"), page);
    m_projectTitle->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: bold;"));
    headerRow->addWidget(m_projectTitle, 1);

    auto *newChatButton = new QPushButton(QStringLiteral("Nova conversa"), page);
    auto *openFolderButton = new QPushButton(QStringLiteral("Abrir pasta"), page);
    headerRow->addWidget(newChatButton);
    headerRow->addWidget(openFolderButton);
    layout->addLayout(headerRow);

    auto *tabs = new QTabWidget(page);

    auto *instructionsTab = new QWidget(tabs);
    auto *instructionsLayout = new QVBoxLayout(instructionsTab);
    auto *instructionsHint = new QLabel(
        QStringLiteral("Instruções enviadas como system prompt em toda conversa deste projeto."),
        instructionsTab);
    instructionsHint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    instructionsLayout->addWidget(instructionsHint);
    m_projPrompt = new QPlainTextEdit(instructionsTab);
    instructionsLayout->addWidget(m_projPrompt, 1);
    auto *saveInstructions = new QPushButton(QStringLiteral("Salvar instruções"),
                                             instructionsTab);
    instructionsLayout->addWidget(saveInstructions, 0, Qt::AlignRight);
    tabs->addTab(instructionsTab, QStringLiteral("Instruções"));

    auto *filesTab = new QWidget(tabs);
    auto *filesLayout = new QVBoxLayout(filesTab);
    auto *filesToolbar = new QHBoxLayout;
    auto *addFilesButton = new QPushButton(QStringLiteral("Adicionar arquivos"), filesTab);
    auto *addImagesButton = new QPushButton(QStringLiteral("Adicionar imagens"), filesTab);
    auto *attachItemButton = new QPushButton(QStringLiteral("Anexar ao chat"), filesTab);
    auto *removeItemButton = new QPushButton(QStringLiteral("Remover"), filesTab);
    filesToolbar->addWidget(addFilesButton);
    filesToolbar->addWidget(addImagesButton);
    filesToolbar->addWidget(attachItemButton);
    filesToolbar->addWidget(removeItemButton);
    filesToolbar->addStretch(1);
    filesLayout->addLayout(filesToolbar);
    m_projFiles = new QListWidget(filesTab);
    filesLayout->addWidget(m_projFiles, 1);
    tabs->addTab(filesTab, QStringLiteral("Arquivos"));

    auto *commandsTab = new QWidget(tabs);
    auto *commandsLayout = new QVBoxLayout(commandsTab);
    auto *commandsToolbar = new QHBoxLayout;
    auto *addCommandButton = new QPushButton(QStringLiteral("Adicionar"), commandsTab);
    auto *editCommandButton = new QPushButton(QStringLiteral("Editar"), commandsTab);
    auto *useCommandButton = new QPushButton(QStringLiteral("Usar no chat"), commandsTab);
    auto *removeCommandButton = new QPushButton(QStringLiteral("Remover"), commandsTab);
    commandsToolbar->addWidget(addCommandButton);
    commandsToolbar->addWidget(editCommandButton);
    commandsToolbar->addWidget(useCommandButton);
    commandsToolbar->addWidget(removeCommandButton);
    commandsToolbar->addStretch(1);
    commandsLayout->addLayout(commandsToolbar);
    m_projCommands = new QListWidget(commandsTab);
    commandsLayout->addWidget(m_projCommands, 1);
    tabs->addTab(commandsTab, QStringLiteral("Comandos"));

    auto *configTab = new QWidget(tabs);
    auto *configLayout = new QFormLayout(configTab);
    m_projName = new QLineEdit(configTab);
    m_projDescription = new QLineEdit(configTab);
    m_projWorkdir = new QLineEdit(configTab);
    auto *workdirRow = new QHBoxLayout;
    workdirRow->addWidget(m_projWorkdir, 1);
    auto *browseButton = new QPushButton(QStringLiteral("Escolher..."), configTab);
    workdirRow->addWidget(browseButton);
    m_projModel = new QComboBox(configTab);
    m_projEffort = new QComboBox(configTab);
    configLayout->addRow(QStringLiteral("Nome:"), m_projName);
    configLayout->addRow(QStringLiteral("Descrição:"), m_projDescription);
    configLayout->addRow(QStringLiteral("Pasta de trabalho:"), workdirRow);
    configLayout->addRow(QStringLiteral("Modelo padrão:"), m_projModel);
    configLayout->addRow(QStringLiteral("Esforço padrão:"), m_projEffort);
    auto *configButtons = new QHBoxLayout;
    auto *saveProjectButton = new QPushButton(QStringLiteral("Salvar projeto"), configTab);
    auto *deleteProjectButton = new QPushButton(QStringLiteral("Excluir projeto"), configTab);
    configButtons->addWidget(saveProjectButton);
    configButtons->addWidget(deleteProjectButton);
    configButtons->addStretch(1);
    configLayout->addRow(configButtons);
    tabs->addTab(configTab, QStringLiteral("Configurações"));

    layout->addWidget(tabs, 1);

    connect(newChatButton, &QPushButton::clicked, this,
            [this] { newConversation(m_activeProjectId); });
    connect(openFolderButton, &QPushButton::clicked, this, &MainWindow::openWorkdir);
    connect(saveInstructions, &QPushButton::clicked, this, &MainWindow::saveProject);
    connect(addFilesButton, &QPushButton::clicked, this, &MainWindow::addProjectFiles);
    connect(addImagesButton, &QPushButton::clicked, this, &MainWindow::addProjectFiles);
    connect(attachItemButton, &QPushButton::clicked, this,
            &MainWindow::attachProjectItem);
    connect(removeItemButton, &QPushButton::clicked, this,
            &MainWindow::removeProjectFile);
    connect(m_projFiles, &QListWidget::itemDoubleClicked, this, [this] {
        QListWidgetItem *item = m_projFiles->currentItem();
        if (!item)
            return;
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    connect(addCommandButton, &QPushButton::clicked, this, &MainWindow::addCommand);
    connect(editCommandButton, &QPushButton::clicked, this, &MainWindow::editCommand);
    connect(useCommandButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = m_projCommands->currentItem();
        if (!item)
            return;
        m_stack->setCurrentIndex(kChatPage);
        m_input->setPlainText(item->data(Qt::UserRole + 1).toString());
        m_input->setFocus();
    });
    connect(removeCommandButton, &QPushButton::clicked, this,
            &MainWindow::removeCommand);
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Escolha a pasta de trabalho"), m_projWorkdir->text());
        if (!dir.isEmpty())
            m_projWorkdir->setText(dir);
    });
    connect(saveProjectButton, &QPushButton::clicked, this, &MainWindow::saveProject);
    connect(deleteProjectButton, &QPushButton::clicked, this, &MainWindow::deleteProject);

    return page;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool isEnter = keyEvent->key() == Qt::Key_Return
            || keyEvent->key() == Qt::Key_Enter;
        if (!isEnter)
            return QMainWindow::eventFilter(watched, event);

        const bool shift = keyEvent->modifiers() & Qt::ShiftModifier;
        const bool control = keyEvent->modifiers() & Qt::ControlModifier;
        const bool enterSends = Config::instance().ui().enterToSend;

        if ((enterSends && !shift) || (!enterSends && control)) {
            sendMessage();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::reloadProjects()
{
    m_projects = Database::instance().projects(m_user.id);
}

void MainWindow::reloadTree(qint64 selectProjectId, qint64 selectConversationId)
{
    reloadProjects();
    m_tree->clear();

    const QVector<Conversation> conversations =
        Database::instance().conversations(m_user.id);

    QTreeWidgetItem *selectedItem = nullptr;

    for (const Project &project : m_projects) {
        auto *projectItem = new QTreeWidgetItem(m_tree);
        projectItem->setText(0, project.name);
        projectItem->setData(0, kTypeRole, QStringLiteral("project"));
        projectItem->setData(0, kIdRole, project.id);
        projectItem->setToolTip(0, project.workdir.isEmpty()
                                       ? QStringLiteral("Sem pasta definida")
                                       : project.workdir);
        projectItem->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));

        for (const Conversation &c : conversations) {
            if (c.projectId != project.id)
                continue;
            auto *convItem = new QTreeWidgetItem(projectItem);
            convItem->setText(0, c.title);
            convItem->setData(0, kTypeRole, QStringLiteral("conv"));
            convItem->setData(0, kIdRole, c.id);
            convItem->setToolTip(0, QStringLiteral("%1\nModelo: %2")
                                        .arg(c.updatedAt.toString(
                                                 QStringLiteral("dd/MM/yyyy HH:mm")),
                                             c.model));
            if (c.id == selectConversationId)
                selectedItem = convItem;
        }
        projectItem->setExpanded(true);
        if (project.id == selectProjectId && !selectedItem)
            selectedItem = projectItem;
    }

    auto *header = new QTreeWidgetItem(m_tree);
    header->setText(0, QStringLiteral("Conversas"));
    header->setData(0, kTypeRole, QStringLiteral("header"));
    header->setFlags(Qt::ItemIsEnabled);
    QFont headerFont = header->font(0);
    headerFont.setBold(true);
    header->setFont(0, headerFont);

    const QDate today = QDate::currentDate();
    struct Group {
        QString name;
        QTreeWidgetItem *item = nullptr;
    };
    QVector<Group> groups = {
        {QStringLiteral("Hoje"), nullptr},
        {QStringLiteral("Últimos 7 dias"), nullptr},
        {QStringLiteral("Mais antigos"), nullptr}
    };

    for (const Conversation &c : conversations) {
        if (c.projectId != 0)
            continue;
        int groupIndex = 2;
        const QDate date = c.updatedAt.date();
        if (date == today)
            groupIndex = 0;
        else if (date.daysTo(today) <= 7)
            groupIndex = 1;

        if (!groups.at(groupIndex).item) {
            auto *groupItem = new QTreeWidgetItem(m_tree);
            groupItem->setText(0, groups.at(groupIndex).name);
            groupItem->setData(0, kTypeRole, QStringLiteral("header"));
            groupItem->setFlags(Qt::ItemIsEnabled);
            QFont font = groupItem->font(0);
            font.setBold(true);
            groupItem->setFont(0, font);
            groups[groupIndex].item = groupItem;
        }

        auto *convItem = new QTreeWidgetItem(groups.at(groupIndex).item);
        convItem->setText(0, c.title);
        convItem->setData(0, kTypeRole, QStringLiteral("conv"));
        convItem->setData(0, kIdRole, c.id);
        if (c.id == selectConversationId)
            selectedItem = convItem;
    }

    if (selectedItem)
        m_tree->setCurrentItem(selectedItem);

    if (!m_search->text().isEmpty())
        filterTree(m_search->text());
}

void MainWindow::filterTree(const QString &term)
{
    std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *item) -> bool {
        bool childVisible = false;
        for (int i = 0; i < item->childCount(); ++i)
            childVisible |= apply(item->child(i));

        const bool selfMatch = term.isEmpty()
            || item->text(0).contains(term, Qt::CaseInsensitive);
        const bool isHeader = item->data(0, kTypeRole).toString()
                              == QLatin1String("header");
        const bool visible = childVisible || (selfMatch && (!isHeader || childVisible));
        item->setHidden(!visible);
        return visible;
    };

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        apply(m_tree->topLevelItem(i));
}

void MainWindow::onTreeSelection()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return;

    const QString type = item->data(0, kTypeRole).toString();
    const qint64 id = item->data(0, kIdRole).toLongLong();
    if (type == QLatin1String("project"))
        showProject(id);
    else if (type == QLatin1String("conv"))
        openConversation(id);
}

void MainWindow::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);
    if (!item)
        return;
    m_tree->setCurrentItem(item);

    const QString type = item->data(0, kTypeRole).toString();
    QMenu menu;
    if (type == QLatin1String("conv")) {
        QAction *renameAction = menu.addAction(QStringLiteral("Renomear"));
        QAction *deleteAction = menu.addAction(QStringLiteral("Excluir"));
        QAction *chosen = menu.exec(m_tree->mapToGlobal(pos));
        if (chosen == renameAction)
            renameSelectedConversation();
        else if (chosen == deleteAction)
            deleteSelectedConversation();
    } else if (type == QLatin1String("project")) {
        QAction *newChatAction = menu.addAction(QStringLiteral("Nova conversa"));
        QAction *openAction = menu.addAction(QStringLiteral("Abrir"));
        QAction *chosen = menu.exec(m_tree->mapToGlobal(pos));
        if (chosen == newChatAction)
            newConversation(item->data(0, kIdRole).toLongLong());
        else if (chosen == openAction)
            showProject(item->data(0, kIdRole).toLongLong());
    }
}

void MainWindow::newConversation(qint64 projectId)
{
    m_currentConversationId = 0;
    m_activeProjectId = projectId;
    clearMessages();
    clearAttachments();

    m_chatTitle->setText(QStringLiteral("Nova conversa"));

    const QSignalBlocker modelBlocker(m_modelCombo);
    const QSignalBlocker effortBlocker(m_effortCombo);

    const Project *project = nullptr;
    for (const Project &p : m_projects) {
        if (p.id == projectId) {
            project = &p;
            break;
        }
    }

    if (project && !project->defaultModel.isEmpty()
        && m_modelCombo->findText(project->defaultModel) >= 0) {
        m_modelCombo->setCurrentText(project->defaultModel);
        updateEffortCombo(project->defaultModel);
    }
    if (project && !project->defaultEffort.isEmpty())
        m_effortCombo->setCurrentText(project->defaultEffort);

    m_stack->setCurrentIndex(kChatPage);
    m_input->setFocus();
}

void MainWindow::newProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Novo projeto"),
                                               QStringLiteral("Nome do projeto:"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Escolha a pasta de trabalho (opcional)"));

    Project project;
    project.userId = m_user.id;
    project.name = name.trimmed();
    project.workdir = dir;
    project.defaultModel = m_modelCombo->count() > 0 ? m_modelCombo->currentText()
                                                     : QStringLiteral("deepseek-flash");
    project.defaultEffort = m_effortCombo->count() > 0 ? m_effortCombo->currentText()
                                                       : QStringLiteral("high");

    QString error;
    const qint64 id = Database::instance().createProject(project, &error);
    if (id <= 0) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }

    logActivity(QStringLiteral("project_create"), project.name);
    showProject(id);
    reloadTree(id, 0);
}

void MainWindow::openConversation(qint64 id)
{
    m_currentConversationId = id;
    m_history.clear();
    clearMessages();
    clearAttachments();

    qint64 projectId = 0;
    QString model;
    QString effort;
    QString title;
    const QVector<Conversation> conversations =
        Database::instance().conversations(m_user.id);
    for (const Conversation &c : conversations) {
        if (c.id == id) {
            projectId = c.projectId;
            model = c.model;
            effort = c.effort;
            title = c.title;
            break;
        }
    }

    m_activeProjectId = projectId;
    m_chatTitle->setText(title);

    {
        const QSignalBlocker modelBlocker(m_modelCombo);
        const QSignalBlocker effortBlocker(m_effortCombo);
        if (!model.isEmpty() && m_modelCombo->findText(model) >= 0)
            m_modelCombo->setCurrentText(model);
        updateEffortCombo(m_modelCombo->currentText());
        if (!effort.isEmpty())
            m_effortCombo->setCurrentText(effort);
    }

    QString error;
    const QVector<Message> stored = Database::instance().messages(id, &error);
    renderMessages(stored);
    m_history = stored;

    m_stack->setCurrentIndex(kChatPage);
    scrollToBottom();
    m_input->setFocus();
}

void MainWindow::showProject(qint64 projectId)
{
    m_activeProjectId = projectId;
    m_currentConversationId = 0;
    loadProjectIntoTab(projectId);
    m_stack->setCurrentIndex(kProjectPage);
}

void MainWindow::loadProjectIntoTab(qint64 projectId)
{
    for (const Project &p : m_projects) {
        if (p.id != projectId)
            continue;
        m_projectTitle->setText(p.name);
        m_projName->setText(p.name);
        m_projDescription->setText(p.description);
        m_projWorkdir->setText(p.workdir);
        m_projPrompt->setPlainText(p.systemPrompt);
        if (!p.defaultModel.isEmpty() && m_projModel->count() > 0
            && m_projModel->findText(p.defaultModel) >= 0)
            m_projModel->setCurrentText(p.defaultModel);
        if (!p.defaultEffort.isEmpty() && m_projEffort->count() > 0
            && m_projEffort->findText(p.defaultEffort) >= 0)
            m_projEffort->setCurrentText(p.defaultEffort);
        break;
    }

    m_projFiles->clear();
    const QVector<ProjectItem> items = Database::instance().projectItems(projectId);
    for (const ProjectItem &item : items) {
        auto *listItem = new QListWidgetItem(
            QStringLiteral("%1  (%2)").arg(item.name).arg(item.kind), m_projFiles);
        listItem->setData(Qt::UserRole, item.path);
        listItem->setData(Qt::UserRole + 1, item.id);
        listItem->setToolTip(item.path);
    }

    m_projCommands->clear();
    const QVector<Command> commands = Database::instance().commands(projectId);
    for (const Command &command : commands) {
        auto *listItem = new QListWidgetItem(
            QStringLiteral("/%1").arg(command.name), m_projCommands);
        listItem->setData(Qt::UserRole, command.id);
        listItem->setData(Qt::UserRole + 1, command.prompt);
        listItem->setToolTip(command.prompt.left(300));
    }
}

void MainWindow::saveProject()
{
    if (m_activeProjectId == 0)
        return;

    Project *project = activeProject();
    if (!project)
        return;

    project->name = m_projName->text().trimmed();
    if (project->name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Projeto"),
                             QStringLiteral("Informe um nome para o projeto."));
        return;
    }
    project->description = m_projDescription->text().trimmed();
    project->workdir = m_projWorkdir->text().trimmed();
    project->systemPrompt = m_projPrompt->toPlainText();
    project->defaultModel = m_projModel->currentText();
    project->defaultEffort = m_projEffort->currentText();

    QString error;
    if (!Database::instance().updateProject(*project, &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }

    m_projectTitle->setText(project->name);
    logActivity(QStringLiteral("project_update"), project->name);
    reloadTree(project->id, 0);
    statusBar()->showMessage(QStringLiteral("Projeto salvo."), 4000);
}

void MainWindow::deleteProject()
{
    if (m_activeProjectId == 0)
        return;

    Project *project = activeProject();
    if (!project)
        return;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Excluir projeto"),
        QStringLiteral("Excluir o projeto \"%1\" com conversas, arquivos e comandos?")
            .arg(project->name));
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!Database::instance().deleteProject(project->id, &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }

    logActivity(QStringLiteral("project_delete"), project->name);
    m_activeProjectId = 0;
    m_currentConversationId = 0;
    clearMessages();
    reloadTree();
    newConversation(0);
}

void MainWindow::renameSelectedConversation()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || item->data(0, kTypeRole).toString() != QLatin1String("conv"))
        return;

    const qint64 id = item->data(0, kIdRole).toLongLong();
    bool ok = false;
    const QString title = QInputDialog::getText(
        this, QStringLiteral("Renomear conversa"), QStringLiteral("Novo título:"),
        QLineEdit::Normal, item->text(0), &ok);
    if (!ok || title.trimmed().isEmpty())
        return;

    QString error;
    if (!Database::instance().renameConversation(id, title.trimmed(), &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    item->setText(0, title.trimmed());
    if (id == m_currentConversationId)
        m_chatTitle->setText(title.trimmed());
}

void MainWindow::deleteSelectedConversation()
{
    QTreeWidgetItem *item = m_tree->currentItem();
    if (!item || item->data(0, kTypeRole).toString() != QLatin1String("conv"))
        return;

    const qint64 id = item->data(0, kIdRole).toLongLong();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Excluir conversa"),
        QStringLiteral("Excluir \"%1\" e todas as mensagens?").arg(item->text(0)));
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!Database::instance().deleteConversation(id, &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }

    logActivity(QStringLiteral("delete_conversation"), item->text(0));

    if (id == m_currentConversationId)
        newConversation(m_activeProjectId);
    reloadTree(m_activeProjectId, 0);
}

void MainWindow::refreshModels()
{
    m_client->fetchModels();
}

void MainWindow::onModelsFetched(const QVector<ModelInfo> &models)
{
    if (models.isEmpty()) {
        onModelsFailed(QStringLiteral("Lista vazia"));
        return;
    }

    m_models = models;

    const QString previousModel = m_modelCombo->count() > 0 ? m_modelCombo->currentText()
                                                            : QString();
    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();
    m_projModel->clear();
    for (const ModelInfo &info : models) {
        m_modelCombo->addItem(info.id);
        m_projModel->addItem(info.id);
    }

    if (!previousModel.isEmpty() && m_modelCombo->findText(previousModel) >= 0)
        m_modelCombo->setCurrentText(previousModel);
    else if (m_modelCombo->findText(Config::instance().deepseek().defaultModel) >= 0)
        m_modelCombo->setCurrentText(Config::instance().deepseek().defaultModel);

    updateEffortCombo(m_modelCombo->currentText());
    if (m_activeProjectId > 0)
        loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::onModelsFailed(const QString &error)
{
    m_models.clear();
    ModelInfo flash;
    flash.id = QStringLiteral("deepseek-flash");
    flash.name = QStringLiteral("DeepSeek-V4.1-Flash");
    flash.supportsImages = true;
    flash.efforts = {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
    flash.defaultEffort = QStringLiteral("high");
    ModelInfo pro;
    pro.id = QStringLiteral("deepseek-v4-pro");
    pro.name = QStringLiteral("DeepSeek-V4-Pro");
    pro.efforts = {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
    pro.defaultEffort = QStringLiteral("high");
    m_models = {flash, pro};

    const QSignalBlocker blocker(m_modelCombo);
    m_modelCombo->clear();
    m_projModel->clear();
    for (const ModelInfo &info : m_models) {
        m_modelCombo->addItem(info.id);
        m_projModel->addItem(info.id);
    }
    if (m_modelCombo->findText(Config::instance().deepseek().defaultModel) >= 0)
        m_modelCombo->setCurrentText(Config::instance().deepseek().defaultModel);
    updateEffortCombo(m_modelCombo->currentText());

    statusBar()->showMessage(
        QStringLiteral("Não foi possível listar os modelos (%1). Usando lista padrão.")
            .arg(error),
        6000);
}

void MainWindow::updateEffortCombo(const QString &modelId)
{
    const QSignalBlocker blocker(m_effortCombo);
    m_effortCombo->clear();
    const ModelInfo *info = modelInfo(modelId);
    QStringList efforts = info ? info->efforts : QStringList();
    if (efforts.isEmpty())
        efforts = {QStringLiteral("low"), QStringLiteral("high"), QStringLiteral("max")};
    m_effortCombo->addItems(efforts);

    QString desired = info ? info->defaultEffort : QString();
    if (m_currentConversationId > 0) {
        const QString stored = Database::instance().conversationEffort(
            m_currentConversationId);
        if (!stored.isEmpty() && m_effortCombo->findText(stored) >= 0)
            desired = stored;
    }
    if (!desired.isEmpty() && m_effortCombo->findText(desired) >= 0)
        m_effortCombo->setCurrentText(desired);
}

const ModelInfo *MainWindow::modelInfo(const QString &id) const
{
    for (const ModelInfo &info : m_models) {
        if (info.id == id)
            return &info;
    }
    return nullptr;
}

void MainWindow::sendMessage()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || !m_client || m_client->isStreaming())
        return;

    if (m_currentConversationId == 0) {
        QString title = text.simplified();
        if (title.size() > 60)
            title = title.left(57) + QStringLiteral("...");

        QString error;
        const qint64 id = Database::instance().createConversation(
            m_user.id, m_activeProjectId, title, currentModel(), currentEffort(), &error);
        if (id <= 0) {
            QMessageBox::critical(this, QStringLiteral("Erro ao criar conversa"), error);
            return;
        }
        m_currentConversationId = id;
        m_chatTitle->setText(title);
    }

    QString error;
    Database::instance().addMessage(m_currentConversationId, QStringLiteral("user"),
                                    text, QString(), currentModel(), m_pendingAttachments,
                                    &error);

    Message userMessage;
    userMessage.role = QStringLiteral("user");
    userMessage.content = text;
    userMessage.attachments = m_pendingAttachments;
    m_history.append(userMessage);

    MessageBubble *bubble = addBubble(MessageBubble::UserRole, text);
    if (!m_pendingAttachments.isEmpty())
        bubble->setAttachments(m_pendingAttachments);

    const int attachmentCount = m_pendingAttachments.size();
    m_input->clear();
    clearAttachments();

    m_streamingBubble = addBubble(MessageBubble::AssistantRole, QString());
    scrollToBottom();
    setStreaming(true);

    logActivity(QStringLiteral("message"),
                QStringLiteral("modelo=%1, esforço=%2, anexos=%3")
                    .arg(currentModel(), currentEffort())
                    .arg(attachmentCount));

    QVector<Message> apiMessages;
    const Project *project = nullptr;
    for (const Project &p : m_projects) {
        if (p.id == m_activeProjectId) {
            project = &p;
            break;
        }
    }
    if (project && !project->systemPrompt.trimmed().isEmpty()) {
        Message systemMessage;
        systemMessage.role = QStringLiteral("system");
        systemMessage.content = project->systemPrompt.trimmed();
        apiMessages.append(systemMessage);
    }
    apiMessages += m_history;

    const ModelInfo *info = modelInfo(currentModel());
    const bool allowImages = info ? info->supportsImages : true;
    m_client->send(currentModel(), currentEffort(), allowImages, apiMessages);
}

void MainWindow::stopStreaming()
{
    if (m_client)
        m_client->cancel();
}

void MainWindow::setStreaming(bool streaming)
{
    m_sendButton->setEnabled(!streaming);
    m_stopButton->setVisible(streaming);
    m_input->setEnabled(!streaming);
    m_attachButton->setEnabled(!streaming);
    if (!streaming)
        m_input->setFocus();
}

void MainWindow::onChunk(const QString &text)
{
    if (m_streamingBubble) {
        m_streamingBubble->appendText(text);
        scrollToBottom();
    }
}

void MainWindow::onReasoning(const QString &text)
{
    if (m_streamingBubble) {
        m_streamingBubble->appendReasoning(text);
        scrollToBottom();
    }
}

void MainWindow::onFinished(const QString &fullText, const QString &fullReasoning)
{
    const QString model = currentModel();

    if (m_streamingBubble)
        m_streamingBubble->setText(fullText);

    if (!fullText.isEmpty() && m_currentConversationId > 0) {
        QString error;
        Database::instance().addMessage(m_currentConversationId,
                                        QStringLiteral("assistant"), fullText,
                                        fullReasoning, model, QVector<Attachment>(),
                                        &error);

        Message assistantMessage;
        assistantMessage.role = QStringLiteral("assistant");
        assistantMessage.content = fullText;
        assistantMessage.reasoning = fullReasoning;
        m_history.append(assistantMessage);

        Database::instance().touchConversation(m_currentConversationId);

        logActivity(QStringLiteral("response"),
                    QStringLiteral("modelo=%1, %2 caracteres")
                        .arg(model).arg(fullText.size()));
    }

    m_streamingBubble = nullptr;
    setStreaming(false);
    reloadTree(m_activeProjectId, m_currentConversationId);
}

void MainWindow::onFailed(const QString &error)
{
    if (m_streamingBubble)
        m_streamingBubble->showError(error);

    m_streamingBubble = nullptr;
    setStreaming(false);
    logActivity(QStringLiteral("error"), error);
}

void MainWindow::attachFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Anexar arquivos ou imagens"));
    for (const QString &path : files) {
        Attachment attachment;
        QString error;
        if (!makeAttachment(path, &attachment, &error)) {
            QMessageBox::warning(this, QStringLiteral("Anexo"), error);
            continue;
        }
        addAttachment(attachment);
    }
}

void MainWindow::addAttachment(const Attachment &attachment)
{
    m_pendingAttachments.append(attachment);
    rebuildAttachmentChips();
}

void MainWindow::rebuildAttachmentChips()
{
    while (m_chipLayout->count() > 1) {
        QLayoutItem *item = m_chipLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_pendingAttachments.size(); ++i) {
        const Attachment &a = m_pendingAttachments.at(i);
        auto *chip = new QWidget(m_chipBar);
        chip->setStyleSheet(QStringLiteral(
            "background: #eef2ff; border: 1px solid #c7d2fe; border-radius: 10px;"));
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(8, 2, 4, 2);
        auto *label = new QLabel(a.isImage() ? QStringLiteral("img: ") + a.name : a.name,
                                 chip);
        chipLayout->addWidget(label);
        auto *remove = new QToolButton(chip);
        remove->setText(QStringLiteral("x"));
        remove->setAutoRaise(true);
        chipLayout->addWidget(remove);
        connect(remove, &QToolButton::clicked, this, [this, i] {
            m_pendingAttachments.remove(i);
            rebuildAttachmentChips();
        });
        m_chipLayout->insertWidget(m_chipLayout->count() - 1, chip);
    }

    m_chipBar->setVisible(!m_pendingAttachments.isEmpty());
}

void MainWindow::clearAttachments()
{
    m_pendingAttachments.clear();
    rebuildAttachmentChips();
}

void MainWindow::showCommandsMenu()
{
    QVector<Command> commands = Database::instance().commands(0);
    if (m_activeProjectId > 0)
        commands += Database::instance().commands(m_activeProjectId);

    if (commands.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Comandos"),
                                 QStringLiteral("Nenhum comando cadastrado. "
                                                "Adicione em Configurações."));
        return;
    }

    QMenu menu(this);
    for (const Command &command : commands) {
        QAction *action = menu.addAction(QStringLiteral("/") + command.name);
        connect(action, &QAction::triggered, this, [this, command] {
            m_input->setPlainText(command.prompt);
            m_input->setFocus();
        });
    }
    menu.exec(m_commandsButton->mapToGlobal(QPoint(0, m_commandsButton->height())));
}

void MainWindow::addCommand()
{
    if (m_activeProjectId == 0)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Novo comando"),
                                               QStringLiteral("Nome (sem /):"),
                                               QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString prompt = QInputDialog::getMultiLineText(
        this, QStringLiteral("Novo comando"), QStringLiteral("Prompt:"), QString(), &ok);
    if (!ok || prompt.trimmed().isEmpty())
        return;

    QString error;
    if (Database::instance().addCommand(m_activeProjectId, name.trimmed(), prompt,
                                        &error) <= 0) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::editCommand()
{
    QListWidgetItem *item = m_projCommands->currentItem();
    if (!item)
        return;

    const qint64 id = item->data(Qt::UserRole).toLongLong();
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Editar comando"),
                                               QStringLiteral("Nome (sem /):"),
                                               QLineEdit::Normal,
                                               item->text().mid(1), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString prompt = QInputDialog::getMultiLineText(
        this, QStringLiteral("Editar comando"), QStringLiteral("Prompt:"),
        item->data(Qt::UserRole + 1).toString(), &ok);
    if (!ok || prompt.trimmed().isEmpty())
        return;

    QString error;
    if (!Database::instance().updateCommand(id, name.trimmed(), prompt, &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::removeCommand()
{
    QListWidgetItem *item = m_projCommands->currentItem();
    if (!item)
        return;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Remover comando"),
        QStringLiteral("Remover o comando %1?").arg(item->text()));
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!Database::instance().removeCommand(item->data(Qt::UserRole).toLongLong(),
                                            &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::addProjectFiles()
{
    if (m_activeProjectId == 0)
        return;

    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Adicionar arquivos ao projeto"));
    for (const QString &path : files) {
        const QFileInfo info(path);
        ProjectItem item;
        item.projectId = m_activeProjectId;
        item.kind = isImageSuffix(info.suffix()) ? QStringLiteral("image")
                                                 : QStringLiteral("file");
        item.name = info.fileName();
        item.path = info.absoluteFilePath();
        item.mime = isImageSuffix(info.suffix()) ? mimeForSuffix(info.suffix())
                                                 : QStringLiteral("text/plain");
        item.size = info.size();
        QString error;
        if (Database::instance().addProjectItem(item, &error) <= 0)
            QMessageBox::warning(this, QStringLiteral("Arquivos"), error);
    }
    loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::removeProjectFile()
{
    QListWidgetItem *item = m_projFiles->currentItem();
    if (!item)
        return;

    QString error;
    if (!Database::instance().removeProjectItem(item->data(Qt::UserRole + 1).toLongLong(),
                                                &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    loadProjectIntoTab(m_activeProjectId);
}

void MainWindow::openWorkdir()
{
    Project *project = activeProject();
    if (!project || project->workdir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Pasta"),
                                 QStringLiteral("Nenhuma pasta de trabalho definida."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(project->workdir));
}

void MainWindow::attachProjectItem()
{
    QListWidgetItem *item = m_projFiles->currentItem();
    if (!item)
        return;

    const QString path = item->data(Qt::UserRole).toString();
    Attachment attachment;
    QString error;
    if (!makeAttachment(path, &attachment, &error)) {
        QMessageBox::warning(this, QStringLiteral("Anexo"), error);
        return;
    }
    addAttachment(attachment);
    m_stack->setCurrentIndex(kChatPage);
    m_input->setFocus();
}

void MainWindow::applyFileFromModel(const QString &path, const QString &content)
{
    Project *project = activeProject();
    if (!project || project->workdir.isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("Salvar arquivo"),
            QStringLiteral("Este chat não está em um projeto com pasta de trabalho "
                           "definida."));
        return;
    }

    const QDir base(project->workdir);
    const QString target = QDir::cleanPath(base.absoluteFilePath(path));
    if (!target.startsWith(base.absolutePath() + QLatin1Char('/'))) {
        QMessageBox::warning(this, QStringLiteral("Salvar arquivo"),
                             QStringLiteral("Caminho fora da pasta do projeto: %1")
                                 .arg(path));
        return;
    }

    const QFileInfo info(target);
    QString message = QStringLiteral("Salvar o arquivo em:\n%1").arg(target);
    if (info.exists())
        message += QStringLiteral("\n\nO arquivo já existe e será sobrescrito.");
    const auto answer = QMessageBox::question(this, QStringLiteral("Confirmar"),
                                              message);
    if (answer != QMessageBox::Yes)
        return;

    QDir().mkpath(info.absolutePath());
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, QStringLiteral("Erro"),
                              QStringLiteral("Não foi possível gravar %1").arg(target));
        return;
    }
    file.write(content.toUtf8());
    file.close();

    logActivity(QStringLiteral("file_write"), target);
    statusBar()->showMessage(QStringLiteral("Arquivo salvo: %1").arg(target), 5000);
}

void MainWindow::showActivities()
{
    QString error;
    const QVector<Activity> activities =
        Database::instance().activities(m_user.id, 300, &error);

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Registro de atividades"));
    dialog.resize(760, 480);
    auto *layout = new QVBoxLayout(&dialog);

    auto *table = new QTableWidget(activities.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({QStringLiteral("Data"), QStringLiteral("Ação"),
                                      QStringLiteral("Detalhes")});
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->setVisible(false);

    for (int row = 0; row < activities.size(); ++row) {
        const Activity &a = activities.at(row);
        table->setItem(row, 0, new QTableWidgetItem(
            a.createdAt.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))));
        table->setItem(row, 1, new QTableWidgetItem(a.action));
        table->setItem(row, 2, new QTableWidgetItem(a.details));
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(m_user, this);
    dialog.setModels(m_models);
    connect(&dialog, &SettingsDialog::settingsChanged, this, &MainWindow::applySettings);
    dialog.exec();
}

void MainWindow::applySettings()
{
    Theme::apply(*qApp, Config::instance().ui().theme);

    User refreshed;
    if (Database::instance().findUserByUsername(m_user.username, &refreshed,
                                                nullptr, nullptr)) {
        m_user = refreshed;
    }

    const QString name = m_user.displayName.isEmpty() ? m_user.username
                                                       : m_user.displayName;
    m_userLabel->setText(QStringLiteral("Conectado como %1").arg(name));
    setWindowTitle(QStringLiteral("DeepSeek Chat — %1").arg(name));

    if (m_currentConversationId > 0)
        openConversation(m_currentConversationId);

    statusBar()->showMessage(QStringLiteral("Configurações aplicadas."), 4000);
}

void MainWindow::doLogout()
{
    logActivity(QStringLiteral("logout"), QStringLiteral("Sessão encerrada"));
    emit logoutRequested();
    close();
}

MessageBubble *MainWindow::addBubble(MessageBubble::Role role, const QString &text)
{
    auto *bubble = new MessageBubble(role, text, m_messagesHost);
    bubble->setMaximumWidth(role == MessageBubble::UserRole ? 720 : 860);
    const Qt::Alignment alignment = role == MessageBubble::UserRole
        ? Qt::AlignRight : Qt::AlignLeft;
    m_messagesLayout->insertWidget(m_messagesLayout->count() - 1, bubble, 0, alignment);
    connect(bubble, &MessageBubble::fileApplyRequested, this,
            &MainWindow::applyFileFromModel);
    return bubble;
}

void MainWindow::clearMessages()
{
    while (m_messagesLayout->count() > 1) {
        QLayoutItem *item = m_messagesLayout->takeAt(0);
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_streamingBubble = nullptr;
}

void MainWindow::renderMessages(const QVector<Message> &messages)
{
    for (const Message &m : messages) {
        MessageBubble::Role role = MessageBubble::AssistantRole;
        if (m.role == QLatin1String("user"))
            role = MessageBubble::UserRole;
        else if (m.role == QLatin1String("system"))
            role = MessageBubble::SystemRole;

        MessageBubble *bubble = addBubble(role, m.content);
        if (!m.reasoning.isEmpty())
            bubble->setReasoning(m.reasoning);
        if (!m.attachments.isEmpty())
            bubble->setAttachments(m.attachments);
    }
}

void MainWindow::scrollToBottom()
{
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_scroll->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

QString MainWindow::currentModel() const
{
    return m_modelCombo->currentText();
}

QString MainWindow::currentEffort() const
{
    return m_effortCombo->currentText();
}

Project *MainWindow::activeProject()
{
    for (Project &p : m_projects) {
        if (p.id == m_activeProjectId)
            return &p;
    }
    return nullptr;
}

void MainWindow::logActivity(const QString &action, const QString &details)
{
    Database::instance().logActivity(m_user.id, action, details);
}
