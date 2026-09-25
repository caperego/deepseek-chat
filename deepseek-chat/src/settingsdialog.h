#pragma once

#include "chattypes.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QSpinBox;
class QToolButton;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const User &user, QWidget *parent = nullptr);

    void setModels(const QVector<ModelInfo> &models);

signals:
    void settingsChanged();

private:
    QWidget *buildAppearanceTab();
    QWidget *buildProfileTab();
    QWidget *buildApiTab();
    QWidget *buildDatabaseTab();
    QWidget *buildCommandsTab();
    QWidget *buildAboutTab();

    void saveAll();
    void testApi();
    void onApiTestFinished();
    void testDatabase();
    void refreshCommands();
    void addGlobalCommand();
    void editGlobalCommand();
    void removeGlobalCommand();
    void saveProfile();
    void changePassword();
    void toggleEcho(QLineEdit *edit, QToolButton *button);

    User m_user;
    QVector<ModelInfo> m_models;

    QComboBox *m_themeCombo = nullptr;
    QSpinBox *m_fontSpin = nullptr;
    QCheckBox *m_enterToSend = nullptr;
    QCheckBox *m_showReasoning = nullptr;

    QLineEdit *m_displayName = nullptr;
    QLineEdit *m_oldPassword = nullptr;
    QLineEdit *m_newPassword = nullptr;
    QLineEdit *m_confirmPassword = nullptr;

    QLineEdit *m_apiKey = nullptr;
    QLineEdit *m_baseUrl = nullptr;
    QComboBox *m_defaultModel = nullptr;
    QLabel *m_apiStatus = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_apiReply = nullptr;

    QLineEdit *m_dbHost = nullptr;
    QSpinBox *m_dbPort = nullptr;
    QLineEdit *m_dbName = nullptr;
    QLineEdit *m_dbUser = nullptr;
    QLineEdit *m_dbPassword = nullptr;
    QLabel *m_dbStatus = nullptr;

    QListWidget *m_commands = nullptr;

    QLabel *m_status = nullptr;
};
