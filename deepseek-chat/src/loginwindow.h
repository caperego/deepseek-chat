#pragma once

#include "chattypes.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

class LoginWindow : public QDialog {
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

    User user() const { return m_user; }

private:
    void buildUi();
    void doLogin();
    void doRegister();
    void showStatus(const QString &message, bool error);
    void openConfig();

    User m_user;
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_loginUser = nullptr;
    QLineEdit *m_loginPass = nullptr;
    QLineEdit *m_regUser = nullptr;
    QLineEdit *m_regName = nullptr;
    QLineEdit *m_regPass = nullptr;
    QLineEdit *m_regPass2 = nullptr;
    QLabel *m_status = nullptr;
};
