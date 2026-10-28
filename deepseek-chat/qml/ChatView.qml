import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: view

    signal workspaceRequested(int tab)
    property var detectedLinks: []
    // Unsent text is kept per conversation while the application is open.
    property var drafts: ({})
    property var draftConversationId: 0
    property var slashMatches: []

    function permissionLabel(mode) {
        return mode === "full" ? "Acesso completo" : (mode === "auto" ? "Aprovar por mim" : "Solicitar aprovação")
    }

    function updateSlashMatches() {
        var text = input.text
        if (text.length === 0 || text.charAt(0) !== "/" || /\s/.test(text)) {
            slashMatches = []
            return
        }
        var query = text.substring(1).toLowerCase()
        slashMatches = commandsMenu.allCommands.filter(function(command) {
            return command.name.toLowerCase().indexOf(query) === 0
        }).slice(0, 8)
        slashList.currentIndex = 0
    }

    function useCommand(command) {
        input.text = command.prompt
        input.cursorPosition = input.length
        input.forceActiveFocus()
        slashMatches = []
        app.logClientActivity("command_use", command.name)
    }

    function sendInput() {
        var text = input.text
        if (text.trim().length === 0) return
        input.text = ""
        var drafts = view.drafts
        delete drafts[String(app.currentConversationId)]
        view.drafts = drafts
        app.send(text)
    }

    function updateLinks() {
        var re = /https?:\/\/[^\s<>"']+/g
        var out = []
        var m
        while ((m = re.exec(input.text)) !== null) {
            var u = m[0].replace(/[.,;:)\]]+$/, "")
            if (out.indexOf(u) < 0)
                out.push(u)
        }
        detectedLinks = out
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: Math.max(62, theme.fontSize * 2 + 28)
            color: theme.bg

            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: app.currentTitle
                    font.bold: true
                    font.pointSize: Math.min(15, theme.fontSize + 1)
                    font.family: theme.fontFamily
                    color: theme.text
                    elide: Text.ElideRight
                }

                AppButton {
                    id: permissionChip
                    objectName: "permissionChip"
                    readonly property string mode: app.activeProjectPermissionMode
                    text: view.width < 900 ? "" : view.permissionLabel(mode)
                    icon.source: "qrc:/resources/icons/shield.svg"
                    icon.color: mode === "full" ? theme.danger : (mode === "auto" ? theme.accent : theme.text)
                    palette.buttonText: icon.color
                    ghost: true
                    ToolTip.visible: hovered
                    ToolTip.text: "Permissões da IA " + (app.activeProjectId > 0 ? "neste projeto" : "nas conversas soltas")
                                  + ": " + view.permissionLabel(mode) + ". Clique para alterar."
                    onClicked: permissionMenu.popup()

                    Menu {
                        id: permissionMenu
                        MenuItem { text: "Solicitar aprovação"; checkable: true; checked: permissionChip.mode === "ask"; onTriggered: app.setPermissionMode("ask") }
                        MenuItem { text: "Aprovar por mim (ações seguras)"; checkable: true; checked: permissionChip.mode === "auto"; onTriggered: app.setPermissionMode("auto") }
                        MenuItem { text: "Acesso completo"; checkable: true; checked: permissionChip.mode === "full"; onTriggered: app.setPermissionMode("full") }
                    }
                }

                AppButton {
                    visible: app.currentParentId > 0
                    text: view.width < 850 ? "↶" : "↶ Retroceder versão"
                    ghost: true
                    ToolTip.visible: hovered
                    ToolTip.text: "Voltar à versão anterior sem apagar esta versão"
                    onClicked: app.openConversation(app.currentParentId)
                }

                AppButton {
                    id: usageButton
                    text: app.usageInfo.costText || (view.width < 900 ? "" : "Custos")
                    icon.source: "qrc:/resources/icons/chart.svg"
                    ghost: true
                    ToolTip.visible: hovered
                    ToolTip.text: "Custos, tokens e saldo da conta"
                    onClicked: usagePopup.open()

                    Popup {
                        id: usagePopup
                        x: -usagePopup.width + usageButton.width
                        y: usageButton.height + 6
                        width: 300
                        padding: 0

                        background: Rectangle {
                            radius: 12
                            color: theme.dark ? "#2b2b2b" : "#ffffff"
                            border.color: theme.border
                        }

                        contentItem: Item {
                            implicitHeight: infoColumn.implicitHeight + 28

                            ColumnLayout {
                                id: infoColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 14
                                spacing: 9

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Custo"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.costText || "—"; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Uso"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.percentText || "—"; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Tokens"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.tokensText || "—"; color: theme.text; font.bold: true }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Entrada"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: Number(app.usageInfo.inputTokens).toLocaleString(Qt.locale(), 'f', 0); color: theme.text }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Saída"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: Number(app.usageInfo.outputTokens).toLocaleString(Qt.locale(), 'f', 0); color: theme.text }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Cache (hit)"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: Number(app.usageInfo.cachedTokens).toLocaleString(Qt.locale(), 'f', 0); color: theme.text }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Total (conta)"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.totalCostText || "—"; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Tokens totais"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.totalTokensText || "—"; color: theme.text }
                                }

                                Rectangle { Layout.fillWidth: true; height: 1; color: theme.border }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Saldo DeepSeek"; color: theme.muted; Layout.fillWidth: true }
                                    Text {
                                        text: app.balanceInfo.text ? app.balanceInfo.text : "—"
                                        color: theme.text
                                        font.bold: true
                                    }
                                }

                                AppButton {
                                    text: "Atualizar saldo"
                                    icon.source: "qrc:/resources/icons/refresh.svg"
                                    ghost: true
                                    onClicked: app.refreshBalance()
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    font.pixelSize: Math.max(10, theme.fontSize - 3)
                                    color: theme.muted
                                    text: app.usageInfo.peak
                                          ? "Horário de pico (preço cheio, UTC)"
                                          : "Fora de pico (50% do preço, UTC)"
                                }
                            }
                        }
                    }
                }

                HeaderComboBox {
                    id: modelCombo
                    objectName: "modelSelector"
                    Layout.preferredWidth: Math.min(190, view.width * 0.28)
                    Accessible.name: "Modelo"
                    ToolTip.text: "Modelo"
                    ToolTip.visible: hovered
                    model: app.models.map(function (m) { return m.id })
                    onActivated: app.currentModel = currentText
                }

                HeaderComboBox {
                    id: effortCombo
                    Layout.preferredWidth: Math.min(110, view.width * 0.17)
                    Accessible.name: "Esforço"
                    ToolTip.text: "Esforço"
                    ToolTip.visible: hovered
                    model: app.efforts
                    onActivated: app.currentEffort = currentText
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

        ListView {
            id: messageList
            anchors.fill: parent
            clip: true
            model: app.messageModel
            spacing: 14
            topMargin: 16
            bottomMargin: 16
            leftMargin: 18
            rightMargin: 18
            delegate: MessageDelegate {
                width: messageList.width - 36
            }

            property bool autoScroll: true

            onCountChanged: positionViewAtEnd()

            onContentYChanged: {
                if (messageList.contentHeight - messageList.contentY - messageList.height < 2)
                    autoScroll = true
                else if (autoScroll)
                    autoScroll = false
            }

            onContentHeightChanged: {
                if (autoScroll)
                    positionViewAtEnd()
            }
        }

        AppButton {
            objectName: "jumpToEndButton"
            visible: !messageList.autoScroll && messageList.count > 0
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            text: app.streaming ? "Acompanhar resposta" : "Ir para o fim"
            icon.source: "qrc:/resources/icons/arrow-down.svg"
            cornerRadius: 18
            onClicked: {
                messageList.autoScroll = true
                messageList.positionViewAtEnd()
            }
        }
        }

        Rectangle {
            Layout.fillWidth: true
            color: theme.bg
            implicitHeight: composer.implicitHeight + 24

            Rectangle {
                width: parent.width
                height: 1
                color: "transparent"
            }

            ColumnLayout {
                id: composer
                anchors.fill: parent
                anchors.margins: 14
                anchors.leftMargin: Math.max(12, (parent.width - 920) / 2)
                anchors.rightMargin: Math.max(12, (parent.width - 920) / 2)
                spacing: 8

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: view.detectedLinks.length > 0

                    Repeater {
                        model: view.detectedLinks

                        delegate: Rectangle {
                            required property var modelData

                            height: 26
                            radius: 13
                            width: linkRow.implicitWidth + 30
                            color: theme.dark ? "#2f3340" : "#eef2ff"
                            border.color: theme.border

                            RowLayout {
                                id: linkRow
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                spacing: 6

                                Image {
                                    source: "qrc:/resources/icons/globe.svg"
                                    sourceSize.width: 14
                                    sourceSize.height: 14
                                }

                                Text {
                                    text: modelData.replace(/^https?:\/\//, "").split("/")[0]
                                    color: theme.text
                                    font.pixelSize: 12
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: app.openLink(modelData)

                                ToolTip.visible: containsMouse
                                ToolTip.text: "Abrir " + modelData
                            }
                        }
                    }

                    AppButton {
                        text: "Anexar página"
                        icon.source: "qrc:/resources/icons/paperclip.svg"
                        implicitHeight: 26
                        font.pointSize: Math.max(9, theme.fontSize - 3)
                        onClicked: {
                            for (var i = 0; i < view.detectedLinks.length; ++i)
                                app.attachUrl(view.detectedLinks[i])
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: app.attachments.length > 0

                    Repeater {
                        model: app.attachments

                        delegate: Rectangle {
                            required property var modelData
                            required property int index

                            readonly property bool isImage: modelData.image
                                    && modelData.url !== undefined
                                    && String(modelData.url).length > 0

                            height: isImage ? 72 : 26
                            width: isImage ? 72 : (chipLabel.implicitWidth + 40)
                            radius: isImage ? 10 : 13
                            color: isImage ? "transparent"
                                           : (theme.dark ? "#2f3340" : "#eef2ff")
                            border.color: theme.border
                            clip: isImage

                            Image {
                                anchors.fill: parent
                                anchors.margins: 1
                                visible: parent.isImage
                                source: parent.isImage ? modelData.url : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 144
                                sourceSize.height: 144
                            }

                            Text {
                                id: chipLabel
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !parent.isImage
                                text: modelData.name
                                color: theme.text
                                font.pixelSize: 12
                            }

                            Rectangle {
                                visible: parent.isImage
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 3
                                width: 22
                                height: 22
                                radius: 11
                                color: "#99000000"

                                Image {
                                    anchors.centerIn: parent
                                    source: "qrc:/resources/icons/close.svg"
                                    sourceSize.width: 12
                                    sourceSize.height: 12
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: app.removeAttachment(index)
                                }
                            }

                            AppButton {
                                visible: !parent.isImage
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                text: ""
                                icon.source: "qrc:/resources/icons/close.svg"
                                icon.width: 12
                                icon.height: 12
                                ghost: true
                                implicitWidth: 24
                                implicitHeight: 24
                                onClicked: app.removeAttachment(index)
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "slashSuggestions"
                    Layout.fillWidth: true
                    visible: view.slashMatches.length > 0
                    implicitHeight: Math.min(260, slashList.contentHeight + 12)
                    radius: 12
                    color: theme.card
                    border.color: theme.border
                    ListView {
                        id: slashList
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: view.slashMatches
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: slashList.width
                            height: slashRow.implicitHeight + 10
                            radius: 8
                            color: index === slashList.currentIndex ? theme.selected : "transparent"
                            TapHandler { onTapped: view.useCommand(modelData) }
                            RowLayout {
                                id: slashRow
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                Text { text: "/" + modelData.name; color: theme.text; font.bold: true }
                                Text { Layout.fillWidth: true; text: modelData.prompt; color: theme.muted; elide: Text.ElideRight; maximumLineCount: 1 }
                            }
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(200, Math.max(72,
                        input.contentHeight + input.topPadding + input.bottomPadding))

                    CopyableTextArea {
                        id: input
                        placeholderText: "Escreva sua mensagem…"
                        wrapMode: TextArea.Wrap
                        font.pointSize: theme.chatFontSize
                        font.family: theme.chatFontFamily
                        onTextChanged: {
                            linkTimer.restart()
                            view.updateSlashMatches()
                        }

                        background: Rectangle {
                            radius: 16
                            color: theme.card
                            border.width: composerDrop.containsDrag ? 2 : 1
                            border.color: composerDrop.containsDrag ? theme.accent : theme.border
                        }

                        Keys.onPressed: (event) => {
                            if (view.slashMatches.length > 0) {
                                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                                    var step = event.key === Qt.Key_Down ? 1 : -1
                                    slashList.currentIndex = (slashList.currentIndex + step + view.slashMatches.length) % view.slashMatches.length
                                    event.accepted = true
                                    return
                                }
                                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    view.useCommand(view.slashMatches[Math.max(0, slashList.currentIndex)])
                                    event.accepted = true
                                    return
                                }
                                if (event.key === Qt.Key_Escape) {
                                    view.slashMatches = []
                                    event.accepted = true
                                    return
                                }
                            }
                            if (event.key === Qt.Key_Escape && app.streaming) {
                                app.stop()
                                event.accepted = true
                                return
                            }
                            if (event.key === Qt.Key_V
                                    && (event.modifiers & Qt.ControlModifier)
                                    && !(event.modifiers & Qt.ShiftModifier)
                                    && !(event.modifiers & Qt.AltModifier)) {
                                if (app.pasteClipboard()) {
                                    event.accepted = true
                                    return
                                }
                            }
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                var shouldSend = app.enterToSend
                                        ? !(event.modifiers & Qt.ShiftModifier)
                                        : (event.modifiers & Qt.ControlModifier)
                                if (shouldSend) {
                                    event.accepted = true
                                    view.sendInput()
                                }
                            }
                        }
                    }
                }

                Timer {
                    id: linkTimer
                    interval: 150
                    onTriggered: view.updateLinks()
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppButton {
                        text: view.width < 640 ? "" : "Anexar"
                        ToolTip.text: "Anexar arquivos"
                        ToolTip.visible: hovered
                        icon.source: "qrc:/resources/icons/paperclip.svg"
                        onClicked: app.attachFiles()
                    }

                    AppButton {
                        text: view.width < 740 ? "" : "Capturar"
                        ToolTip.text: "Capturar tela"
                        ToolTip.visible: hovered
                        icon.source: "qrc:/resources/icons/image.svg"
                        onClicked: captureMenu.popup()

                        Menu {
                            id: captureMenu
                            MenuItem {
                                text: "Selecionar área da tela"
                                onTriggered: app.captureScreenRegion()
                            }
                            MenuItem {
                                text: "Janela do aplicativo"
                                onTriggered: app.captureAppWindow()
                            }
                        }
                    }

                    AppButton {
                        text: ""
                        icon.source: "qrc:/resources/icons/globe.svg"
                        implicitWidth: 40
                        ToolTip.visible: hovered
                        ToolTip.text: "Pesquisar na web com fontes"
                        onClicked: {
                            if (input.text.trim().length > 0) {
                                var question = input.text
                                input.text = ""
                                app.send("Pesquise na web, abra as fontes relevantes e cite os links: " + question)
                            } else {
                                input.text = "Pesquise na web, abra as fontes relevantes e cite os links: "
                                input.forceActiveFocus()
                            }
                        }
                    }

                    AppButton {
                        text: ""
                        icon.source: app.recording
                                     ? "qrc:/resources/icons/stop.svg"
                                     : "qrc:/resources/icons/mic.svg"
                        primary: app.recording
                        danger: app.recording
                        implicitWidth: 40
                        ToolTip.visible: hovered
                        ToolTip.text: app.recording ? "Parar gravação" : "Gravar mensagem de voz"
                        onClicked: {
                            if (app.recording)
                                app.stopRecording()
                            else
                                app.startRecording()
                        }
                    }

                    AppButton {
                        text: ""
                        icon.source: "qrc:/resources/icons/slash.svg"
                        implicitWidth: 40
                        ToolTip.visible: hovered
                        ToolTip.text: "Comandos rápidos (digite / no início da mensagem)"
                        onClicked: commandsMenu.popup()
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: view.width < 560 ? "" : (app.streaming ? "Parar" : "Enviar")
                        ToolTip.visible: hovered && text.length === 0
                        ToolTip.text: app.streaming ? "Parar (Esc)" : "Enviar"
                        icon.source: app.streaming
                                     ? "qrc:/resources/icons/stop.svg"
                                     : "qrc:/resources/icons/send.svg"
                        primary: !app.streaming
                        danger: app.streaming
                        enabled: app.streaming || input.text.trim().length > 0
                        onClicked: {
                            if (app.streaming)
                                app.stop()
                            else
                                view.sendInput()
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: (app.enterToSend ? "Enter envia · Shift+Enter quebra linha"
                                            : "Ctrl+Enter envia · Enter quebra linha")
                          + (app.streaming ? " · Esc para" : " · / comandos · Ctrl+K paleta")
                          + (input.text.trim().length > 0
                             ? " · " + app.estimateMessageCost(input.text) : "")
                    color: theme.muted
                    font.family: theme.fontFamily
                    font.pointSize: 9
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }

            DropArea {
                id: composerDrop
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls)
                        app.attachDroppedUrls(drop.urls)
                }
            }
        }
    }

    Menu {
        id: commandsMenu
        property var allCommands: app.globalCommands.concat(app.projectCommands)

        Repeater {
            model: commandsMenu.allCommands

            MenuItem {
                required property var modelData
                text: "/" + modelData.name
                onTriggered: view.useCommand(modelData)
            }
        }
    }

    Connections {
        target: app

        function onComposerTextRequested(text) {
            input.text = text
            input.forceActiveFocus()
        }

        function onCurrentConversationChanged() {
            var drafts = view.drafts
            if (input.text.trim().length > 0) drafts[String(view.draftConversationId)] = input.text
            else delete drafts[String(view.draftConversationId)]
            view.drafts = drafts
            view.draftConversationId = app.currentConversationId
            input.text = drafts[String(app.currentConversationId)] || ""
            modelCombo.currentIndex = Math.max(0, modelCombo.model.indexOf(app.currentModel))
            effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
        }

        function onEffortsChanged() {
            effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
        }
    }

    Component.onCompleted: {
        view.draftConversationId = app.currentConversationId
        modelCombo.currentIndex = Math.max(0, modelCombo.model.indexOf(app.currentModel))
        effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
    }
}
