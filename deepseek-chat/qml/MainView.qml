import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: main
    spacing: 0
    property int navigationWidth: app.sidebarWidth
    property bool navigationCollapsed: app.sidebarCollapsed

    function toggleNavigation() {
        navigationCollapsed = !navigationCollapsed
        app.saveNavigationLayout(navigationWidth, navigationCollapsed)
    }

    Rectangle {
        visible: main.navigationCollapsed
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
        visible: !main.navigationCollapsed
        Layout.preferredWidth: Math.min(main.navigationWidth, Math.max(220, main.width * 0.38))
        Layout.fillHeight: true
        onCollapseRequested: main.toggleNavigation()
    }

    Rectangle {
        visible: !main.navigationCollapsed
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
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: app.viewMode

        ChatView {}
        ProjectView {}
    }

    Rectangle {
        Layout.preferredWidth: app.artifactVisible ? Math.min(460, main.width * 0.4) : 0
        Layout.fillHeight: true
        visible: app.artifactVisible
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
                    text: "Copiar"
                    ghost: true
                    onClicked: app.copyToClipboard(artifactArea.text)
                }

                AppButton {
                    text: "Abrir"
                    icon.source: "qrc:/resources/icons/open.svg"
                    ghost: true
                    onClicked: app.openPath(app.artifactPath)
                }

                AppButton {
                    text: "Salvar"
                    icon.source: "qrc:/resources/icons/save.svg"
                    primary: true
                    onClicked: app.saveArtifact(artifactArea.text)
                }

                AppButton {
                    text: "Fechar"
                    icon.source: "qrc:/resources/icons/close.svg"
                    ghost: true
                    onClicked: app.closeArtifact()
                }
            }

            TabBar {
                id: artifactTabs
                Layout.fillWidth: true
                TabButton { text: "Editar" }
                TabButton { text: "Prévia" }
                TabButton { text: "Alterações" }
                TabButton { text: "Versões" }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: artifactTabs.currentIndex

                ScrollView {
                    CopyableTextArea {
                        id: artifactArea
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

    Dialog {
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

            CheckBox {
                id: alwaysCheck
                text: "Não perguntar novamente nesta conversa"
            }

            CheckBox {
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

    Dialog {
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

            TextField {
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
        }
    }
}
