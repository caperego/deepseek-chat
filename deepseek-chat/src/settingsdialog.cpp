#include "settingsdialog.h"
#include "assets.h"
#include "auth.h"
#include "config.h"
#include "database.h"
#include "theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QTabWidget>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char *kAppVersion = "1.0.0";

QWidget *passwordRow(QLineEdit *edit, QToolButton *button, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(edit, 1);
    button->setText(QStringLiteral("ver"));
    button->setCheckable(true);
    button->setAutoRaise(true);
    layout->addWidget(button);
    return container;
}

}

SettingsDialog::SettingsDialog(const User &user, QWidget *parent)
    : QDialog(parent),
      m_user(user)
{
    setWindowTitle(QStringLiteral("Configurações"));
    setWindowIcon(Assets::appIcon());
    resize(720, 560);

    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildAppearanceTab(), QStringLiteral("Aparência"));
    tabs->addTab(buildProfileTab(), QStringLiteral("Perfil"));
    tabs->addTab(buildApiTab(), QStringLiteral("API DeepSeek"));
    tabs->addTab(buildDatabaseTab(), QStringLiteral("Banco de dados"));
    tabs->addTab(buildCommandsTab(), QStringLiteral("Comandos"));
    tabs->addTab(buildAboutTab(), QStringLiteral("Sobre"));
    layout->addWidget(tabs, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save
                                             | QDialogButtonBox::Close,
                                         this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("Salvar"));
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("Fechar"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::saveAll);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);

    m_themeCombo = new QComboBox(tab);
    m_themeCombo->addItem(QStringLiteral("Claro (estilo ChatGPT)"),
                          QStringLiteral("light"));
    m_themeCombo->addItem(QStringLiteral("Escuro"), QStringLiteral("dark"));
    const int themeIndex = m_themeCombo->findData(
        Theme::normalize(Config::instance().ui().theme));
    if (themeIndex >= 0)
        m_themeCombo->setCurrentIndex(themeIndex);
    layout->addRow(QStringLiteral("Tema:"), m_themeCombo);

    m_fontSpin = new QSpinBox(tab);
    m_fontSpin->setRange(9, 22);
    m_fontSpin->setValue(Config::instance().ui().fontSize);
    m_fontSpin->setSuffix(QStringLiteral(" pt"));
    layout->addRow(QStringLiteral("Tamanho da fonte:"), m_fontSpin);

    m_enterToSend = new QCheckBox(QStringLiteral("Enter envia a mensagem"), tab);
    m_enterToSend->setChecked(Config::instance().ui().enterToSend);
    layout->addRow(m_enterToSend);

    m_showReasoning = new QCheckBox(QStringLiteral("Mostrar raciocínio dos modelos"),
                                    tab);
    m_showReasoning->setChecked(Config::instance().ui().showReasoning);
    layout->addRow(m_showReasoning);

    auto *hint = new QLabel(
        QStringLiteral("O tema é aplicado ao salvar. As mudanças valem para todas "
                       "as janelas."),
        tab);
    hint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addRow(hint);

    return tab;
}

QWidget *SettingsDialog::buildProfileTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    auto *nameForm = new QFormLayout;
    m_displayName = new QLineEdit(m_user.displayName, tab);
    nameForm->addRow(QStringLiteral("Nome exibido:"), m_displayName);
    layout->addLayout(nameForm);

    auto *saveName = new QPushButton(QStringLiteral("Salvar nome"), tab);
    saveName->setMaximumWidth(160);
    layout->addWidget(saveName);

    auto *separator = new QLabel(QStringLiteral("<hr>"), tab);
    layout->addWidget(separator);

    auto *passForm = new QFormLayout;
    m_oldPassword = new QLineEdit(tab);
    m_oldPassword->setEchoMode(QLineEdit::Password);
    m_newPassword = new QLineEdit(tab);
    m_newPassword->setEchoMode(QLineEdit::Password);
    m_confirmPassword = new QLineEdit(tab);
    m_confirmPassword->setEchoMode(QLineEdit::Password);
    passForm->addRow(QStringLiteral("Senha atual:"), m_oldPassword);
    passForm->addRow(QStringLiteral("Nova senha:"), m_newPassword);
    passForm->addRow(QStringLiteral("Confirmar:"), m_confirmPassword);
    layout->addLayout(passForm);

    auto *changePass = new QPushButton(QStringLiteral("Alterar senha"), tab);
    changePass->setMaximumWidth(160);
    layout->addWidget(changePass);
    layout->addStretch(1);

    connect(saveName, &QPushButton::clicked, this, &SettingsDialog::saveProfile);
    connect(changePass, &QPushButton::clicked, this, &SettingsDialog::changePassword);

    return tab;
}

