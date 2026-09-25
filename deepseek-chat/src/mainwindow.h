#pragma once

#include "chattypes.h"
#include "messagebubble.h"

#include <QMainWindow>
#include <QVector>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class DeepSeekClient;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const User &user, QWidget *parent = nullptr);

signals:
    void logoutRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void buildSidebar(QWidget *sidebar);
    QWidget *buildChatPage();
    QWidget *buildProjectPage();

    void reloadProjects();
    void reloadTree(qint64 selectProjectId = 0, qint64 selectConversationId = 0);
    void filterTree(const QString &term);
    void onTreeSelection();
    void onTreeContextMenu(const QPoint &pos);

    void newConversation(qint64 projectId);
    void newProject();
    void openConversation(qint64 id);
    void showProject(qint64 projectId);
    void loadProjectIntoTab(qint64 projectId);
    void saveProject();
    void deleteProject();
    void renameSelectedConversation();
    void deleteSelectedConversation();

    void refreshModels();
    void updateEffortCombo(const QString &modelId);

    void sendMessage();
    void stopStreaming();
    void setStreaming(bool streaming);
    void onChunk(const QString &text);
    void onReasoning(const QString &text);
    void onFinished(const QString &fullText, const QString &fullReasoning);
    void onFailed(const QString &error);
    void onModelsFetched(const QVector<ModelInfo> &models);
    void onModelsFailed(const QString &error);

    void attachFiles();
    void addAttachment(const Attachment &attachment);
    void rebuildAttachmentChips();
    void clearAttachments();

    void showCommandsMenu();
    void addCommand();
    void editCommand();
    void removeCommand();

    void addProjectFiles();
    void removeProjectFile();
    void openWorkdir();
    void attachProjectItem();

    void applyFileFromModel(const QString &path, const QString &content);
    void showActivities();
    void openSettings();
    void applySettings();
    void doLogout();

    MessageBubble *addBubble(MessageBubble::Role role, const QString &text = QString());
    void clearMessages();
    void renderMessages(const QVector<Message> &messages);
    void scrollToBottom();
    QString currentModel() const;
    QString currentEffort() const;
    Project *activeProject();
    const ModelInfo *modelInfo(const QString &id) const;
    void logActivity(const QString &action, const QString &details);

    User m_user;
    DeepSeekClient *m_client = nullptr;
    QVector<ModelInfo> m_models;
    QVector<Project> m_projects;
    QVector<Attachment> m_pendingAttachments;

    qint64 m_activeProjectId = 0;
    qint64 m_currentConversationId = 0;
    QVector<Message> m_history;

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_userLabel = nullptr;
    QPushButton *m_newChatButton = nullptr;
    QPushButton *m_newProjectButton = nullptr;

    QStackedWidget *m_stack = nullptr;

    QLabel *m_chatTitle = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_effortCombo = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_messagesHost = nullptr;
    QVBoxLayout *m_messagesLayout = nullptr;

    QWidget *m_chipBar = nullptr;
    QHBoxLayout *m_chipLayout = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_attachButton = nullptr;
    QPushButton *m_commandsButton = nullptr;

    QLabel *m_projectTitle = nullptr;
    QLineEdit *m_projName = nullptr;
    QLineEdit *m_projDescription = nullptr;
    QLineEdit *m_projWorkdir = nullptr;
    QPlainTextEdit *m_projPrompt = nullptr;
    QComboBox *m_projModel = nullptr;
    QComboBox *m_projEffort = nullptr;
    QListWidget *m_projFiles = nullptr;
    QListWidget *m_projCommands = nullptr;

    MessageBubble *m_streamingBubble = nullptr;
};
