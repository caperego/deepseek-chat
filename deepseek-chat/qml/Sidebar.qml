import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sidebar
    color: theme.sidebar
    signal collapseRequested()
    property var collapsedProjects: ({})

    function openSettings() { settingsDialog.open() }
    function openNewProject() { newProjectDialog.open() }
    function openTerminal() { terminalDialog.open() }
    function openActivities() { activitiesDialog.open() }
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
            for (var row = 0; row < app.sidebar.length; ++row) {
                var entry = app.sidebar[row]
                if (entry.type === "project") project = entry.id
                if (entry.type === "header") project = 0
                if (entry.type === "conv" && project && collapsedProjects[project]) continue
                out.push(entry)
            }
            return out
        }
        for (var j = 0; j < app.sidebar.length; ++j) {
            var item = app.sidebar[j]
            if (item.type !== "header" && item.title
                    && item.title.toLowerCase().indexOf(term) >= 0)
                out.push(item)
        }
        return out
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

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
                ToolTip.text: "Recolher projetos e conversas"
                ToolTip.visible: hovered
                onClicked: sidebar.collapseRequested()
            }
        }

        Text {
            text: "Conectado como " + (app.displayName.length > 0 ? app.displayName : app.userName)
            color: theme.muted
            font.pointSize: Math.max(9, theme.sidebarFontSize - 1)
            font.family: theme.fontFamily
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        AppButton {
            text: "Nova conversa"
            icon.source: "qrc:/resources/icons/plus.svg"
            Layout.fillWidth: true
            font.pointSize: theme.sidebarFontSize
            primary: true
            onClicked: app.newConversation(app.activeProjectId)
        }

        AppButton {
            text: "Novo projeto"
            icon.source: "qrc:/resources/icons/folder.svg"
            Layout.fillWidth: true
            font.pointSize: theme.sidebarFontSize
            onClicked: newProjectDialog.open()
        }

        TextField {
            id: search
            placeholderText: "Pesquisar..."
            Layout.fillWidth: true
            font.pointSize: theme.sidebarFontSize
            font.family: theme.fontFamily
            leftPadding: 14
            implicitHeight: Math.max(40, contentHeight + 18)
            background: Rectangle {
                radius: 11
                color: theme.card
                border.color: search.activeFocus ? theme.accent : theme.border
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
                    anchors.leftMargin: (modelData.type === "project" ? 32 : 12)
                        + (modelData.indent ? modelData.indent * 12 : 0)
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
            spacing: 6

            AppButton {
                text: "Atividades"
                ghost: true
                font.pointSize: theme.sidebarFontSize
                icon.source: "qrc:/resources/icons/list.svg"
                Layout.fillWidth: true
                onClicked: {
                    app.refreshActivities()
                    activitiesDialog.open()
                }
            }

            AppButton {
                text: "Terminal"
                ghost: true
                font.pointSize: theme.sidebarFontSize
                icon.source: "qrc:/resources/icons/slash.svg"
                Layout.fillWidth: true
                onClicked: terminalDialog.open()
            }

            AppButton {
                text: "Configurações"
                ghost: true
                font.pointSize: theme.sidebarFontSize
                icon.source: "qrc:/resources/icons/gear.svg"
                Layout.fillWidth: true
                onClicked: settingsDialog.open()
            }

            AppButton {
                text: "Sair"
                ghost: true
                font.pointSize: theme.sidebarFontSize
                icon.source: "qrc:/resources/icons/logout.svg"
                Layout.fillWidth: true
                onClicked: app.logout()
            }
        }
    }

    QtObject {
        id: ctx
        property int targetId: 0
        property string targetTitle: ""
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

    Dialog {
        id: tagsDialog
        parent: Overlay.overlay
        title: "Etiquetas da conversa"
        modal: true
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: tagsField
            width: parent.width
            placeholderText: "Ex.: trabalho, urgente"
        }

        onOpened: tagsField.text = app.conversationTags(ctx.targetId)
        onAccepted: app.setConversationTags(ctx.targetId, tagsField.text)
    }

    Dialog {
        id: renameDialog
        parent: Overlay.overlay
        title: "Renomear conversa"
        modal: true
    font.pointSize: theme.fontSize
        anchors.centerIn: parent
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextField {
            id: renameField
            width: parent.width
            text: ctx.targetTitle
        }

        onOpened: renameField.text = ctx.targetTitle
        onAccepted: app.renameConversation(ctx.targetId, renameField.text)
    }

    Dialog {
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

    Dialog {
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

    Dialog {
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
            TextField {
                id: projectName
                Layout.fillWidth: true
                placeholderText: "meu projeto"
            }

            Text { text: "Descrição:"; color: theme.text }
            TextField {
                id: projectDescription
                Layout.fillWidth: true
            }

            Text { text: "Pasta:"; color: theme.text }
            RowLayout {
                Layout.fillWidth: true
                TextField {
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
            ComboBox {
                id: projectModel
                Layout.fillWidth: true
                model: app.models.map(function (m) { return m.id })
            }

            Text { text: "Esforço:"; color: theme.text }
            ComboBox {
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

    Dialog {
        id: terminalDialog
        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 820
        height: 580
        title: "Terminal"
        font.pointSize: theme.fontSize
        property string output: ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            ScrollView {
                id: terminalScroll
                Layout.fillWidth: true
                Layout.fillHeight: true

                CopyableTextArea {
                    id: outputArea
                    readOnly: true
                    wrapMode: TextArea.NoWrap
                    font.family: "monospace"
                    font.pointSize: Math.max(9, theme.fontSize - 2)
                    color: theme.text
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: terminalInput
                    Layout.fillWidth: true
                    placeholderText: "comando…"
                    onAccepted: terminalDialog.submit()
                }

                AppButton {
                    text: "Enviar"
                    primary: true
                    onClicked: terminalDialog.submit()
                }

                AppButton {
                    text: "Parar"
                    danger: true
                    enabled: app.terminalRunning
                    onClicked: app.killTerminal()
                }

                AppButton {
                    text: "Fechar"
                    ghost: true
                    onClicked: terminalDialog.close()
                }
            }
        }

        function submit() {
            if (terminalInput.text.length === 0)
                return
            app.runTerminalCommand(terminalInput.text)
            terminalInput.text = ""
        }

        function appendOutput(text) {
            output = output + text
            if (output.length > 200000)
                output = output.slice(output.length - 200000)
            outputArea.text = output
            outputArea.cursorPosition = outputArea.length
        }
    }

    Connections {
        target: app

        function onTerminalOutput(text) {
            terminalDialog.appendOutput(text)
        }
    }
}