QWidget *SettingsDialog::buildApiTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);

    m_apiKey = new QLineEdit(Config::instance().deepseek().apiKey, tab);
    m_apiKey->setEchoMode(QLineEdit::Password);
    auto *showKey = new QToolButton(tab);
    connect(showKey, &QToolButton::toggled, this,
            [this, showKey] { toggleEcho(m_apiKey, showKey); });
    layout->addRow(QStringLiteral("API key:"), passwordRow(m_apiKey, showKey, tab));

    m_baseUrl = new QLineEdit(Config::instance().deepseek().baseUrl, tab);
    layout->addRow(QStringLiteral("Base URL:"), m_baseUrl);

    m_defaultModel = new QComboBox(tab);
    setModels(m_models);
    layout->addRow(QStringLiteral("Modelo padrão:"), m_defaultModel);

    auto *link = new QLabel(
        QStringLiteral("<a href=\"https://platform.deepseek.com\">platform.deepseek.com"
                       "</a>"),
        tab);
    link->setOpenExternalLinks(true);
    layout->addRow(QStringLiteral("Obter chave:"), link);

    auto *testButton = new QPushButton(QStringLiteral("Testar conexão"), tab);
    testButton->setMaximumWidth(180);
    layout->addRow(testButton);

    m_apiStatus = new QLabel(tab);
    m_apiStatus->setWordWrap(true);
    layout->addRow(m_apiStatus);

    connect(testButton, &QPushButton::clicked, this, &SettingsDialog::testApi);

    return tab;
}

QWidget *SettingsDialog::buildDatabaseTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QFormLayout(tab);

    const DbConfig &db = Config::instance().db();
    m_dbHost = new QLineEdit(db.host, tab);
    m_dbPort = new QSpinBox(tab);
    m_dbPort->setRange(1, 65535);
    m_dbPort->setValue(db.port);
    m_dbName = new QLineEdit(db.name, tab);
    m_dbUser = new QLineEdit(db.user, tab);
    m_dbPassword = new QLineEdit(db.password, tab);
    m_dbPassword->setEchoMode(QLineEdit::Password);
    auto *showPassword = new QToolButton(tab);
    connect(showPassword, &QToolButton::toggled, this,
            [this, showPassword] { toggleEcho(m_dbPassword, showPassword); });

    layout->addRow(QStringLiteral("Servidor:"), m_dbHost);
    layout->addRow(QStringLiteral("Porta:"), m_dbPort);
    layout->addRow(QStringLiteral("Banco:"), m_dbName);
    layout->addRow(QStringLiteral("Usuário:"), m_dbUser);
    layout->addRow(QStringLiteral("Senha:"),
                   passwordRow(m_dbPassword, showPassword, tab));

    auto *testButton = new QPushButton(QStringLiteral("Testar conexão"), tab);
    testButton->setMaximumWidth(180);
    layout->addRow(testButton);

    m_dbStatus = new QLabel(tab);
    m_dbStatus->setWordWrap(true);
    layout->addRow(m_dbStatus);

    auto *hint = new QLabel(
        QStringLiteral("A conexão é reaplicada ao salvar. Se falhar, o app continua com "
                       "a conexão atual."),
        tab);
    hint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addRow(hint);

    connect(testButton, &QPushButton::clicked, this, &SettingsDialog::testDatabase);

    return tab;
}

QWidget *SettingsDialog::buildCommandsTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);

    auto *hint = new QLabel(
        QStringLiteral("Comandos globais ficam disponíveis em qualquer conversa "
                       "(botão \"/ Comandos\")."),
        tab);
    hint->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(hint);

    m_commands = new QListWidget(tab);
    layout->addWidget(m_commands, 1);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(QStringLiteral("Adicionar"), tab);
    auto *editButton = new QPushButton(QStringLiteral("Editar"), tab);
    auto *removeButton = new QPushButton(QStringLiteral("Remover"), tab);
    buttons->addWidget(addButton);
    buttons->addWidget(editButton);
    buttons->addWidget(removeButton);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, &SettingsDialog::addGlobalCommand);
    connect(editButton, &QPushButton::clicked, this, &SettingsDialog::editGlobalCommand);
    connect(removeButton, &QPushButton::clicked, this,
            &SettingsDialog::removeGlobalCommand);

    refreshCommands();
    return tab;
}

