import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: main
    objectName: "mainView"
    spacing: 0
    property int navigationWidth: app.sidebarWidth
    property bool navigationCollapsed: app.sidebarCollapsed
    // Side panel (file, browser, terminal, tasks) beside the conversation.
    property bool workspaceOpen: false
    property bool workspaceExpanded: false
    property bool openedForArtifact: false
    property real artifactWidth: 460
    readonly property bool artifactExpanded: workspaceExpanded
    readonly property real artifactMaximumWidth: Math.max(280,
        main.width - (navigation.visible ? navigation.width + 6 : 52) - 420)

    function openWorkspace(tab) {
        if (!workspaceOpen) openedForArtifact = false
        workspaceOpen = true
        workspace.selectTab(tab)
    }
    function toggleWorkspace() {
        if (workspaceOpen) {
            workspaceOpen = false
            workspaceExpanded = false
        } else {
            openWorkspace(workspace.currentTab)
        }
    }
    function toggleArtifactExpanded() {
        workspaceExpanded = !workspaceExpanded
    }
    function toggleNavigation() {
        navigationCollapsed = !navigationCollapsed
        app.saveNavigationLayout(navigationWidth, navigationCollapsed)
    }
    function openBrowserAddress() {
        openWorkspace(workspace.tabBrowser)
        Qt.callLater(function() { if (workspace.browser()) workspace.browser().focusAddress() })
    }
    function findAnywhere() {
        var browser = workspace.browser()
        if (workspaceOpen && workspace.currentTab === workspace.tabBrowser && browser) {
            browser.showFind()
            return
        }
        if (navigationCollapsed) toggleNavigation()
        navigation.focusSearch()
    }
    function conversationIds() {
        var ids = []
        var entries = app.sidebar
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].type === "conv") ids.push(entries[i].id)
        return ids
    }
    function cycleConversation(step) {
        var ids = conversationIds()
        if (ids.length === 0) return
        var index = ids.indexOf(app.currentConversationId)
        index = index < 0 ? 0 : (index + step + ids.length) % ids.length
        app.openConversation(ids[index])
    }

    component RailButton: AppButton {
        property string hint
        Layout.fillWidth: true
        ghost: true
        ToolTip.text: hint
        ToolTip.visible: hovered
        Accessible.name: hint
    }

    Rectangle {
        visible: main.navigationCollapsed && !main.workspaceExpanded
        Layout.preferredWidth: 52
        Layout.fillHeight: true
        color: theme.sidebar
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            RailButton { icon.source: "qrc:/resources/icons/list.svg"; hint: "Mostrar projetos e conversas (Ctrl+B)"; onClicked: main.toggleNavigation() }
            RailButton { icon.source: "qrc:/resources/icons/plus.svg"; hint: "Nova conversa (Ctrl+N)"; onClicked: app.newConversation(app.activeProjectId) }
            RailButton { icon.source: "qrc:/resources/icons/globe.svg"; hint: "Navegador (Ctrl+L)"; onClicked: main.openWorkspace(workspace.tabBrowser) }
            RailButton { icon.source: "qrc:/resources/icons/terminal.svg"; hint: "Terminal (Ctrl+`)"; onClicked: main.openWorkspace(workspace.tabTerminal) }
            RailButton { icon.source: "qrc:/resources/icons/tasks.svg"; hint: "Tarefas em segundo plano"; onClicked: main.openWorkspace(workspace.tabTasks) }
            Item { Layout.fillHeight: true }
            RailButton { icon.source: "qrc:/resources/icons/gear.svg"; hint: "Configurações (Ctrl+,)"; onClicked: navigation.openSettings() }
        }
    }

    Sidebar {
        id: navigation
        visible: !main.navigationCollapsed && !main.workspaceExpanded
        Layout.preferredWidth: Math.min(main.navigationWidth, Math.max(220, main.width * 0.38))
        Layout.fillHeight: true
        onCollapseRequested: main.toggleNavigation()
        onWorkspaceRequested: function(tab) { main.openWorkspace(tab) }
    }

    Rectangle {
        visible: !main.navigationCollapsed && !main.workspaceExpanded
        Layout.preferredWidth: 6
        Layout.fillHeight: true
        color: resizeMouse.containsMouse || resizeMouse.pressed ? theme.selected : theme.bg
        MouseArea {
            id: resizeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real startX: 0
            property real startWidth: 0
            onPressed: function(event) {
                startX = mapToItem(main, event.x, event.y).x
                startWidth = navigation.width
            }
            onPositionChanged: function(event) {
                if (pressed)
                    main.navigationWidth = Math.round(Math.max(220, Math.min(480,
                        startWidth + mapToItem(main, event.x, event.y).x - startX)))
            }
            onReleased: app.saveNavigationLayout(main.navigationWidth, false)
            onDoubleClicked: {
                main.navigationWidth = 284
                app.saveNavigationLayout(284, false)
            }
        }
    }

    StackLayout {
        objectName: "conversationPane"
        visible: !main.workspaceExpanded
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: app.viewMode
        ChatView {
            id: chat
            onWorkspaceRequested: function(tab) { main.openWorkspace(tab) }
        }
        ProjectView {}
    }

    Rectangle {
        objectName: "artifactResizeHandle"
        visible: main.workspaceOpen && !main.workspaceExpanded
        Layout.preferredWidth: 8
        Layout.fillHeight: true
        color: artifactResizeMouse.containsMouse || artifactResizeMouse.pressed ? theme.selected : theme.bg
        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: 40
            radius: 1
            color: artifactResizeMouse.containsMouse || artifactResizeMouse.pressed ? theme.accent : theme.border
        }
        MouseArea {
            id: artifactResizeMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real startX: 0
            property real startWidth: 0
            onPressed: function(event) {
                startX = mapToItem(main, event.x, event.y).x
                startWidth = artifactPanel.width
            }
            onPositionChanged: function(event) {
                if (pressed)
                    main.artifactWidth = Math.max(280, Math.min(main.artifactMaximumWidth,
                        startWidth + startX - mapToItem(main, event.x, event.y).x))
            }
            onDoubleClicked: main.toggleArtifactExpanded()
            ToolTip.visible: containsMouse && !pressed
            ToolTip.text: "Arraste para redimensionar · Clique duplo para expandir"
        }
    }

    Item {
        id: artifactPanel
        objectName: "artifactPanel"
        visible: main.workspaceOpen
        Layout.preferredWidth: main.workspaceExpanded ? main.width
            : Math.min(main.artifactWidth, main.artifactMaximumWidth)
        Layout.fillWidth: main.workspaceExpanded
        Layout.fillHeight: true
        WorkspaceTools {
            id: workspace
            anchors.fill: parent
            expanded: main.workspaceExpanded
            onCloseRequested: { main.workspaceOpen = false; main.workspaceExpanded = false }
            onExpandRequested: main.toggleArtifactExpanded()
        }
    }

    AppDialog {
        id: approvalDialog
        modal: true
        anchors.centerIn: parent
        width: 480
        font.pointSize: theme.fontSize
        title: approvalTitle
        closePolicy: Popup.NoAutoClose

        property string approvalTitle: ""
        property string approvalDetail: ""
        property string approvalDiff: ""

        ColumnLayout {
            width: parent.width
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: approvalDialog.approvalDetail
                color: theme.text
                wrapMode: Text.Wrap
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                visible: approvalDialog.approvalDiff.length > 0
                radius: 8
                color: theme.dark ? "#1e1e1e" : "#fafafa"
                border.color: theme.border
                clip: true

                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 8

                    CopyableTextArea {
                        text: approvalDialog.approvalDiff
                        readOnly: true
                        wrapMode: TextArea.NoWrap
                        font.family: "monospace"
                        font.pointSize: Math.max(9, theme.fontSize - 2)
                        color: theme.text
                    }
                }
            }

            AppCheckBox {
                id: alwaysCheck
                text: "Não perguntar novamente nesta conversa"
            }

            AppCheckBox {
                id: projectCheck
                text: "Sempre permitir este tipo de ação neste projeto"
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Negar"
                    danger: true
                    onClicked: app.resolveApprovalEx(false, alwaysCheck.checked,
                                                     projectCheck.checked)
                }

                AppButton {
                    text: "Permitir"
                    primary: true
                    onClicked: app.resolveApprovalEx(true, alwaysCheck.checked,
                                                     projectCheck.checked)
                }
            }
        }

        onOpened: {
            alwaysCheck.checked = false
            projectCheck.checked = false
        }
    }

    // Keyboard shortcuts (Ctrl maps to the same keys on Windows and Linux).
    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; onActivated: commandPalette.openAndFocus() }
    Shortcut { sequence: "Ctrl+N"; onActivated: app.newConversation(app.activeProjectId) }
    Shortcut { sequence: "Ctrl+B"; onActivated: main.toggleNavigation() }
    Shortcut { sequence: "Ctrl+J"; onActivated: main.toggleWorkspace() }
    Shortcut { sequence: "Ctrl+L"; onActivated: main.openBrowserAddress() }
    Shortcut { sequence: "Ctrl+F"; onActivated: main.findAnywhere() }
    Shortcut { sequence: "Ctrl+,"; onActivated: navigation.openSettings() }
    Shortcut { sequence: "Ctrl+`"; onActivated: main.openWorkspace(workspace.tabTerminal) }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: main.cycleConversation(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: main.cycleConversation(-1) }
    Shortcut { sequence: "Ctrl+Backtab"; onActivated: main.cycleConversation(-1) }

    AppDialog {
        id: commandPalette
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, main.width - 40)
        height: Math.min(500, main.height - 80)
        modal: true
        title: "Comandos"
        padding: 12

        function actions() {
            return [
                { title: "Nova conversa", hint: "Criar uma conversa no projeto atual", keys: "Ctrl+N",
                  run: function() { app.newConversation(app.activeProjectId) } },
                { title: "Novo projeto", hint: "Criar e organizar um novo projeto", keys: "",
                  run: function() { navigation.openNewProject() } },
                { title: "Buscar conversas", hint: "Pesquisar títulos e mensagens", keys: "Ctrl+F",
                  run: function() { main.findAnywhere() } },
                { title: "Mostrar ou ocultar a barra lateral", hint: "Projetos e conversas", keys: "Ctrl+B",
                  run: function() { main.toggleNavigation() } },
                { title: "Mostrar ou ocultar o painel lateral", hint: "Arquivo, navegador, terminal e tarefas", keys: "Ctrl+J",
                  run: function() { main.toggleWorkspace() } },
                { title: "Navegador", hint: "Abrir sites e prévias locais", keys: "Ctrl+L",
                  run: function() { main.openBrowserAddress() } },
                { title: "Abrir terminal", hint: "Executar comandos no projeto", keys: "Ctrl+`",
                  run: function() { main.openWorkspace(workspace.tabTerminal) } },
                { title: "Tarefas em segundo plano", hint: "Acompanhar comandos e conversas", keys: "",
                  run: function() { main.openWorkspace(workspace.tabTasks) } },
                { title: "Abrir configurações", hint: "Tema, modelo, áudio e preferências", keys: "Ctrl+,",
                  run: function() { navigation.openSettings() } },
                { title: "Plugins MCP", hint: "Servidores e ferramentas conectadas", keys: "",
                  run: function() { navigation.openPlugins() } },
                { title: "Automações", hint: "Tarefas agendadas", keys: "",
                  run: function() { navigation.openAutomations() } },
                { title: "Atividades", hint: "Ver as ações recentes", keys: "",
                  run: function() { navigation.openActivities() } },
                { title: "Exportar conversa", hint: "Salvar a conversa atual em Markdown", keys: "",
                  run: function() { app.exportCurrentConversation() } },
                { title: "Abrir pasta do projeto", hint: "Mostrar os arquivos do projeto", keys: "",
                  run: function() { app.openWorkdir() } },
                { title: "Atualizar custos", hint: "Consultar o uso atual da conversa", keys: "",
                  run: function() { app.refreshUsage(); app.refreshBalance() } },
                { title: "Próxima conversa", hint: "Ir para a conversa seguinte da lista", keys: "Ctrl+Tab",
                  run: function() { main.cycleConversation(1) } }
            ]
        }

        function filteredActions() {
            var query = commandSearch.text.trim().toLowerCase()
            var result = actions().filter(function(action) {
                return query.length === 0 || action.title.toLowerCase().indexOf(query) >= 0
                    || action.hint.toLowerCase().indexOf(query) >= 0
            })
            if (query.length > 0) {
                var entries = app.sidebar
                var found = 0
                for (var i = 0; i < entries.length && found < 8; ++i) {
                    var entry = entries[i]
                    if (entry.type !== "conv" || !entry.title || entry.title.toLowerCase().indexOf(query) < 0)
                        continue
                    found++
                    result.push({ title: entry.title, hint: "Ir para a conversa", keys: "",
                                  run: (function(id) { return function() { app.openConversation(id) } })(entry.id) })
                }
            }
            return result
        }

        function trigger(action) {
            commandPalette.close()
            action.run()
        }

        function openAndFocus() {
            commandSearch.text = ""
            commandList.model = filteredActions()
            commandList.currentIndex = 0
            open()
            commandSearch.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            AppTextField {
                id: commandSearch
                Layout.fillWidth: true
                placeholderText: "Digite um comando ou o nome de uma conversa…"
                onTextChanged: {
                    commandList.model = commandPalette.filteredActions()
                    commandList.currentIndex = 0
                }
                onAccepted: {
                    var choices = commandList.model
                    if (choices.length > 0)
                        commandPalette.trigger(choices[Math.max(0, commandList.currentIndex)])
                }
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Down) {
                        commandList.currentIndex = Math.min(commandList.count - 1, commandList.currentIndex + 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        commandList.currentIndex = Math.max(0, commandList.currentIndex - 1)
                        event.accepted = true
                    }
                }
            }

            ListView {
                id: commandList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: []
                highlightMoveDuration: 0
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    width: commandList.width
                    height: commandRow.implicitHeight + 14
                    radius: 8
                    color: index === commandList.currentIndex ? theme.selected
                         : (commandHover.hovered ? theme.sidebar : "transparent")
                    HoverHandler { id: commandHover }
                    TapHandler { onTapped: commandPalette.trigger(modelData) }
                    RowLayout {
                        id: commandRow
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { Layout.fillWidth: true; text: modelData.title; color: theme.text; elide: Text.ElideRight }
                            Text { Layout.fillWidth: true; text: modelData.hint; color: theme.muted; font.pointSize: Math.max(9, theme.fontSize - 2); elide: Text.ElideRight }
                        }
                        Text { visible: modelData.keys.length > 0; text: modelData.keys; color: theme.muted; font.pointSize: Math.max(9, theme.fontSize - 2) }
                    }
                }
            }
        }
    }

    Connections {
        target: app

        function onApprovalRequested(id, title, detail, diff) {
            approvalDialog.approvalTitle = title
            approvalDialog.approvalDetail = detail
            approvalDialog.approvalDiff = diff
            approvalDialog.open()
        }

        function onApprovalClosed() {
            approvalDialog.close()
        }

        function onArtifactChanged() {
            if (app.artifactVisible) {
                workspace.setArtifactText(app.artifactContent)
                if (!main.workspaceOpen) {
                    main.workspaceOpen = true
                    main.openedForArtifact = true
                }
                workspace.selectTab(workspace.tabFile)
            } else if (main.openedForArtifact) {
                main.workspaceOpen = false
                main.workspaceExpanded = false
                main.openedForArtifact = false
            } else if (workspace.currentTab === workspace.tabFile) {
                workspace.selectTab(workspace.tabTasks)
            }
        }

        function onAgentBrowserRequest(id, request) {
            if (!main.workspaceOpen) main.openedForArtifact = false
            main.workspaceOpen = true
            workspace.agentRequest(id, request)
        }

        function onBrowserRequested(url) {
            if (!main.workspaceOpen) main.openedForArtifact = false
            main.workspaceOpen = true
            workspace.openUrl(url)
        }
    }
}
