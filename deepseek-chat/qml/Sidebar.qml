import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl

Rectangle {
    id: sidebar
    color: theme.sidebar
    signal workspaceRequested(int tab)
    signal collapseRequested()
    property var collapsedProjects: ({})

    function openSettings() { settingsDialog.open() }
    function openNewProject() { newProjectDialog.open() }
    function openTerminal() { workspaceRequested(2) }
    function openActivities() { app.refreshActivities(); activitiesDialog.open() }
    function openPlugins() { settingsDialog.openPlugins() }
    function openAutomations() { automationsDialog.open() }
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    component ToolIcon: AppButton {
        property string hint
        Layout.fillWidth: true
        implicitWidth: 34
        leftPadding: 4
        rightPadding: 4
        ghost: true
        ToolTip.text: hint
        ToolTip.visible: hovered
        Accessible.name: hint
    }
    readonly property bool hasConversations: {
        var entries = app.sidebar
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].type === "conv" || entries[i].type === "project") return true
        return false
    }
    function toggleProject(id) {
        var folded = Object.assign({}, collapsedProjects)
        folded[id] = !folded[id]
        collapsedProjects = folded
    }

    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: theme.border
    }

    property var filteredModel: {
        var term = search.text.toLowerCase()
        var out = []
        var entries = app.sidebar
        if (term.length >= 2) {
            var hits = app.searchResults
            if (hits.length > 0) {
                out.push({type: "header", title: "Mensagens", indent: 0, level: 0})
                for (var i = 0; i < hits.length; ++i) {
                    out.push({type: "search", id: hits[i].conversationId,
                              title: hits[i].title, snippet: hits[i].snippet,
                              indent: 0})
                }
            }
        }
        if (term.length === 0) {
            var project = 0
            for (var row = 0; row < entries.length; ++row) {
                var entry = entries[row]
                if (entry.type === "project") project = entry.id
                if (entry.type === "header") project = 0
                if (entry.type === "conv" && project && collapsedProjects[project]) continue
                out.push(entry)
            }
            return out
        }
        for (var j = 0; j < entries.length; ++j) {
            var item = entries[j]
            if (item.type !== "header" && item.title
                    && item.title.toLowerCase().indexOf(term) >= 0)
                out.push(item)
        }
        return out
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            spacing: 8

            Image {
                source: "qrc:/resources/deepseek-32.png"
                sourceSize.width: 26
                sourceSize.height: 26
            }

            Text {
                text: "DeepSeek Chat"
                font.bold: true
                font.pointSize: theme.sidebarFontSize + 1
                font.family: theme.fontFamily
                color: theme.text
                Layout.fillWidth: true
            }
            AppButton {
                text: "‹"
                ghost: true
                implicitWidth: 28
                leftPadding: 4
                rightPadding: 4
                font.pointSize: theme.sidebarFontSize + 3
                ToolTip.text: "Recolher projetos e conversas (Ctrl+B)"
                ToolTip.visible: hovered
                onClicked: sidebar.collapseRequested()
            }
        }

        AppButton {
            text: "Nova conversa"
            icon.source: "qrc:/resources/icons/plus.svg"
            Layout.fillWidth: true
            font.pointSize: theme.sidebarFontSize
            ToolTip.text: "Ctrl+N"
            ToolTip.visible: hovered
            onClicked: app.newConversation(app.activeProjectId)
        }

        // Tools stay one click away without pushing the conversation list down.
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            ToolIcon { icon.source: "qrc:/resources/icons/folder.svg"; hint: "Novo projeto"; onClicked: newProjectDialog.open() }
            ToolIcon { icon.source: "qrc:/resources/icons/globe.svg"; hint: "Navegador (Ctrl+L)"; onClicked: sidebar.workspaceRequested(1) }
            ToolIcon { icon.source: "qrc:/resources/icons/terminal.svg"; hint: "Terminal (Ctrl+`)"; onClicked: sidebar.openTerminal() }
            ToolIcon { icon.source: "qrc:/resources/icons/tasks.svg"; hint: "Tarefas em segundo plano"; onClicked: sidebar.workspaceRequested(0) }
            ToolIcon { icon.source: "qrc:/resources/icons/clock.svg"; hint: "Automações"; onClicked: sidebar.openAutomations() }
            ToolIcon { icon.source: "qrc:/resources/icons/activity.svg"; hint: "Atividades"; onClicked: sidebar.openActivities() }
            ToolIcon { icon.source: "qrc:/resources/icons/plugin.svg"; hint: "Plugins MCP"; onClicked: sidebar.openPlugins() }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 3
            Layout.bottomMargin: 3
            implicitHeight: 1
            color: theme.border
        }

        AppTextField {
            id: search
            placeholderText: "Pesquisar… (Ctrl+F)"
            Layout.fillWidth: true
            font.pointSize: theme.sidebarFontSize
            font.family: theme.fontFamily
            leftPadding: 36
            implicitHeight: Math.max(40, contentHeight + 18)
            background: Rectangle {
                radius: 11
                color: "transparent"
                border.color: search.activeFocus ? theme.accent : theme.border
            }

            IconImage {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                source: "qrc:/resources/icons/search.svg"
                color: theme.muted
            }

            Keys.onEscapePressed: {
                text = ""
                focus = false
            }

            onTextChanged: {
                if (text.trim().length >= 2) {
                    searchTimer.restart()
                } else {
                    searchTimer.stop()
                    app.clearSearch()
                }
            }
        }

        Timer {
            id: searchTimer
            interval: 450
            onTriggered: app.searchMessages(search.text)
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: sidebar.filteredModel
            spacing: 1
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            footer: Text {
                width: list.width
                visible: search.text.length > 0 ? list.count === 0 : !sidebar.hasConversations
                height: visible ? implicitHeight + 24 : 0
                topPadding: 12
                leftPadding: 12
                rightPadding: 12
                wrapMode: Text.Wrap
                color: theme.muted
                font.pointSize: Math.max(9, theme.sidebarFontSize - 1)
                font.family: theme.fontFamily
                text: search.text.length > 0
                      ? (search.text.trim().length < 2 ? "Digite ao menos 2 letras." : "Nada encontrado para \"" + search.text + "\".")
                      : "Nenhuma conversa ainda. Use \"Nova conversa\" ou Ctrl+N para começar."
            }

            delegate: Rectangle {
                id: itemRect
                required property var modelData
                required property int index

                readonly property bool selectedConv: modelData.type === "conv"
                        && modelData.id === app.currentConversationId
                        && app.viewMode === 0
                readonly property bool mainHeader: modelData.type === "header"
                        && modelData.level !== 1
                readonly property bool busy: (modelData.type === "conv"
                        && app.streamingConversationIds.indexOf(modelData.id) >= 0)
                        || (modelData.type === "project"
                            && app.streamingProjectIds.indexOf(modelData.id) >= 0)

                width: list.width
                height: modelData.type === "header"
                        ? (itemRect.mainHeader ? 40 : 30)
                        : (modelData.type === "search" ? Math.max(52, theme.sidebarFontSize * 4)
                           : (modelData.type === "project" ? 38 : 34))
                radius: 9
                color: {
                    if (modelData.type === "header")
                        return "transparent"
                    if (selectedConv)
                        return theme.selected
                    return mouse.containsMouse ? theme.selected : "transparent"
                }

                Canvas {
                    id: busySpinner
                    visible: itemRect.busy
                    width: 14
                    height: 14
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter

                    readonly property color spinnerColor: itemRect.selectedConv
                            ? theme.text : theme.accent

                    onSpinnerColorChanged: requestPaint()
                    onVisibleChanged: if (visible) requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.strokeStyle = spinnerColor
                        ctx.lineWidth = 2
                        ctx.lineCap = "round"
                        ctx.beginPath()
                        ctx.arc(7, 7, 5, 0, Math.PI * 1.5)
                        ctx.stroke()
                    }

                    RotationAnimator on rotation {
                        running: busySpinner.visible
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: 900
                    }
                }

                Text {
                    anchors.verticalCenter: modelData.type === "search"
                                            ? undefined : parent.verticalCenter
                    anchors.top: modelData.type === "search" ? parent.top : undefined
                    anchors.topMargin: 5
                    anchors.left: parent.left
                    anchors.leftMargin: (modelData.type === "project" ? 52 : 12)
                        + (modelData.indent ? modelData.indent * 52 : 0)
                    anchors.right: parent.right
                    anchors.rightMargin: itemRect.busy ? 30 : 8
                    text: modelData.type === "conv"
                          ? (modelData.pinned ? "★ " : "")
                            + (modelData.parentId > 0 ? "↳ " : "") + modelData.title
                          : modelData.title
                    color: modelData.type === "header"
                           ? theme.muted : theme.text
                    font.bold: itemRect.selectedConv || modelData.type === "search"
                    font.weight: modelData.type === "header" || modelData.type === "project"
                                 ? Font.DemiBold : Font.Normal
                    font.pointSize: modelData.type === "header"
                                    ? Math.max(10, theme.sidebarFontSize - 1) : theme.sidebarFontSize
                    font.family: theme.fontFamily
                    elide: Text.ElideRight
                }

                IconImage {
                    visible: modelData.type === "project"
                    x: 28
                    anchors.verticalCenter: parent.verticalCenter
                    width: 17
                    height: 17
                    source: "qrc:/resources/icons/folder.svg"
                    color: theme.muted
                }

                Text {
                    visible: modelData.type === "search"
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.top: parent.top
                    anchors.topMargin: 27
                    text: modelData.snippet !== undefined ? modelData.snippet : ""
                    color: theme.muted
                    font.pointSize: Math.max(9, theme.sidebarFontSize - 2)
                    font.family: theme.fontFamily
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (event) => {
                        if (modelData.type === "header")
                            return
                        if (event.button === Qt.RightButton) {
                            if (modelData.type === "search")
                                return
                            ctx.targetId = modelData.id
                            ctx.targetTitle = modelData.title
                            if (modelData.type === "project")
                                projectMenu.popup()
                            else
                                convMenu.popup()
                            return
                        }
                        if (modelData.type === "search") {
                            app.openConversation(modelData.id)
                            search.text = ""
                            app.clearSearch()
                            return
                        }
                        if (modelData.type === "project")
                            app.openProject(modelData.id)
                        else if (modelData.type === "conv")
                            app.openConversation(modelData.id)
                    }
                }
                ToolButton {
                    visible: modelData.type === "project"
                    width: 28
                    height: parent.height
                    text: sidebar.collapsedProjects[modelData.id] ? "›" : "⌄"
                    font.pointSize: theme.sidebarFontSize
                    ToolTip.text: sidebar.collapsedProjects[modelData.id]
                                  ? "Mostrar conversas do projeto" : "Recolher conversas do projeto"
                    ToolTip.visible: hovered
                    Accessible.name: ToolTip.text
                    onClicked: sidebar.toggleProject(modelData.id)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Rectangle {
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                implicitHeight: 1
                color: theme.border
            }

            SidebarAction {
                text: "Configurações"
                icon.source: "qrc:/resources/icons/gear.svg"
                Layout.fillWidth: true
                onClicked: settingsDialog.open()
            }

            SidebarAction {
                text: "Sair"
                icon.source: "qrc:/resources/icons/logout.svg"
                Layout.fillWidth: true
                onClicked: app.logout()
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                text: app.displayName.length > 0 ? app.displayName : app.userName
                color: theme.muted
                font.pointSize: Math.max(9, theme.sidebarFontSize - 1)
                font.family: theme.fontFamily
                elide: Text.ElideRight
            }
        }
    }

    QtObject {
        id: ctx
        property int targetId: 0
        property string targetTitle: ""
    }

    AutomationsDialog {
        id: automationsDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
    }

    Menu {
        id: projectMenu

        MenuItem {
            text: "Nova conversa"
            onTriggered: app.newConversation(ctx.targetId)
        }

        MenuItem {
            text: "Abrir projeto"
            onTriggered: app.openProject(ctx.targetId)
        }

        MenuItem {
            text: "Renomear projeto"
            onTriggered: renameProjectDialog.open()
        }

        MenuItem {
            text: "Abrir pasta do projeto"
            onTriggered: app.openProjectFolder(ctx.targetId)
        }

        MenuSeparator {}

        MenuItem {
            text: "Excluir projeto"
            onTriggered: deleteProjectDialog.open()
        }
    }

    Menu {
        id: convMenu

        MenuItem {
            text: app.isConversationPinned(ctx.targetId) ? "Desafixar conversa" : "Fixar conversa"
            onTriggered: app.toggleConversationPinned(ctx.targetId)
        }

        MenuSeparator {}

        MenuItem {
            text: "Editar etiquetas"
            onTriggered: tagsDialog.open()
        }

        MenuItem {
            text: "Ramificar conversa"
            onTriggered: app.branchConversation(ctx.targetId)
        }

        MenuItem {
            text: "Transformar em projeto"
            onTriggered: app.convertConversationToProject(ctx.targetId)
        }

        MenuItem {
            text: "Renomear"
            onTriggered: renameDialog.open()
        }

        MenuItem {
            text: "Excluir"
            onTriggered: deleteDialog.open()
        }
    }

    AppDialog {
        id: tagsDialog
        parent: Overlay.overlay
        title: "Etiquetas da conversa"
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        AppTextField {
            id: tagsField
            width: parent.width
            placeholderText: "Ex.: trabalho, urgente"
            onAccepted: tagsDialog.accept()
        }

        onOpened: tagsField.text = app.conversationTags(ctx.targetId)
        onAccepted: app.setConversationTags(ctx.targetId, tagsField.text)
    }

    AppDialog {
        id: renameDialog
        parent: Overlay.overlay
        title: "Renomear conversa"
        modal: true
    font.pointSize: theme.fontSize
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        AppTextField {
            id: renameField
            width: parent.width
            text: ctx.targetTitle
            onAccepted: renameDialog.accept()
        }

        onOpened: {
            renameField.text = ctx.targetTitle
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        onAccepted: app.renameConversation(ctx.targetId, renameField.text)
    }

    AppDialog {
        id: renameProjectDialog
        parent: Overlay.overlay
        title: "Renomear projeto"
        modal: true
        font.pointSize: theme.fontSize
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        AppTextField {
            id: renameProjectField
            width: parent.width
            onAccepted: renameProjectDialog.accept()
        }

        onOpened: {
            renameProjectField.text = ctx.targetTitle
            renameProjectField.forceActiveFocus()
            renameProjectField.selectAll()
        }
        onAccepted: app.renameProject(ctx.targetId, renameProjectField.text)
    }

    AppDialog {
        id: deleteDialog
        parent: Overlay.overlay
    font.pointSize: theme.fontSize
        title: "Excluir conversa"
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Yes | Dialog.No

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "Excluir \"" + ctx.targetTitle + "\" e todas as mensagens?"
            color: theme.text
        }

        onAccepted: app.deleteConversation(ctx.targetId)
    }

    AppDialog {
        id: deleteProjectDialog
        parent: Overlay.overlay
    font.pointSize: theme.fontSize
        title: "Excluir projeto"
        modal: true
        anchors.centerIn: parent
        width: 420
        standardButtons: Dialog.Yes | Dialog.No

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: theme.text
            text: "Excluir o projeto \"" + ctx.targetTitle
                  + "\" com todas as conversas, arquivos e comandos?"
        }

        onAccepted: app.deleteProject(ctx.targetId)
    }

    AppDialog {
        id: newProjectDialog
        parent: Overlay.overlay
    font.pointSize: theme.fontSize
        title: "Novo projeto"
        modal: true
        anchors.centerIn: parent
        width: 480
        standardButtons: Dialog.Ok | Dialog.Cancel

        GridLayout {
            width: parent.width
            columns: 2
            columnSpacing: 10
            rowSpacing: 10

            Text { text: "Nome:"; color: theme.text }
            AppTextField {
                id: projectName
                Layout.fillWidth: true
                placeholderText: "meu projeto"
            }

            Text { text: "Descrição:"; color: theme.text }
            AppTextField {
                id: projectDescription
                Layout.fillWidth: true
            }

            Text { text: "Pasta:"; color: theme.text }
            RowLayout {
                Layout.fillWidth: true
                AppTextField {
                    id: projectWorkdir
                    Layout.fillWidth: true
                    placeholderText: "/caminho/do/projeto"
                }
                AppButton {
                    text: "..."
                    icon.source: "qrc:/resources/icons/folder.svg"
                    onClicked: projectWorkdir.text = app.chooseDirectory("Escolha a pasta de trabalho")
                }
            }

            Text { text: "Modelo:"; color: theme.text }
            HeaderComboBox {
                id: projectModel
                Layout.fillWidth: true
                model: app.models.map(function (m) { return m.id })
            }

            Text { text: "Esforço:"; color: theme.text }
            HeaderComboBox {
                id: projectEffort
                Layout.fillWidth: true
                model: ["low", "high", "max"]
            }
        }

        onAccepted: app.createProject({
            "name": projectName.text,
            "description": projectDescription.text,
            "workdir": projectWorkdir.text,
            "systemPrompt": "",
            "defaultModel": projectModel.currentText,
            "defaultEffort": projectEffort.currentText
        })
    }

    SettingsDialog {
        id: settingsDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
    }

    ActivitiesDialog {
        id: activitiesDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
    }

}