QWidget *SettingsDialog::buildAboutTab()
{
    auto *tab = new QWidget(this);
    auto *layout = new QVBoxLayout(tab);
    layout->setAlignment(Qt::AlignTop);

    auto *logo = new QLabel(tab);
    logo->setPixmap(Assets::logoPixmap(96));
    logo->setAlignment(Qt::AlignCenter);
    layout->addWidget(logo);

    auto *title = new QLabel(QStringLiteral("DeepSeek Chat"), tab);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: bold;"));
    layout->addWidget(title);

    auto *version = new QLabel(QStringLiteral("Versão %1").arg(
                                   QString::fromLatin1(kAppVersion)), tab);
    version->setAlignment(Qt::AlignCenter);
    version->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(version);

    auto *about = new QLabel(
        QStringLiteral("Cliente desktop para a API DeepSeek.\n"
                       "Conversas, projetos, arquivos, comandos e histórico em MySQL.\n\n"
                       "Interface inspirada no ChatGPT."),
        tab);
    about->setAlignment(Qt::AlignCenter);
    about->setWordWrap(true);
    layout->addWidget(about);

    return tab;
}

void SettingsDialog::setModels(const QVector<ModelInfo> &models)
{
    if (!m_defaultModel)
        return;
    m_models = models;
    m_defaultModel->clear();
    for (const ModelInfo &info : models)
        m_defaultModel->addItem(info.id);
    const QString desired = Config::instance().deepseek().defaultModel;
    if (m_defaultModel->findText(desired) >= 0)
        m_defaultModel->setCurrentText(desired);
}

void SettingsDialog::toggleEcho(QLineEdit *edit, QToolButton *button)
{
    edit->setEchoMode(button->isChecked() ? QLineEdit::Normal : QLineEdit::Password);
    button->setText(button->isChecked() ? QStringLiteral("ocultar")
                                        : QStringLiteral("ver"));
}

void SettingsDialog::testApi()
{
    if (m_apiReply)
        return;

    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(QUrl(m_baseUrl->text().trimmed()
                                 + QStringLiteral("/models")));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + m_apiKey->text().trimmed().toUtf8());
    request.setTransferTimeout(20000);

    m_apiStatus->setText(QStringLiteral("Testando..."));
    m_apiReply = m_network->get(request);
    connect(m_apiReply, &QNetworkReply::finished, this,
            &SettingsDialog::onApiTestFinished);
}

void SettingsDialog::onApiTestFinished()
{
    if (!m_apiReply)
        return;
    QNetworkReply *reply = m_apiReply;
    m_apiReply = nullptr;

    const QByteArray data = reply->readAll();
    const auto error = reply->error();
    const QString errorText = reply->errorString();
    reply->deleteLater();

    if (error != QNetworkReply::NoError) {
        m_apiStatus->setText(QStringLiteral("Falhou: ") + errorText);
        return;
    }

    const QJsonArray list = QJsonDocument::fromJson(data)
                                .object().value(QStringLiteral("data")).toArray();
    QStringList ids;
    for (const QJsonValue &value : list)
        ids.append(value.toObject().value(QStringLiteral("id")).toString());
    m_apiStatus->setText(QStringLiteral("OK — %1 modelo(s): %2")
                             .arg(ids.size())
                             .arg(ids.join(QStringLiteral(", "))));
}

void SettingsDialog::testDatabase()
{
    const QString connectionName = QStringLiteral("settings_test");
    if (QSqlDatabase::contains(connectionName))
        QSqlDatabase::removeDatabase(connectionName);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"),
                                                connectionName);
    db.setHostName(m_dbHost->text().trimmed());
    db.setPort(m_dbPort->value());
    db.setDatabaseName(m_dbName->text().trimmed());
    db.setUserName(m_dbUser->text().trimmed());
    db.setPassword(m_dbPassword->text());
    db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5"));

    if (db.open()) {
        m_dbStatus->setText(QStringLiteral("OK — conexão estabelecida."));
        db.close();
    } else {
        m_dbStatus->setText(QStringLiteral("Falhou: ") + db.lastError().text());
    }
    QSqlDatabase::removeDatabase(connectionName);
}

