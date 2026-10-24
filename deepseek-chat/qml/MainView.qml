import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: main
    objectName: "mainView"
    spacing: 0
    property int navigationWidth: app.sidebarWidth
    property bool navigationCollapsed: app.sidebarCollapsed
    property bool workspaceOpen: false
    property bool workspaceExpanded: false
    property real workspaceHeight: 360
    function openWorkspace(tab) {
        artifactExpanded = false
        if (app.artifactVisible) workspaceExpanded = true
        workspaceOpen = true
        workspace.selectTab(tab)
    }
    property real artifactWidth: 460
    property bool artifactExpanded: false
    readonly property real artifactMaximumWidth: Math.max(280,
        main.width - (navigation.visible ? navigation.width + 6 : 52) - 288)

    function toggleArtifactExpanded() {
        artifactExpanded = !artifactExpanded
    }

    function toggleNavigation() {
        navigationCollapsed = !navigationCollapsed
        app.saveNavigationLayout(navigationWidth, navigationCollapsed)
    }

    Rectangle {
        visible: main.navigationCollapsed && !main.artifactExpanded
        Layout.preferredWidth: 52
        Layout.fillHeight: true
        color: theme.sidebar
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            AppButton {
                text: "☰"
                Layout.fillWidth: true
                ghost: true
                ToolTip.text: "Mostrar projetos e conversas"
                ToolTip.visible: hovered
                onClicked: main.toggleNavigation()
            }
            AppButton {
                text: "+"
                Layout.fillWidth: true
                ghost: true
                ToolTip.text: "Nova conversa"
                ToolTip.visible: hovered
                onClicked: app.newConversation(app.activeProjectId)
            }
            Item { Layout.fillHeight: true }
            AppButton {
                icon.source: "qrc:/resources/icons/gear.svg"
                Layout.fillWidth: true
                ghost: true
                ToolTip.text: "Configurações"
                ToolTip.visible: hovered
                onClicked: navigation.openSettings()
            }
        }
    }

    Sidebar {
        id: navigation
        visible: !main.navigationCollapsed && !main.artifactExpanded
        Layout.preferredWidth: Math.min(main.navigationWidth, Math.max(220, main.width * 0.38))
        Layout.fillHeight: true
        onCollapseRequested: main.toggleNavigation()
        onWorkspaceRequested: function(tab) { main.openWorkspace(tab) }
    }

    Rectangle {
        visible: !main.navigationCollapsed && !main.artifactExpanded
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

    ColumnLayout {
        objectName: "conversationPane"
        visible: !main.artifactExpanded
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: 0
        StackLayout {
            visible: !main.workspaceOpen || !main.workspaceExpanded
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: app.viewMode
            ChatView {}
            ProjectView {}
        }
        Rectangle {
            visible: main.workspaceOpen && !main.workspaceExpanded
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            color: theme.border
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeVerCursor
                property real startY: 0
                property real startHeight: 0
                onPressed: function(event) { startY = mapToItem(main, event.x, event.y).y; startHeight = workspace.height }
                onPositionChanged: function(event) {
                    if (pressed) main.workspaceHeight = Math.max(240, Math.min(main.height - 240,
                        startHeight + startY - mapToItem(main, event.x, event.y).y))
                }
            }
        }
        WorkspaceTools {
            id: workspace
            visible: main.workspaceOpen
            Layout.fillWidth: true
            Layout.fillHeight: main.workspaceExpanded
            Layout.preferredHeight: Math.min(main.workspaceHeight, main.height * 0.55)
            expanded: main.workspaceExpanded
            onCloseRequested: main.workspaceOpen = false
            onExpandRequested: main.workspaceExpanded = !main.workspaceExpanded
        }
    }

    Rectangle {
        objectName: "artifactResizeHandle"
        visible: app.artifactVisible && !main.artifactExpanded && !(main.workspaceOpen && main.workspaceExpanded)
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

    Rectangle {
        id: artifactPanel
        objectName: "artifactPanel"
        Layout.preferredWidth: main.artifactExpanded ? main.width
            : Math.min(main.artifactWidth, main.artifactMaximumWidth)
        Layout.fillWidth: main.artifactExpanded
        Layout.fillHeight: true
        visible: app.artifactVisible && !(main.workspaceOpen && main.workspaceExpanded)
        color: theme.bg

        Rectangle {
            width: 1
            height: parent.height
            anchors.left: parent.left
            color: theme.border
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: app.artifactPath
                    color: theme.text
                    font.bold: true
                    elide: Text.ElideMiddle
                }

                AppButton {
                    objectName: "expandArtifactButton"
                    text: main.artifactExpanded ? "Reduzir" : "Expandir"
                    ghost: true
                    ToolTip.visible: hovered
                    ToolTip.text: main.artifactExpanded ? "Voltar ao painel lateral" : "Usar toda a janela para ver o arquivo"
                    onClicked: main.toggleArtifactExpanded()
                }

                AppButton {
                    icon.source: "qrc:/resources/icons/close.svg"
                    ghost: true
                    ToolTip.text: "Fechar arquivo"
                    ToolTip.visible: hovered
                    onClicked: app.closeArtifact()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                AppButton {
                    text: artifactPanel.width < 420 ? "" : "Copiar"
                    ToolTip.text: "Copiar arquivo"
                    ToolTip.visible: hovered
                    icon.source: "qrc:/resources/icons/copy.svg"
                    ghost: true
                    onClicked: app.copyToClipboard(artifactArea.text)
                }

                AppButton {
                    text: artifactPanel.width < 420 ? "" : "Abrir"
                    ToolTip.text: "Abrir no aplicativo padrão"
                    ToolTip.visible: hovered
                    icon.source: "qrc:/resources/icons/open.svg"
                    ghost: true
                    onClicked: app.openPath(app.artifactPath)
                }

                AppButton {
                    text: artifactPanel.width < 420 ? "" : "Salvar"
                    ToolTip.text: "Salvar arquivo"
                    ToolTip.visible: hovered
                    icon.source: "qrc:/resources/icons/save.svg"
                    primary: true
                    onClicked: app.saveArtifact(artifactArea.text)
                }

                Item { Layout.fillWidth: true }
            }

            AppTabBar {
                id: artifactTabs
                Layout.fillWidth: true
                AppTabButton { text: "Editar" }
                AppTabButton { text: "Prévia" }
                AppTabButton { text: "Alterações" }
                AppTabButton { text: "Versões" }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: artifactTabs.currentIndex

                ScrollView {
                    CopyableTextArea {
                        id: artifactArea
                        objectName: "artifactEditor"
                        wrapMode: TextArea.NoWrap
                        font.family: "Consolas"
                        font.pointSize: theme.codeFontSize
                        color: theme.text
                    }
                }

                Item {
                    id: previewPane
                    readonly property bool svg: /\.svg$/i.test(app.artifactPath)
                    readonly property bool markdown: /\.(md|markdown)$/i.test(app.artifactPath)
                    readonly property bool html: /\.(html|htm)$/i.test(app.artifactPath)

                    Image {
                        anchors.fill: parent
                        anchors.margins: 10
                        visible: parent.svg
                        source: parent.svg && artifactTabs.currentIndex === 1
                                ? app.artifactPreviewUrl(artifactArea.text) : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }

                    ScrollView {
                        anchors.fill: parent
                        visible: !parent.svg
                        CopyableTextEdit {
                            width: parent.width
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            textFormat: previewPane.markdown ? TextEdit.MarkdownText
                                        : (previewPane.html ? TextEdit.RichText
                                           : TextEdit.PlainText)
                            text: artifactTabs.currentIndex === 1 ? artifactArea.text : ""
                            color: theme.text
                            font.pointSize: theme.chatFontSize
                            font.family: theme.chatFontFamily
                            onLinkActivated: function(link) { app.openExternal(link) }
                        }
                    }
                }

                ScrollView {
                    CopyableTextArea {
                        text: artifactTabs.currentIndex === 2
                              ? app.artifactDiff(artifactArea.text) : ""
                        readOnly: true
                        wrapMode: TextArea.NoWrap
                        font.family: "Consolas"
                        font.pointSize: theme.codeFontSize
                        color: theme.text
                    }
                }

                ScrollView {
                    ColumnLayout {
                        width: parent.width
                        spacing: 8
                        Text {
                            text: app.artifactVersions.length > 0
                                  ? "Versões anteriores deste arquivo" : "Nenhuma versão anterior."
                            color: theme.muted
                        }
                        Repeater {
                            model: app.artifactVersions
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text {
                                    text: modelData.label
                                    color: theme.text
                                    Layout.fillWidth: true
                                }
                                AppButton {
                                    text: "Restaurar"
                                    onClicked: app.restoreArtifactVersion(modelData.path)
                                }
                            }
                        }
                    }
                }
            }
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

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        onActivated: commandPalette.openAndFocus()
    }

    AppDialog {
        id: commandPalette
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, main.width - 40)
        height: Math.min(460, main.height - 80)
        modal: true
        title: "Comandos"
        padding: 12

        function actions() {
            return [
                { title: "Nova conversa", hint: "Criar uma conversa no projeto atual",
                  run: function() { app.newConversation(app.activeProjectId) } },
                { title: "Novo projeto", hint: "Criar e organizar um novo projeto",
                  run: function() { navigation.openNewProject() } },
                { title: "Abrir configurações", hint: "Tema, modelo, áudio e preferências",
                  run: function() { navigation.openSettings() } },
                { title: "Tarefas em segundo plano", hint: "Acompanhar comandos e conversas",
                  run: function() { main.openWorkspace(0) } },
                { title: "Navegador", hint: "Abrir sites e prévias locais",
                  run: function() { main.openWorkspace(1) } },
                { title: "Abrir terminal", hint: "Executar comandos no projeto",
                  run: function() { navigation.openTerminal() } },
                { title: "Atividades", hint: "Ver as ações recentes",
                  run: function() { navigation.openActivities() } },
                { title: "Exportar conversa", hint: "Salvar a conversa atual em Markdown",
                  run: function() { app.exportCurrentConversation() } },
                { title: "Abrir pasta do projeto", hint: "Mostrar os arquivos do projeto",
                  run: function() { app.openWorkdir() } },
                { title: "Atualizar custos", hint: "Consultar o uso atual da conversa",
                  run: function() { app.refreshUsage(); app.refreshBalance() } }
            ]
        }

        function filteredActions() {
            var query = commandSearch.text.trim().toLowerCase()
            return actions().filter(function(action) {
                return query.length === 0 || action.title.toLowerCase().indexOf(query) >= 0
                    || action.hint.toLowerCase().indexOf(query) >= 0
            })
        }

        function trigger(action) {
            commandPalette.close()
            action.run()
        }

        function openAndFocus() {
            commandSearch.text = ""
            open()
            commandSearch.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            AppTextField {
                id: commandSearch
                Layout.fillWidth: true
                placeholderText: "Digite um comando…"
                onTextChanged: commandList.model = commandPalette.filteredActions()
                onAccepted: {
                    var choices = commandPalette.filteredActions()
                    if (choices.length > 0)
                        commandPalette.trigger(choices[0])
                }
            }

            ListView {
                id: commandList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: commandPalette.filteredActions()

                delegate: AppButton {
                    required property var modelData
                    width: commandList.width
                    text: modelData.title + "\n" + modelData.hint
                    ghost: true
                    onClicked: commandPalette.trigger(modelData)
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
            if (app.artifactVisible)
                artifactArea.text = app.artifactContent
            else
                main.artifactExpanded = false
        }
    }
}
