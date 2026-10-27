import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Side panel shared by the opened file, the browser, the terminal and the
// background tasks, so each of them can stay beside the conversation.
Rectangle {
    id: panel
    objectName: "workspaceTools"
    color: theme.bg
    property bool expanded: false
    property int currentTab: 0
    property int selectedTask: 0
    property bool browserStarted: false
    property string pendingUrl: ""
    property string pendingTerminalText: ""
    property int taskRevision: 0
    property bool followTask: true
    property var terminalHistory: []
    property int terminalHistoryIndex: -1
    readonly property alias terminalText: terminalOutput.text
    readonly property alias artifactText: artifactView.text
    readonly property bool compact: width < 560
    readonly property int tabTasks: 0
    readonly property int tabBrowser: 1
    readonly property int tabTerminal: 2
    readonly property int tabFile: 3
    signal closeRequested()
    signal expandRequested()

    function selectTab(tab) {
        if (tab === tabFile && !app.artifactVisible) tab = tabTasks
        currentTab = tab
        if (tab === tabBrowser) browserStarted = true
        if (tab === tabTerminal) terminalInput.forceActiveFocus()
    }
    function openUrl(url) {
        selectTab(tabBrowser)
        if (browserLoader.item) browserLoader.item.openUrl(url)
        else pendingUrl = url
    }
    function browser() { return browserLoader.item }
    function setArtifactText(text) { artifactView.text = text }
    function appendTerminal(text) {
        pendingTerminalText += text
        if (!terminalFlush.running) terminalFlush.start()
    }

    Rectangle {
        width: 1
        height: parent.height
        anchors.left: parent.left
        color: theme.border
        visible: !panel.expanded
    }

    component PanelTab: AppButton {
        required property int tab
        property string label
        visible: tab !== panel.tabFile || app.artifactVisible
        ghost: panel.currentTab !== tab
        text: panel.compact ? "" : label
        font.bold: panel.currentTab === tab
        ToolTip.text: label
        ToolTip.visible: hovered && panel.compact
        Accessible.name: label
        background: Rectangle {
            radius: 9
            color: panel.currentTab === tab ? theme.selected : (parent.hovered ? theme.sidebar : "transparent")
        }
        onClicked: panel.selectTab(tab)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            PanelTab { tab: panel.tabFile; label: "Arquivo"; icon.source: "qrc:/resources/icons/file.svg" }
            PanelTab { tab: panel.tabBrowser; label: "Navegador"; icon.source: "qrc:/resources/icons/globe.svg" }
            PanelTab { tab: panel.tabTerminal; label: "Terminal"; icon.source: "qrc:/resources/icons/terminal.svg" }
            PanelTab {
                tab: panel.tabTasks
                label: "Tarefas" + (runningCount > 0 ? " (" + runningCount + ")" : "")
                icon.source: "qrc:/resources/icons/tasks.svg"
                readonly property int runningCount: {
                    var revision = panel.taskRevision
                    var count = app.activeRuns.length
                    var tasks = app.backgroundTasks
                    for (var i = 0; i < tasks.length; ++i) if (tasks[i].running) ++count
                    return count
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "expandArtifactButton"
                ghost: true
                text: panel.compact ? "" : (panel.expanded ? "Reduzir" : "Expandir")
                icon.source: "qrc:/resources/icons/panel.svg"
                ToolTip.visible: hovered
                ToolTip.text: panel.expanded ? "Voltar a mostrar a conversa" : "Usar toda a janela"
                onClicked: panel.expandRequested()
            }
            AppButton {
                ghost: true
                icon.source: "qrc:/resources/icons/close.svg"
                ToolTip.text: "Recolher painel (Ctrl+J) · tarefas e terminal continuam"
                ToolTip.visible: hovered
                onClicked: panel.closeRequested()
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.currentTab

            // Tarefas em segundo plano
            ColumnLayout {
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    AppTextField {
                        id: taskCommand
                        Layout.fillWidth: true
                        placeholderText: "Comando em segundo plano, ex.: npm run dev"
                        onAccepted: startTask.clicked()
                    }
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
                    AppButton { text: panel.compact ? "" : "Limpar concluídas"; icon.source: "qrc:/resources/icons/trash.svg"; ghost: true; ToolTip.text: "Limpar concluídas"; ToolTip.visible: hovered; onClicked: { app.clearFinishedTasks(); panel.selectedTask = 0 } }
                }
                Label {
                    text: "Pasta: " + (app.activeProjectWorkdir || "pasta pessoal") + " · continuam com o aplicativo aberto"
                    color: theme.muted
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                }
                SplitView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: panel.compact ? Qt.Vertical : Qt.Horizontal
                    ScrollView {
                        id: taskListScroll
                        SplitView.preferredWidth: 300
                        SplitView.preferredHeight: 180
                        SplitView.minimumWidth: 150
                        SplitView.minimumHeight: 80
                        ColumnLayout {
                            width: taskListScroll.availableWidth
                            spacing: 4
                            Repeater {
                                model: app.activeRuns
                                delegate: RowLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label { Layout.fillWidth: true; text: "Chat · " + modelData.title + " · etapa " + modelData.round; color: theme.accent; elide: Text.ElideRight }
                                    AppButton { text: "Parar"; danger: true; ghost: true; onClicked: app.stopConversationRun(modelData.conversationId) }
                                }
                            }
                            Repeater {
                                model: app.backgroundTasks
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: taskRow.implicitHeight + 12
                                    color: panel.selectedTask === modelData.id ? theme.selected : (taskHover.hovered ? theme.sidebar : "transparent")
                                    radius: 8
                                    HoverHandler { id: taskHover }
                                    TapHandler { onTapped: panel.selectedTask = modelData.id }
                                    ToolTip.visible: taskHover.hovered
                                    ToolTip.text: modelData.workdir + "\n" + modelData.command
                                    RowLayout {
                                        id: taskRow
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 4
                                        Rectangle {
                                            width: 8; height: 8; radius: 4
                                            color: modelData.running ? theme.accent
                                                 : (modelData.status === "Falhou" ? theme.danger : theme.muted)
                                        }
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            Label { Layout.fillWidth: true; text: "#" + modelData.id + " · " + modelData.status + (modelData.running || modelData.exitCode < 0 ? "" : " (" + modelData.exitCode + ")"); color: modelData.status === "Falhou" ? theme.danger : theme.text; elide: Text.ElideRight }
                                            Label { Layout.fillWidth: true; text: modelData.command; color: theme.muted; elide: Text.ElideRight }
                                        }
                                        AppButton {
                                            visible: modelData.previewUrl.length > 0
                                            ghost: true
                                            icon.source: "qrc:/resources/icons/globe.svg"
                                            ToolTip.text: "Abrir prévia " + modelData.previewUrl
                                            ToolTip.visible: hovered
                                            onClicked: panel.openUrl(modelData.previewUrl)
                                        }
                                        AppButton {
                                            ghost: true
                                            icon.source: "qrc:/resources/icons/refresh.svg"
                                            ToolTip.text: "Reiniciar"
                                            ToolTip.visible: hovered
                                            onClicked: {
                                                var id = app.restartBackgroundTask(modelData.id)
                                                if (id > 0) panel.selectedTask = id
                                            }
                                        }
                                        AppButton { visible: modelData.running; text: "Parar"; danger: true; ghost: true; onClicked: app.stopBackgroundTask(modelData.id) }
                                    }
                                }
                            }
                            Label {
                                visible: app.backgroundTasks.length === 0 && app.activeRuns.length === 0
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: "Nenhuma tarefa. Rode servidores de desenvolvimento, builds ou testes aqui; a URL local aparece como prévia."
                                color: theme.muted
                            }
                        }
                    }
                    ColumnLayout {
                        SplitView.fillWidth: true
                        SplitView.fillHeight: true
                        spacing: 4
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            CopyableTextArea {
                                id: taskOutput
                                objectName: "backgroundTaskOutput"
                                readOnly: true
                                wrapMode: TextArea.NoWrap
                                font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
                                font.pointSize: theme.codeFontSize
                                text: {
                                    var revision = panel.taskRevision
                                    return panel.selectedTask > 0 ? app.backgroundTaskOutput(panel.selectedTask) : "Selecione uma tarefa para acompanhar a saída."
                                }
                                onTextChanged: if (panel.followTask) cursorPosition = length
                            }
                        }
                        AppCheckBox {
                            text: "Acompanhar o fim da saída"
                            checked: panel.followTask
                            onToggled: panel.followTask = checked
                        }
                    }
                }
            }

            Loader {
                id: browserLoader
                objectName: "browserLoader"
                active: panel.browserStarted
                source: "BrowserPane.qml"
                onLoaded: {
                    if (panel.pendingUrl.length > 0) {
                        item.openUrl(panel.pendingUrl)
                        panel.pendingUrl = ""
                    }
                }
            }

            // Terminal
            ColumnLayout {
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    text: "Terminal " + (Qt.platform.os === "windows" ? "cmd.exe" : "bash") + " · pasta: " + (app.activeProjectWorkdir || "pasta pessoal") + " · ↑/↓ histórico · Ctrl+C interrompe"
                    color: theme.muted
                    elide: Text.ElideMiddle
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    CopyableTextArea {
                        id: terminalOutput
                        objectName: "workspaceTerminalOutput"
                        readOnly: true
                        wrapMode: TextArea.WrapAnywhere
                        font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
                        font.pointSize: theme.codeFontSize
                    }
                }
                RowLayout {
                    AppTextField {
                        id: terminalInput
                        objectName: "workspaceTerminalInput"
                        Layout.fillWidth: true
                        font.family: Qt.platform.os === "windows" ? "Consolas" : "monospace"
                        placeholderText: app.terminalRunning ? "Comando ou resposta ao processo…" : "Digite um comando para iniciar o terminal…"
                        onAccepted: sendTerminal.clicked()
                        Keys.onPressed: (event) => {
                            var history = panel.terminalHistory
                            if (event.key === Qt.Key_Up && history.length > 0) {
                                panel.terminalHistoryIndex = panel.terminalHistoryIndex < 0
                                    ? history.length - 1 : Math.max(0, panel.terminalHistoryIndex - 1)
                                text = history[panel.terminalHistoryIndex]
                                event.accepted = true
                            } else if (event.key === Qt.Key_Down && panel.terminalHistoryIndex >= 0) {
                                panel.terminalHistoryIndex++
                                if (panel.terminalHistoryIndex >= history.length) {
                                    panel.terminalHistoryIndex = -1
                                    text = ""
                                } else {
                                    text = history[panel.terminalHistoryIndex]
                                }
                                event.accepted = true
                            } else if (event.key === Qt.Key_C && (event.modifiers & Qt.ControlModifier)
                                       && selectedText.length === 0 && app.terminalRunning) {
                                app.interruptTerminal()
                                event.accepted = true
                            }
                        }
                    }
                    AppButton {
                        id: sendTerminal
                        text: "Enviar"
                        primary: true
                        onClicked: {
                            var command = terminalInput.text
                            if (command.trim().length > 0) {
                                var history = panel.terminalHistory.filter(function(item) { return item !== command })
                                history.push(command)
                                panel.terminalHistory = history.slice(-100)
                            }
                            panel.terminalHistoryIndex = -1
                            app.runTerminalCommand(command)
                            terminalInput.text = ""
                        }
                    }
                    AppButton { text: "Ctrl+C"; ghost: true; enabled: app.terminalRunning; onClicked: app.interruptTerminal() }
                    AppButton { text: panel.compact ? "" : "Encerrar"; icon.source: "qrc:/resources/icons/stop.svg"; danger: true; ghost: true; enabled: app.terminalRunning; ToolTip.text: "Encerrar o terminal"; ToolTip.visible: hovered; onClicked: app.killTerminal() }
                    AppButton { text: panel.compact ? "" : "Limpar"; icon.source: "qrc:/resources/icons/trash.svg"; ghost: true; ToolTip.text: "Limpar a tela"; ToolTip.visible: hovered; onClicked: { terminalOutput.clear(); panel.pendingTerminalText = "" } }
                }
            }

            ArtifactView { id: artifactView }
        }
    }

    Timer {
        id: terminalFlush
        interval: 33
        onTriggered: {
            // Append incrementally instead of rebuilding the whole transcript.
            terminalOutput.insert(terminalOutput.length, panel.pendingTerminalText)
            panel.pendingTerminalText = ""
            if (terminalOutput.length > 200000)
                terminalOutput.remove(0, terminalOutput.length - 150000)
            terminalOutput.cursorPosition = terminalOutput.length
        }
    }
    Connections {
        target: app
        function onLoggedInChanged() {
            if (!app.loggedIn) {
                panel.browserStarted = false
                terminalOutput.clear()
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
            panel.appendTerminal(text)
        }
    }
}
