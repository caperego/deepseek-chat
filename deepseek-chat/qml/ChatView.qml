import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: view

    property var detectedLinks: []

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
            height: 54
            color: theme.bg

            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: app.currentTitle
                    font.bold: true
                    font.pixelSize: app.currentConversationId > 0 ? 17 : 15
                    color: app.currentConversationId > 0 ? theme.accent : theme.text
                    elide: Text.ElideRight
                }

                AppButton {
                    id: usageButton
                    text: app.usageInfo.costText
                    icon.source: "qrc:/resources/icons/chart.svg"
                    ghost: true
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
                                    Text { text: app.usageInfo.costText; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Uso"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.percentText; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Tokens"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.tokensText; color: theme.text; font.bold: true }
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
                                    Text { text: app.usageInfo.totalCostText; color: theme.text; font.bold: true }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: "Tokens totais"; color: theme.muted; Layout.fillWidth: true }
                                    Text { text: app.usageInfo.totalTokensText; color: theme.text }
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

                Text {
                    text: "Modelo:"
                    color: theme.muted
                }

                ComboBox {
                    id: modelCombo
                    Layout.preferredWidth: 190
                    model: app.models.map(function (m) { return m.id })
                    onActivated: app.currentModel = currentText
                }

                Text {
                    text: "Esforço:"
                    color: theme.muted
                }

                ComboBox {
                    id: effortCombo
                    Layout.preferredWidth: 110
                    model: app.efforts
                    onActivated: app.currentEffort = currentText
                }
            }
        }

        ListView {
            id: messageList
            Layout.fillWidth: true
            Layout.fillHeight: true
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
            onCountChanged: positionViewAtEnd()
        }

        Rectangle {
            Layout.fillWidth: true
            color: theme.bg
            implicitHeight: composer.implicitHeight + 24

            Rectangle {
                width: parent.width
                height: 1
                color: theme.border
            }

            ColumnLayout {
                id: composer
                anchors.fill: parent
                anchors.margins: 12
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
                                onClicked: app.openExternal(modelData)

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

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    AppButton {
                        text: "Anexar"
                        icon.source: "qrc:/resources/icons/paperclip.svg"
                        onClicked: app.attachFiles()
                    }

                    AppButton {
                        text: ""
                        icon.source: app.recording
                                     ? "qrc:/resources/icons/stop.svg"
                                     : "qrc:/resources/icons/mic.svg"
                        primary: app.recording
                        danger: app.recording
                        implicitWidth: 40
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
                        ghost: true
                        implicitWidth: 40
                        onClicked: commandsMenu.popup()
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 84

                        TextArea {
                            id: input
                            placeholderText: "Escreva sua mensagem... (Enter envia, Shift+Enter quebra linha)"
                            wrapMode: TextArea.Wrap
                            font.pointSize: theme.chatFontSize
                            onTextChanged: view.updateLinks()

                            background: Rectangle {
                                radius: 12
                                color: theme.dark ? "#2a2a2a" : "#f7f7f8"
                                border.width: composerDrop.containsDrag ? 2 : 1
                                border.color: composerDrop.containsDrag ? theme.accent : theme.border
                            }

                            Keys.onPressed: (event) => {
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
                                        var text = input.text
                                        input.text = ""
                                        app.send(text)
                                    }
                                }
                            }
                        }
                    }

                    AppButton {
                        text: app.streaming ? "Parar" : "Enviar"
                        icon.source: app.streaming
                                     ? "qrc:/resources/icons/stop.svg"
                                     : "qrc:/resources/icons/send.svg"
                        primary: !app.streaming
                        danger: app.streaming
                        onClicked: {
                            if (app.streaming) {
                                app.stop()
                            } else {
                                var text = input.text
                                input.text = ""
                                app.send(text)
                            }
                        }
                    }
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
                onTriggered: {
                    input.text = modelData.prompt
                    input.forceActiveFocus()
                    app.logClientActivity("command_use", modelData.name)
                }
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
            modelCombo.currentIndex = Math.max(0, modelCombo.model.indexOf(app.currentModel))
            effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
        }

        function onEffortsChanged() {
            effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
        }
    }

    Component.onCompleted: {
        modelCombo.currentIndex = Math.max(0, modelCombo.model.indexOf(app.currentModel))
        effortCombo.currentIndex = Math.max(0, effortCombo.model.indexOf(app.currentEffort))
    }
}
