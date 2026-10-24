import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel
    objectName: "workspaceTools"
    color: theme.card
    border.color: theme.border
    radius: 12
    property bool expanded: false
    property int currentTab: 0
    property int selectedTask: 0
    property bool browserStarted: false
    property string terminalText: ""
    property string pendingTerminalText: ""
    property int taskRevision: 0
    signal closeRequested()
    signal expandRequested()
    function selectTab(tab) {
        currentTab = tab
        if (tab === 1) browserStarted = true
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            AppTabBar {
                Layout.fillWidth: true
                currentIndex: panel.currentTab
                onCurrentIndexChanged: panel.selectTab(currentIndex)
                AppTabButton { text: "Tarefas"; icon.source: "qrc:/resources/icons/list.svg" }
                AppTabButton { text: "Navegador"; icon.source: "qrc:/resources/icons/globe.svg" }
                AppTabButton { text: "Terminal"; icon.source: "qrc:/resources/icons/terminal.svg" }
            }
            AppButton { text: panel.expanded ? "Reduzir" : "Ampliar"; ghost: true; onClicked: panel.expandRequested() }
            AppButton { icon.source: "qrc:/resources/icons/close.svg"; ghost: true; onClicked: panel.closeRequested(); ToolTip.text: "Recolher painel; manter tarefas"; ToolTip.visible: hovered }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.currentTab
            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    AppTextField { id: taskCommand; Layout.fillWidth: true; placeholderText: "Comando em segundo plano, ex.: npm run dev"; onAccepted: startTask.clicked() }
                    AppButton {
                        id: startTask
                        text: "Executar"
                        primary: true
                        enabled: taskCommand.text.trim().length > 0
                        onClicked: {
                            var id = app.startBackgroundTask(taskCommand.text)
                            if (id > 0) { panel.selectedTask = id; taskCommand.text = "" }
                        }
                    }
                    AppButton { text: "Limpar concluídas"; ghost: true; onClicked: { app.clearFinishedTasks(); panel.selectedTask = 0 } }
                }
                Label { text: "Pasta: " + (app.activeProjectWorkdir || "Pasta pessoal") + " · Tarefas continuam com o aplicativo aberto"; color: theme.muted; Layout.fillWidth: true; elide: Text.ElideMiddle }
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: Qt.Horizontal
                    ScrollView {
                        id: taskListScroll
                        SplitView.preferredWidth: 320
                        SplitView.minimumWidth: 150
                        ColumnLayout {
                            width: taskListScroll.availableWidth
                            Repeater {
                                model: app.activeRuns
                                delegate: RowLayout {
                                    required property var modelData
                                    Label { Layout.fillWidth: true; text: "Chat · " + modelData.title + " · etapa " + modelData.round; color: theme.text; elide: Text.ElideRight }
                                    AppButton { text: "Parar"; onClicked: app.stopConversationRun(modelData.conversationId) }
                                }
                            }
                            Repeater {
                                model: app.backgroundTasks
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: taskRow.implicitHeight + 12
                                    color: panel.selectedTask === modelData.id ? theme.selected : "transparent"
                                    radius: 8
                                    HoverHandler { id: taskHover }
                                    ToolTip.visible: taskHover.hovered
                                    ToolTip.text: modelData.workdir + "\n" + modelData.command
                                    RowLayout {
                                        id: taskRow
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Label { Layout.fillWidth: true; text: "#" + modelData.id + " · " + modelData.status; color: theme.text; elide: Text.ElideRight }
                                            Label { Layout.fillWidth: true; text: modelData.command; color: theme.muted; elide: Text.ElideRight }
                                            TapHandler { onTapped: panel.selectedTask = modelData.id }
                                        }
                                        AppButton { visible: modelData.running; text: "Parar"; danger: true; onClicked: app.stopBackgroundTask(modelData.id) }
                                    }
                                }
                            }
                            Label { visible: app.backgroundTasks.length === 0 && app.activeRuns.length === 0; text: "Nenhuma tarefa em execução"; color: theme.muted }
                        }
                    }
                    ScrollView {
                        SplitView.fillWidth: true
                        CopyableTextArea {
                            id: taskOutput
                            objectName: "backgroundTaskOutput"
                            readOnly: true
                            wrapMode: TextArea.NoWrap
                            font.family: "monospace"
                            font.pointSize: theme.codeFontSize
                            text: {
                                var revision = panel.taskRevision
                                return panel.selectedTask > 0 ? app.backgroundTaskOutput(panel.selectedTask) : "Selecione uma tarefa para acompanhar a saída."
                            }
                        }
                    }
                }
            }
            Loader {
                id: browserLoader
                objectName: "browserLoader"
                active: panel.browserStarted
                source: "BrowserPane.qml"
            }
            ColumnLayout {
                Label { Layout.fillWidth: true; text: "Terminal interativo · a sessão mantém seu diretório de trabalho"; color: theme.muted; elide: Text.ElideMiddle }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    CopyableTextArea { id: terminalOutput; objectName: "workspaceTerminalOutput"; text: panel.terminalText; readOnly: true; wrapMode: TextArea.NoWrap; font.family: "monospace"; font.pointSize: theme.codeFontSize }
                }
                RowLayout {
                    AppTextField { id: terminalInput; Layout.fillWidth: true; placeholderText: "Comando ou resposta ao processo…"; onAccepted: sendTerminal.clicked() }
                    AppButton { id: sendTerminal; text: "Enviar"; primary: true; onClicked: { app.runTerminalCommand(terminalInput.text); terminalInput.text = "" } }
                    AppButton { text: "Ctrl+C"; enabled: app.terminalRunning; onClicked: app.sendTerminalInput("\u0003") }
                    AppButton { text: "Encerrar"; danger: true; enabled: app.terminalRunning; onClicked: app.killTerminal() }
                    AppButton { text: "Limpar"; ghost: true; onClicked: { panel.terminalText = ""; panel.pendingTerminalText = "" } }
                }
            }
        }
    }
    Timer {
        id: terminalFlush
        interval: 33
        onTriggered: {
            panel.terminalText = (panel.terminalText + panel.pendingTerminalText).slice(-200000)
            panel.pendingTerminalText = ""
            terminalOutput.cursorPosition = terminalOutput.length
        }
    }
    Connections {
        target: app
        function onLoggedInChanged() {
            if (!app.loggedIn) {
                panel.browserStarted = false
                panel.terminalText = ""
                panel.pendingTerminalText = ""
                terminalFlush.stop()
                panel.selectedTask = 0
                panel.closeRequested()
            }
        }
        function onBackgroundTasksChanged() {
            panel.taskRevision++
        }
        function onTerminalOutput(text) {
            panel.pendingTerminalText = (panel.pendingTerminalText + text).slice(-200000)
            if (!terminalFlush.running) terminalFlush.start()
        }
    }
}
