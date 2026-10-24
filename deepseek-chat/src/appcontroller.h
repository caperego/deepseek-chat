#pragma once

#include "chattypes.h"
#include "automationstore.h"

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class ApiClient;
class StreamClient;
class QmlTheme;
class QTextToSpeech;
class QAudioSource;
class QBuffer;
class QProcess;
class QSystemTrayIcon;
class QSocketNotifier;
class QWindow;
class QTimer;
class QMenu;
class QPixmap;

class MessageListModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        RoleRole = Qt::UserRole + 1,
        ContentRole,
        DisplayContentRole,
        ReasoningRole,
        AttachmentNamesRole,
        FilesRole,
        ErrorRole,
        StreamingRole,
        MessageIdRole,
        DetailRole,
        SourcesRole
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setMessages(const QVector<Message> &messages);
    void clearMessages();
    void beginAssistant();
    void appendChunk(const QString &chunk);
    void appendReasoning(const QString &chunk);
    void finishAssistant(const QString &fullText, const QString &fullReasoning);
    void failAssistant(const QString &error);
    void appendTool(const QString &text, const QString &detail);
    void updateLastTool(const QString &text, const QString &detail);
    void resumeAssistant(const QString &text, const QString &reasoning);
    void removeLast();
    QString lastRole() const;

private:
    void flushPendingUpdate();
    QTimer *m_updateTimer = nullptr;
    int m_pendingRow = -1;
    static QString displayContent(const QString &content);
    static QVariantList extractFiles(const QString &content);

    struct Item {
        QString role;
        QString content;
        QString reasoning;
        QString error;
        QString detail;
        qint64 id = 0;
        QVariantList attachments;
        QVariantList sources;
        bool streaming = false;
    };

    QVector<Item> m_items;
};

struct AgentRun {
    QString automationId;
    StreamClient *stream = nullptr;
    qint64 conversationId = 0;
    qint64 projectId = 0;
    QString title;
    QString model;
    QString effort;
    QString workdir;
    QString permissionMode;
    bool allowImages = true;
    bool toolsEnabled = true;
    bool aborted = false;
    bool autoApprove = false;
    bool persistUser = true;
    bool generateTitle = false;
    bool awaitingProtocolRepair = false;
    int protocolRepairs = 0;
    int outputContinuations = 0;
    int round = 0;
    int toolIndex = 0;
    QVector<Message> messages;
    QVector<ToolCall> toolQueue;
    QJsonArray sources;
    ToolCall currentTool;
    QProcess *process = nullptr;
};

struct ApprovalRequest {
    int id = 0;
    QString title;
    QString detail;
    QString diff;
    QString toolName;
    AgentRun *run = nullptr;
    std::function<void(bool)> callback;
};

struct McpServer {
    QString name;
    QJsonObject config;
    QProcess *process = nullptr;
    bool initialized = false;
    bool listing = false;
    bool capsResources = false;
    bool capsPrompts = false;
    bool hasResources = false;
    bool hasPrompts = false;
    QByteArray buffer;
    QString status = QStringLiteral("Iniciando");
};

struct McpTool {
    QString server;
    QString name;
    QString description;
    QJsonObject schema;
};

