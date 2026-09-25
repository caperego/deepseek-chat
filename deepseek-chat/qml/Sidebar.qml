import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

Rectangle {
    id: sidebar
    color: theme.sidebar

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
        if (term.length === 0)
            return app.sidebar
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
        anchors.margins: 10
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
                font.pixelSize: 17
                color: theme.text
                Layout.fillWidth: true
            }
        }

        Text {
            text: "Conectado como " + (app.displayName.length > 0 ? app.displayName : app.userName)
            color: theme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        AppButton {
            text: "+  Nova conversa"
            icon.source: "qrc:/resources/icons/plus.svg"
            Layout.fillWidth: true
            onClicked: app.newConversation(app.activeProjectId)
        }

        AppButton {
            text: "+  Novo projeto"
            icon.source: "qrc:/resources/icons/folder.svg"
            Layout.fillWidth: true
            onClicked: newProjectDialog.open()
        }

        TextField {
            id: search
            placeholderText: "Pesquisar..."
            Layout.fillWidth: true

            onTextChanged: {
                if (text.trimmed().length >= 2) {
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
            spacing: 2

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
                        ? (itemRect.mainHeader ? 38 : 30)
                        : (modelData.type === "search" ? 52 : 40)
                radius: 8
                color: {
                    if (modelData.type === "header")
                        return "transparent"
                    if (selectedConv)
                        return theme.accent
                    return mouse.containsMouse ? theme.selected : "transparent"
                }

                RectangularShadow {
                    anchors.fill: parent
                    radius: parent.radius
                    blur: 16
                    color: theme.accent
                    offset: Qt.vector2d(0, 3)
                    visible: itemRect.selectedConv
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
                            ? "#ffffff" : theme.accent

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
                    anchors.leftMargin: 10 + (modelData.indent ? modelData.indent * 16 : 0)
                    anchors.right: parent.right
                    anchors.rightMargin: itemRect.busy ? 30 : 8
                    text: modelData.title
                    color: modelData.type === "header"
                           ? theme.accent
                           : (itemRect.selectedConv ? "#ffffff" : theme.text)
                    font.bold: modelData.type === "header" || itemRect.selectedConv
                               || modelData.type === "project"
                               || modelData.type === "search"
                    font.pixelSize: modelData.type === "header"
                                    ? (itemRect.mainHeader ? 15 : 13) : 14
                    font.letterSpacing: modelData.type === "header" ? 0.8 : 0
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
                    font.pixelSize: 11
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
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                text: "Atividades"
                icon.source: "qrc:/resources/icons/list.svg"
                Layout.fillWidth: true
                onClicked: {
                    app.refreshActivities()
                    activitiesDialog.open()
                }
            }

            AppButton {
                text: "Terminal"
                icon.source: "qrc:/resources/icons/slash.svg"
                Layout.fillWidth: true
                onClicked: terminalDialog.open()
            }

            AppButton {
                text: "Configurações"
                icon.source: "qrc:/resources/icons/gear.svg"
                Layout.fillWidth: true
                onClicked: settingsDialog.open()
            }

            AppButton {
                text: "Sair"
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
        id: renameDialog
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
        anchors.centerIn: parent
    }

    ActivitiesDialog {
        id: activitiesDialog
        anchors.centerIn: parent
    }

    Dialog {
        id: terminalDialog
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

                TextArea {
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