void SettingsDialog::saveProfile()
{
    const QString name = m_displayName->text().trimmed();
    if (name.isEmpty())
        return;

    if (!Database::instance().updateDisplayName(m_user.id, name)) {
        m_status->setText(QStringLiteral("Não foi possível salvar o nome."));
        return;
    }
    m_user.displayName = name;
    Database::instance().logActivity(m_user.id, QStringLiteral("profile_update"),
                                     QStringLiteral("Nome: %1").arg(name));
    m_status->setText(QStringLiteral("Nome atualizado."));
    emit settingsChanged();
}

void SettingsDialog::changePassword()
{
    if (m_newPassword->text() != m_confirmPassword->text()) {
        m_status->setText(QStringLiteral("As senhas novas não conferem."));
        return;
    }

    QString error;
    if (!Auth::changePassword(m_user.id, m_oldPassword->text(),
                              m_newPassword->text(), &error)) {
        m_status->setText(error);
        return;
    }

    m_oldPassword->clear();
    m_newPassword->clear();
    m_confirmPassword->clear();
    m_status->setText(QStringLiteral("Senha alterada com sucesso."));
}

void SettingsDialog::refreshCommands()
{
    if (!m_commands)
        return;
    m_commands->clear();
    const QVector<Command> commands = Database::instance().commands(0);
    for (const Command &command : commands) {
        auto *item = new QListWidgetItem(QStringLiteral("/") + command.name, m_commands);
        item->setData(Qt::UserRole, command.id);
        item->setData(Qt::UserRole + 1, command.prompt);
        item->setToolTip(command.prompt.left(300));
    }
}

void SettingsDialog::addGlobalCommand()
{
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
    if (Database::instance().addCommand(0, name.trimmed(), prompt, &error) <= 0) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    refreshCommands();
    emit settingsChanged();
}

void SettingsDialog::editGlobalCommand()
{
    QListWidgetItem *item = m_commands->currentItem();
    if (!item)
        return;

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
    if (!Database::instance().updateCommand(item->data(Qt::UserRole).toLongLong(),
                                            name.trimmed(), prompt, &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    refreshCommands();
    emit settingsChanged();
}

void SettingsDialog::removeGlobalCommand()
{
    QListWidgetItem *item = m_commands->currentItem();
    if (!item)
        return;

    const auto answer = QMessageBox::question(
        this, QStringLiteral("Remover comando"),
        QStringLiteral("Remover %1?").arg(item->text()));
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!Database::instance().removeCommand(item->data(Qt::UserRole).toLongLong(),
                                            &error)) {
        QMessageBox::critical(this, QStringLiteral("Erro"), error);
        return;
    }
    refreshCommands();
    emit settingsChanged();
}

void SettingsDialog::saveAll()
{
    Config &config = Config::instance();

    config.ui().theme = m_themeCombo->currentData().toString();
    config.ui().fontSize = m_fontSpin->value();
    config.ui().enterToSend = m_enterToSend->isChecked();
    config.ui().showReasoning = m_showReasoning->isChecked();

    config.deepseek().apiKey = m_apiKey->text().trimmed();
    config.deepseek().baseUrl = m_baseUrl->text().trimmed();
    if (m_defaultModel->count() > 0)
        config.deepseek().defaultModel = m_defaultModel->currentText();

    const bool dbChanged = config.db().host != m_dbHost->text().trimmed()
        || config.db().port != m_dbPort->value()
        || config.db().name != m_dbName->text().trimmed()
        || config.db().user != m_dbUser->text().trimmed()
        || config.db().password != m_dbPassword->text();

    config.db().host = m_dbHost->text().trimmed();
    config.db().port = m_dbPort->value();
    config.db().name = m_dbName->text().trimmed();
    config.db().user = m_dbUser->text().trimmed();
    config.db().password = m_dbPassword->text();

    QString error;
    if (!config.save(&error)) {
        m_status->setText(error);
        return;
    }

    if (dbChanged) {
        if (!Database::instance().reconnect(&error))
            m_status->setText(QStringLiteral("Config salva, mas o banco falhou: ")
                              + error);
        else
            m_status->setText(QStringLiteral("Configurações salvas e banco reconectado."));
    } else {
        m_status->setText(QStringLiteral("Configurações salvas."));
    }

    Theme::apply(*qApp, config.ui().theme);
    emit settingsChanged();
}