class AppController : public QObject {
    Q_OBJECT
    friend class AgentRegression;
    friend class UiPreview;

    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loggedInChanged)
    Q_PROPERTY(bool restoringSession READ restoringSession NOTIFY restoringSessionChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY userChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY userChanged)
    Q_PROPERTY(QVariantList sidebar READ sidebar NOTIFY sidebarChanged)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY projectsChanged)
    Q_PROPERTY(QVariantList projectItems READ projectItems NOTIFY projectItemsChanged)
    Q_PROPERTY(QVariantList projectCommands READ projectCommands NOTIFY projectCommandsChanged)
    Q_PROPERTY(QVariantList globalCommands READ globalCommands NOTIFY globalCommandsChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList mcpServers READ mcpServers NOTIFY mcpServersChanged)
    Q_PROPERTY(QVariantList automations READ automations NOTIFY automationsChanged)
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(QVariantList activities READ activities NOTIFY activitiesChanged)
    Q_PROPERTY(MessageListModel *messageModel READ messageModel CONSTANT)
    Q_PROPERTY(qint64 currentConversationId READ currentConversationId NOTIFY currentConversationChanged)
    Q_PROPERTY(qint64 currentParentId READ currentParentId NOTIFY currentConversationChanged)
    Q_PROPERTY(QVariantList streamingConversationIds READ streamingConversationIds NOTIFY streamingChanged)
    Q_PROPERTY(QVariantList streamingProjectIds READ streamingProjectIds NOTIFY streamingChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentModel READ currentModel WRITE setCurrentModel NOTIFY currentConversationChanged)
    Q_PROPERTY(QString currentEffort READ currentEffort WRITE setCurrentEffort NOTIFY currentConversationChanged)
    Q_PROPERTY(QStringList efforts READ efforts NOTIFY effortsChanged)
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)
    Q_PROPERTY(int viewMode READ viewMode NOTIFY viewModeChanged)
    Q_PROPERTY(qint64 activeProjectId READ activeProjectId NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectName READ activeProjectName NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectDescription READ activeProjectDescription NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectWorkdir READ activeProjectWorkdir NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectPrompt READ activeProjectPrompt NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectModel READ activeProjectModel NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectEffort READ activeProjectEffort NOTIFY activeProjectChanged)
    Q_PROPERTY(QString activeProjectPermissionMode READ activeProjectPermissionMode NOTIFY activeProjectChanged)
    Q_PROPERTY(QVariantList workdirFiles READ workdirFiles NOTIFY workdirFilesChanged)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    Q_PROPERTY(QString apiUrl READ apiUrl NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool enterToSend READ enterToSend NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool showReasoning READ showReasoning NOTIFY uiSettingsChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY uiSettingsChanged)
    Q_PROPERTY(int sidebarWidth READ sidebarWidth NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool sidebarCollapsed READ sidebarCollapsed NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool ttsEnabled READ ttsEnabled NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool sttEnabled READ sttEnabled NOTIFY uiSettingsChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QString ttsStatus READ ttsStatus NOTIFY uiSettingsChanged)
    Q_PROPERTY(QVariantMap usageInfo READ usageInfo NOTIFY usageChanged)
    Q_PROPERTY(QVariantMap balanceInfo READ balanceInfo NOTIFY balanceChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QVariantList projectMemories READ projectMemories NOTIFY projectMemoriesChanged)
    Q_PROPERTY(bool terminalRunning READ terminalRunning NOTIFY terminalChanged)
    Q_PROPERTY(bool artifactVisible READ artifactVisible NOTIFY artifactChanged)
    Q_PROPERTY(QString artifactPath READ artifactPath NOTIFY artifactChanged)
    Q_PROPERTY(QString artifactContent READ artifactContent NOTIFY artifactChanged)
    Q_PROPERTY(QVariantList artifactVersions READ artifactVersions NOTIFY artifactChanged)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateStatusChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY updateStatusChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    void setTheme(QmlTheme *theme) { m_theme = theme; }
    void setWindow(QWindow *window);

    bool loggedIn() const { return m_user.id > 0; }
    bool restoringSession() const { return m_restoringSession; }
    QString userName() const { return m_user.username; }
    QString displayName() const { return m_user.displayName; }
    QVariantList sidebar() const { return m_sidebar; }
    QVariantList projects() const { return m_projects; }
    QVariantList projectItems() const { return m_projectItems; }
    QVariantList projectCommands() const { return m_projectCommands; }
    QVariantList globalCommands() const { return m_globalCommands; }
    QVariantList models() const { return m_modelsList; }
    QVariantList mcpServers() const;
    QVariantList automations() const { return m_automations.tasks(); }
    Q_INVOKABLE bool addAutomation(const QString &title, const QString &prompt,
                                   int delayMinutes, int repeatMinutes, qint64 projectId);
    Q_INVOKABLE void toggleAutomation(const QString &id);
    Q_INVOKABLE void removeAutomation(const QString &id);
    Q_INVOKABLE void stopAutomation(const QString &id);
    Q_INVOKABLE bool addMcpServer(const QString &name, const QString &command, const QString &arguments);
    Q_INVOKABLE void setMcpServerEnabled(const QString &name, bool enabled);
    QVariantList attachments() const { return m_attachments; }
    QVariantList activities() const { return m_activities; }
    MessageListModel *messageModel() { return m_messages; }
    qint64 currentConversationId() const { return m_currentConversationId; }
    qint64 currentParentId() const { return m_currentParentId; }
    QVariantList streamingConversationIds() const;
    QVariantList streamingProjectIds() const;
    QString currentTitle() const { return m_currentTitle; }
    QString currentModel() const { return m_currentModel; }
    void setCurrentModel(const QString &model);
    QString currentEffort() const { return m_currentEffort; }
    void setCurrentEffort(const QString &effort);
    QStringList efforts() const { return m_efforts; }
    bool streaming() const;
    int viewMode() const { return m_viewMode; }
    qint64 activeProjectId() const { return m_activeProjectId; }
    QString activeProjectName() const { return m_activeProject.name; }
    QString activeProjectDescription() const { return m_activeProject.description; }
    QString activeProjectWorkdir() const { return m_activeProject.workdir; }
    QString activeProjectPrompt() const { return m_activeProject.systemPrompt; }
    QString activeProjectModel() const { return m_activeProject.defaultModel; }
    QString activeProjectEffort() const { return m_activeProject.defaultEffort; }
    QString activeProjectPermissionMode() const { return permissionMode(); }
    QVariantList workdirFiles() const { return m_workdirFiles; }
    QString configPath() const;
    QString appVersion() const;
    QString apiUrl() const;
    bool enterToSend() const;
    bool showReasoning() const;
    int fontSize() const;
    int sidebarWidth() const;
    bool sidebarCollapsed() const;
    bool ttsEnabled() const;
    bool sttEnabled() const;
    bool recording() const { return m_recording; }
    QString ttsStatus() const;
    QVariantMap usageInfo() const { return m_usage; }
    QVariantMap balanceInfo() const { return m_balanceInfo; }
    QVariantList searchResults() const { return m_searchResults; }
    QVariantList projectMemories() const { return m_projectMemories; }
    bool terminalRunning() const;
    bool artifactVisible() const { return !m_artifactPath.isEmpty(); }
    QString artifactPath() const { return m_artifactPath; }
    QString artifactContent() const { return m_artifactContent; }
    QVariantList artifactVersions() const { return m_artifactVersions; }
    QString updateStatus() const { return m_updateStatus; }
    bool updateAvailable() const { return m_updateAvailable; }

    Q_INVOKABLE void tryAutoLogin();
    Q_INVOKABLE void login(const QString &username, const QString &password);
    Q_INVOKABLE void registerUser(const QString &username, const QString &displayName,
                                  const QString &password);
    Q_INVOKABLE void logout();

    Q_INVOKABLE void newConversation(qint64 projectId);
    Q_INVOKABLE void openConversation(qint64 id);
    Q_INVOKABLE void openProject(qint64 id);
    Q_INVOKABLE void createProject(const QVariantMap &data);
    Q_INVOKABLE void saveActiveProject(const QVariantMap &data);
    Q_INVOKABLE void deleteActiveProject();
    Q_INVOKABLE void deleteProject(qint64 id);
    Q_INVOKABLE void renameConversation(qint64 id, const QString &title);
    Q_INVOKABLE void deleteConversation(qint64 id);
    Q_INVOKABLE bool isConversationPinned(qint64 id) const;
    Q_INVOKABLE void toggleConversationPinned(qint64 id);
    Q_INVOKABLE QString conversationTags(qint64 id) const;
    Q_INVOKABLE void setConversationTags(qint64 id, const QString &tags);
    Q_INVOKABLE void branchConversation(qint64 id);
    Q_INVOKABLE void branchFromMessage(qint64 messageId);
    Q_INVOKABLE void convertConversationToProject(qint64 id);
    Q_INVOKABLE void showChatView();

    Q_INVOKABLE void send(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshModels();

    Q_INVOKABLE void attachFiles();
    Q_INVOKABLE void captureScreenRegion();
    Q_INVOKABLE void captureAppWindow();
    Q_INVOKABLE bool pasteClipboard();
    Q_INVOKABLE void attachDroppedUrls(const QVariantList &urls);
    Q_INVOKABLE void removeAttachment(int index);
    Q_INVOKABLE void clearAttachments();
    Q_INVOKABLE void addProjectFiles();
    Q_INVOKABLE void removeProjectItem(qint64 id);
    Q_INVOKABLE bool addProjectMemory(const QString &content);
    Q_INVOKABLE bool updateProjectMemory(qint64 id, const QString &content);
    Q_INVOKABLE void deleteProjectMemory(qint64 id);
    Q_INVOKABLE void rememberMessage(qint64 messageId);
    Q_INVOKABLE void attachProjectItem(qint64 id);
    Q_INVOKABLE void openWorkdir();

    Q_INVOKABLE void addProjectCommand(const QString &name, const QString &prompt);
    Q_INVOKABLE void updateProjectCommand(qint64 id, const QString &name,
                                          const QString &prompt);
    Q_INVOKABLE void removeProjectCommand(qint64 id);
    Q_INVOKABLE void addGlobalCommand(const QString &name, const QString &prompt);
    Q_INVOKABLE void updateGlobalCommand(qint64 id, const QString &name,
                                         const QString &prompt);
    Q_INVOKABLE void removeGlobalCommand(qint64 id);

    Q_INVOKABLE void applyFile(const QString &path, const QString &content);
    Q_INVOKABLE void refreshActivities();
    Q_INVOKABLE void logClientActivity(const QString &action, const QString &details);
    Q_INVOKABLE void saveSettings(const QVariantMap &data);
    Q_INVOKABLE void testConnection(const QString &url);
    Q_INVOKABLE void changeDisplayName(const QString &name);
    Q_INVOKABLE void changePassword(const QString &oldPassword,
                                    const QString &newPassword);
    Q_INVOKABLE void openConfigFile();
    Q_INVOKABLE QVariantMap currentSettings() const;
    Q_INVOKABLE void saveNavigationLayout(int width, bool collapsed);
    Q_INVOKABLE QString chooseDirectory(const QString &title);
    Q_INVOKABLE void refreshWorkdirFiles();
    Q_INVOKABLE void attachWorkdirFile(const QString &path);
    Q_INVOKABLE void openPath(const QString &path);
    Q_INVOKABLE void resolveApproval(bool allow, bool always);
    Q_INVOKABLE void resolveApprovalEx(bool allow, bool alwaysSession,
                                       bool alwaysProject);
    Q_INVOKABLE void copyToClipboard(const QString &text);
    Q_INVOKABLE void attachUrl(const QString &url);
    Q_INVOKABLE void openExternal(const QString &url);
    Q_INVOKABLE void regenerateLast();
    Q_INVOKABLE void editMessage(qint64 messageId, const QString &newText);
    Q_INVOKABLE void searchMessages(const QString &query);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void runTerminalCommand(const QString &command);
    Q_INVOKABLE void sendTerminalInput(const QString &text);
    Q_INVOKABLE void killTerminal();
    Q_INVOKABLE QString gitStatusText();
    Q_INVOKABLE void exportCurrentConversation();
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void downloadUpdate();
    Q_INVOKABLE void openArtifact(const QString &path);
    Q_INVOKABLE void closeArtifact();
    Q_INVOKABLE void saveArtifact(const QString &content);
    Q_INVOKABLE void restoreArtifactVersion(const QString &versionPath);
    Q_INVOKABLE QString artifactDiff(const QString &edited) const;
    Q_INVOKABLE QString artifactPreviewUrl(const QString &edited) const;
    Q_INVOKABLE void reloadMcp();
    Q_INVOKABLE void openMcpFile();

    Q_INVOKABLE QStringList ttsVoices() const;
    Q_INVOKABLE void testVoice(const QString &text);
    Q_INVOKABLE void stopSpeaking();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void refreshUsage();
    Q_INVOKABLE void refreshBalance();
    Q_INVOKABLE QString estimateMessageCost(const QString &text) const;
    Q_INVOKABLE void restoreWindow();

signals:
    void loggedInChanged();
    void restoringSessionChanged();
    void userChanged();
    void sidebarChanged();
    void projectsChanged();
    void projectItemsChanged();
    void projectCommandsChanged();
    void globalCommandsChanged();
    void modelsChanged();
    void mcpServersChanged();
    void automationsChanged();
    void attachmentsChanged();
    void activitiesChanged();
    void currentConversationChanged();
    void effortsChanged();
    void streamingChanged();
    void viewModeChanged();
    void activeProjectChanged();
    void uiSettingsChanged();
    void recordingChanged();
    void usageChanged();
    void workdirFilesChanged();
    void loginFailed(const QString &message);
    void registerFailed(const QString &message);
    void toast(const QString &message);
    void connectionTestDone(const QString &message);
    void composerTextRequested(const QString &text);
    void approvalRequested(int id, const QString &title, const QString &detail,
                           const QString &diff);
    void approvalClosed();
    void searchResultsChanged();
    void projectMemoriesChanged();
    void terminalOutput(const QString &text);
    void terminalChanged();
    void artifactChanged();
    void updateStatusChanged();
    void balanceChanged();

private:
    void afterLogin(const User &user);
    void rebuildSidebar();
    void rebuildProjects();
    void loadProjectDetails(qint64 projectId);
    void refreshProjectMemories();
    void refreshArtifactVersions();
    void loadGlobalCommands();
    void fillModels(const QVector<ModelInfo> &models);
    void updateEfforts();
    void openConversationInternal(qint64 id);
    void setActiveProject(qint64 id);
    double costForModel(const QString &model, qint64 prompt, qint64 completion,
                        qint64 cached) const;
    bool isPeakNow() const;
    void configureTts();
    void transcribeRecording(const QString &file);
    bool addAttachmentPath(const QString &path);
    void attachCapturedPixmap(const QPixmap &pixmap);
    QString permissionMode() const;
    QString agentContextText() const;
    static QString folderNameForTitle(const QString &title);

    AgentRun *runFor(qint64 conversationId) const;
    bool displayingRun(const AgentRun *run) const;
    void destroyRun(AgentRun *run);
    void destroyAllRuns();
    void startRun(const QString &title, bool persistUser);
    void connectRun(AgentRun *run);
    void pollAutomations();
    void runAutomation(const QVariantMap &task);
    void stopRun(AgentRun *run);
    void sendRound(AgentRun *run);
    void continueRun(AgentRun *run, const QString &instruction);
    void onRunFinished(AgentRun *run, const QString &fullText,
                       const QString &fullReasoning);
    void finishRun(AgentRun *run, const QString &fullText, const QString &fullReasoning);
    void failRun(AgentRun *run, const QString &error);
    void handleToolCalls(AgentRun *run, const QVector<ToolCall> &calls);
    void runNextTool(AgentRun *run);
    void executeToolCall(AgentRun *run, const ToolCall &call);
    void performToolCall(AgentRun *run, const ToolCall &call);
    void finishToolCall(AgentRun *run, const QString &result, const QString &summary);
    void fetchUrl(AgentRun *run, const ToolCall &call);
    void browseUrl(AgentRun *run, const ToolCall &call);
    void searchWeb(AgentRun *run, const ToolCall &call);
    void downloadFile(AgentRun *run, const ToolCall &call);
    void runCommand(AgentRun *run, const ToolCall &call);
    void maybeQueueNext(qint64 conversationId);
    void maybeGenerateTitle(qint64 conversationId, const QString &model,
                            const QString &effort);
    void requestCompactionSummary(qint64 conversationId, const QString &model,
                                  const QString &effort);
    void launchUpdateScript(const QString &zipPath, const QString &extractDir);
    void loadMcpServers();
    void trackMcpCall(McpServer *server, int id, AgentRun *run, const QString &tool);
    void failMcpCalls(McpServer *server, const QString &reason);
    bool saveMcpServer(const QString &name, const QJsonObject &config, bool replace);
    void startMcpServer(McpServer *server);
    void updateTrayTooltip();
    void handleMcpOutput(McpServer *server);
    void mcpSend(McpServer *server, const QJsonObject &message);
    void mcpCallTool(AgentRun *run, const ToolCall &call);
    static QString mcpPublicName(const QString &server, const QString &tool);
    static QString mcpJsonText(const QJsonObject &object);
    void requestApproval(AgentRun *run, const QString &title, const QString &detail,
                         const QString &diff, const QString &toolName,
                         std::function<void(bool)> callback);
    void showNextApproval();
    void removeApprovalsFor(AgentRun *run);
    bool toolNeedsApproval(const AgentRun *run, const ToolCall &call,
                           bool *outsideWorkdir) const;
    static QString resolvePathFor(const QString &workdir, const QString &path);
    static bool insideDir(const QString &workdir, const QString &absolutePath);
    static QString diffText(const QString &before, const QString &after);
    static void backupFile(const QString &path);
    bool missingWorkdir(const AgentRun *run, const QString &path) const;
    bool runListFiles(AgentRun *run, const ToolCall &call, QString *result,
                      QString *summary);
    bool runReadFile(AgentRun *run, const ToolCall &call, QString *result,
                     QString *summary);
    bool runWriteFile(AgentRun *run, const ToolCall &call, QString *result,
                      QString *summary);
    bool runFindFiles(AgentRun *run, const ToolCall &call, QString *result,
                      QString *summary);
    bool runGrepFiles(AgentRun *run, const ToolCall &call, QString *result,
                      QString *summary);
    bool runRestoreFile(AgentRun *run, const ToolCall &call, QString *result,
                        QString *summary);
    QJsonArray toolDefinitions();
    static QJsonObject parseArguments(const ToolCall &call, QString *error);
    static QString toolApprovalTitle(const ToolCall &call);
    QString toolApprovalDetail(const AgentRun *run, const ToolCall &call,
                               bool outsideWorkdir) const;
    static QString htmlToText(const QString &html);
    static QVariantMap conversationMap(const Conversation &c);
    static QVariantMap projectMap(const Project &p);
    static QVariantMap itemMap(const ProjectItem &item);
    static QVariantMap commandMap(const Command &command);

    User m_user;
    bool m_restoringSession = false;
    ApiClient *m_api = nullptr;
    QmlTheme *m_theme = nullptr;
    MessageListModel *m_messages = nullptr;
    QTextToSpeech *m_tts = nullptr;
    QAudioSource *m_audioSource = nullptr;
    QBuffer *m_audioBuffer = nullptr;
    QByteArray m_audioData;
    int m_recordSampleRate = 16000;
    int m_recordChannels = 1;
    bool m_recording = false;
    QString m_recordFile;
    int m_lastPromptTokens = 0;
    int m_lastCompletionTokens = 0;
    int m_lastCachedTokens = 0;
    QVariantMap m_usage;
    QVariantMap m_balanceInfo;
    QVector<ModelInfo> m_models;
    QVector<Project> m_projectList;

    QVariantList m_sidebar;
    QVariantList m_projects;
    QVariantList m_projectItems;
    QVariantList m_projectCommands;
    QVariantList m_globalCommands;
    QVariantList m_modelsList;
    QVariantList m_attachments;
    QVariantList m_activities;
    QVariantList m_workdirFiles;

    QVector<AgentRun *> m_runs;
    QVector<ApprovalRequest> m_approvals;
    bool m_approvalShown = false;
    int m_approvalId = 0;
    QVariantList m_searchResults;
    QVariantList m_projectMemories;
    QVariantList m_artifactVersions;
    QHash<qint64, QString> m_queuedText;
    QSet<qint64> m_titlePending;
    QHash<qint64, QString> m_compaction;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    QWindow *m_window = nullptr;
    bool m_forceQuit = false;
    bool m_trayNotified = false;
    QProcess *m_terminal = nullptr;
    void *m_conpty = nullptr;
    bool m_terminalRunning = false;
    int m_ptyMaster = -1;
    QSocketNotifier *m_ptyNotifier = nullptr;
    QString m_artifactPath;
    QString m_artifactContent;
    QString m_updateStatus;
    bool m_updateAvailable = false;
    QString m_pendingUpdateUrl;
    qint64 m_notifyConversation = 0;
    QVector<McpServer *> m_mcpServers;
    QVector<McpTool> m_mcpTools;
    bool m_mcpLoaded = false;
    int m_nextMcpId = 1;
    AutomationStore m_automations;
    QString m_automationBackend;
    bool m_pollingAutomations = false;
    QHash<QPair<QString, int>, AgentRun *> m_mcpCallRuns;
    QHash<QPair<QString, int>, QString> m_mcpCallTools;

    Project m_activeProject;
    qint64 m_activeProjectId = 0;
    qint64 m_currentConversationId = 0;
    qint64 m_currentParentId = 0;
    QString m_currentTitle;
    QString m_currentModel;
    QString m_currentEffort;
    QStringList m_efforts;
    QVector<Attachment> m_pendingAttachments;
    QVector<Message> m_history;
    int m_viewMode = 0;
};
