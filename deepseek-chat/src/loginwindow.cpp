#include "loginwindow.h"
#include "auth.h"
#include "config.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

LoginWindow::LoginWindow(QWidget *parent)
    : QDialog(parent)
{
    buildUi();
}

void LoginWindow::buildUi()
{
    setWindowTitle(QStringLiteral("DeepSeek Chat — Acesso"));
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);

    auto *title = new QLabel(QStringLiteral("DeepSeek Chat"), this);
    title->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: bold;"));
    title->setAlignment(Qt::AlignCenter);
    root->addWidget(title);

    auto *subtitle = new QLabel(QStringLiteral("Entre para continuar suas conversas"), this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(QStringLiteral("color: #6b7280;"));
    root->addWidget(subtitle);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs);

    auto *loginTab = new QWidget(m_tabs);
    auto *loginForm = new QFormLayout(loginTab);
    m_loginUser = new QLineEdit(loginTab);
    m_loginUser->setPlaceholderText(QStringLiteral("usuário"));
    m_loginPass = new QLineEdit(loginTab);
    m_loginPass->setEchoMode(QLineEdit::Password);
    m_loginPass->setPlaceholderText(QStringLiteral("senha"));
    loginForm->addRow(QStringLiteral("Usuário:"), m_loginUser);
    loginForm->addRow(QStringLiteral("Senha:"), m_loginPass);
    auto *loginButton = new QPushButton(QStringLiteral("Entrar"), loginTab);
    loginButton->setDefault(true);
    loginForm->addRow(loginButton);
    m_tabs->addTab(loginTab, QStringLiteral("Entrar"));

    auto *regTab = new QWidget(m_tabs);
    auto *regForm = new QFormLayout(regTab);
    m_regUser = new QLineEdit(regTab);
    m_regUser->setPlaceholderText(QStringLiteral("mínimo 3 caracteres"));
    m_regName = new QLineEdit(regTab);
    m_regName->setPlaceholderText(QStringLiteral("opcional"));
    m_regPass = new QLineEdit(regTab);
    m_regPass->setEchoMode(QLineEdit::Password);
    m_regPass2 = new QLineEdit(regTab);
    m_regPass2->setEchoMode(QLineEdit::Password);
    regForm->addRow(QStringLiteral("Usuário:"), m_regUser);
    regForm->addRow(QStringLiteral("Nome:"), m_regName);
    regForm->addRow(QStringLiteral("Senha:"), m_regPass);
    regForm->addRow(QStringLiteral("Confirmar:"), m_regPass2);
    auto *regButton = new QPushButton(QStringLiteral("Criar conta"), regTab);
    regForm->addRow(regButton);
    m_tabs->addTab(regTab, QStringLiteral("Criar conta"));

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *bottom = new QHBoxLayout;
    auto *configButton = new QPushButton(QStringLiteral("config.ini"), this);
    configButton->setFlat(true);
    bottom->addWidget(configButton);
    bottom->addStretch();
    auto *quitButton = new QPushButton(QStringLiteral("Sair"), this);
    bottom->addWidget(quitButton);
    root->addLayout(bottom);

    connect(loginButton, &QPushButton::clicked, this, &LoginWindow::doLogin);
    connect(regButton, &QPushButton::clicked, this, &LoginWindow::doRegister);
    connect(m_loginPass, &QLineEdit::returnPressed, this, &LoginWindow::doLogin);
    connect(m_regPass2, &QLineEdit::returnPressed, this, &LoginWindow::doRegister);
    connect(quitButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(configButton, &QPushButton::clicked, this, &LoginWindow::openConfig);
}

void LoginWindow::doLogin()
{
    QString error;
    User logged;
    if (!Auth::login(m_loginUser->text(), m_loginPass->text(), &logged, &error)) {
        showStatus(error, true);
        return;
    }
    m_user = logged;
    accept();
}

void LoginWindow::doRegister()
{
    if (m_regPass->text() != m_regPass2->text()) {
        showStatus(QStringLiteral("As senhas não conferem."), true);
        return;
    }

    QString error;
    User created;
    if (!Auth::registerUser(m_regUser->text(), m_regName->text(),
                            m_regPass->text(), &created, &error)) {
        showStatus(error, true);
        return;
    }

    m_user = created;
    accept();
}

void LoginWindow::showStatus(const QString &message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #b91c1c;")
                                  : QStringLiteral("color: #15803d;"));
}

void LoginWindow::openConfig()
{
    const QString path = Config::instance().filePath();
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
