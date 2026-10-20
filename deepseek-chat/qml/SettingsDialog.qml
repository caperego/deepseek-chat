import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "settingsDialog"

    modal: true
    width: parent ? Math.min(800, parent.width - 48) : 800
    height: parent ? Math.min(800, parent.height - 48) : 800
    title: "Configurações"
    padding: 0
    font.pointSize: theme.fontSize
    header: Item { visible: false; implicitHeight: 0 }

    property string connectionStatus: ""
    property string statusText: ""
    property int sectionIndex: 0
    readonly property var sectionNames: ["Aparência", "Áudio", "Perfil", "Conexão", "Comandos", "Plugins MCP", "Sobre"]
    QtObject { id: themeCombo; property int currentIndex: 0 }

    function openPlugins() {
        sectionIndex = 5
        open()
    }

    function loadSettings() {
        var settings = app.currentSettings()
        apiUrlField.text = settings.apiUrl
        themeCombo.currentIndex = settings.theme === "dark" ? 1
                                : (settings.theme === "system" ? 2 : 0)
        fontSpin.value = settings.fontSize
        chatFontSpin.value = settings.chatFontSize
        sidebarFontSpin.value = settings.sidebarFontSize
        codeFontSpin.value = settings.codeFontSize
        uiFamilyCombo.currentIndex = Math.max(0, uiFamilyCombo.model.indexOf(settings.fontFamily))
        chatFamilyCombo.currentIndex = Math.max(0, chatFamilyCombo.model.indexOf(settings.chatFontFamily))
        uiFamilyCombo.editText = settings.fontFamily
        chatFamilyCombo.editText = settings.chatFontFamily
        enterToSendCheck.checked = settings.enterToSend
        showReasoningCheck.checked = settings.showReasoning
        notifyCheck.checked = settings.notifyOnFinish
        closeToTrayCheck.checked = settings.closeToTray
        globalInstructionsArea.text = settings.globalInstructions
        defaultPermissionCombo.currentIndex =
                Math.max(0, ["ask", "auto", "full"].indexOf(settings.permissionMode))
        var modelIndex = defaultModelCombo.model.indexOf(settings.defaultModel)
        if (modelIndex >= 0)
            defaultModelCombo.currentIndex = modelIndex
        ttsEnabledCheck.checked = settings.ttsEnabled
        var voiceIndex = ttsVoiceCombo.model.indexOf(settings.ttsVoice)
        ttsVoiceCombo.currentIndex = voiceIndex
        ttsRateSpin.value = settings.ttsRate
        sttEnabledCheck.checked = settings.sttEnabled
        sttCommandField.text = settings.sttCommand
        flashInputHitSpin.value = settings.flashInputHit
        flashInputMissSpin.value = settings.flashInputMiss
        flashOutputSpin.value = settings.flashOutput
        proInputHitSpin.value = settings.proInputHit
        proInputMissSpin.value = settings.proInputMiss
        proOutputSpin.value = settings.proOutput
        peakCheck.checked = settings.peakEnabled
        displayNameField.text = app.displayName
        connectionStatus = ""
        statusText = ""
    }

    function saveAll() {
        app.saveSettings({
            "apiUrl": apiUrlField.text,
            "theme": themeCombo.currentIndex === 1 ? "dark"
                     : (themeCombo.currentIndex === 2 ? "system" : "light"),
            "fontSize": fontSpin.value,
            "chatFontSize": chatFontSpin.value,
            "sidebarFontSize": sidebarFontSpin.value,
            "codeFontSize": codeFontSpin.value,
            "fontFamily": uiFamilyCombo.editText,
            "chatFontFamily": chatFamilyCombo.editText,
            "enterToSend": enterToSendCheck.checked,
            "showReasoning": showReasoningCheck.checked,
            "notifyOnFinish": notifyCheck.checked,
            "closeToTray": closeToTrayCheck.checked,
            "globalInstructions": globalInstructionsArea.text,
            "permissionMode": ["ask", "auto", "full"][defaultPermissionCombo.currentIndex],
            "defaultModel": defaultModelCombo.currentText,
            "ttsEnabled": ttsEnabledCheck.checked,
            "ttsVoice": ttsVoiceCombo.currentIndex >= 0 ? ttsVoiceCombo.currentText : "",
            "ttsRate": ttsRateSpin.value,
            "sttEnabled": sttEnabledCheck.checked,
            "sttCommand": sttCommandField.text,
            "flashInputHit": flashInputHitSpin.value,
            "flashInputMiss": flashInputMissSpin.value,
            "flashOutput": flashOutputSpin.value,
            "proInputHit": proInputHitSpin.value,
            "proInputMiss": proInputMissSpin.value,
            "proOutput": proOutputSpin.value,
            "peakEnabled": peakCheck.checked
        })
    }

    onOpened: loadSettings()

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 188
                Layout.fillHeight: true
                color: "transparent"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 22
                    anchors.bottomMargin: 12
                    spacing: 4
                    Text {
                        text: "Configurações"
                        color: theme.text
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 1
                        font.weight: Font.DemiBold
                        Layout.leftMargin: 12
                        Layout.bottomMargin: 14
                    }
                    ListView {
                        id: settingsNavigation
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        ScrollBar.vertical: ScrollBar {}
                        model: dialog.sectionNames
                        delegate: SidebarAction {
                            required property int index
                            required property string modelData
                            width: settingsNavigation.width
                            text: index === 5 ? "Plugins" : modelData
                            font.pointSize: theme.fontSize
                            icon.source: ["qrc:/resources/icons/eye.svg", "qrc:/resources/icons/mic.svg",
                                          "qrc:/resources/icons/chat.svg", "qrc:/resources/icons/globe.svg",
                                          "qrc:/resources/icons/terminal.svg", "qrc:/resources/icons/plugin.svg",
                                          "qrc:/resources/icons/file.svg"][index]
                            background: Rectangle {
                                radius: 12
                                color: dialog.sectionIndex === index || parent.hovered ? theme.selected : "transparent"
                                border.width: parent.visualFocus ? 2 : 0
                                border.color: theme.accent
                            }
                            onClicked: dialog.sectionIndex = index
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 14
                    Layout.topMargin: 14
                    Text {
                        Layout.fillWidth: true
                        text: dialog.sectionNames[dialog.sectionIndex]
                        color: theme.text
                        font.family: theme.fontFamily
                        font.pointSize: theme.fontSize + 1
                        font.weight: Font.DemiBold
                    }
                    AppButton {
                        icon.source: "qrc:/resources/icons/close.svg"
                        ghost: true
                        implicitWidth: 32
                        implicitHeight: 32
                        leftPadding: 5
                        rightPadding: 5
                        ToolTip.text: "Fechar"
                        ToolTip.visible: hovered
                        onClicked: dialog.close()
                    }
                }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: dialog.sectionIndex

            Item {
                ScrollView {
                    id: appearanceScroll
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(620, appearanceScroll.availableWidth)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Text { Layout.columnSpan: 2; text: "Tema"; color: theme.text }
                        RowLayout {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: ["Claro", "Escuro", "Sistema"]
                                delegate: Button {
                                    required property int index
                                    required property string modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 82
                                    Accessible.name: "Tema " + modelData
                                    contentItem: Column {
                                        spacing: 8
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: ["☼", "☾", "◐"][index]
                                            font.pixelSize: 24
                                            color: theme.text
                                        }
                                        Text {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            text: modelData
                                            color: theme.text
                                            font.family: theme.fontFamily
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 16
                                        color: themeCombo.currentIndex === index || parent.hovered ? theme.sidebar : "transparent"
                                        border.color: themeCombo.currentIndex === index ? theme.muted : theme.border
                                        border.width: parent.visualFocus ? 2 : 1
                                    }
                                    onClicked: themeCombo.currentIndex = index
                                }
                            }
                        }

                        Text {
                            Layout.columnSpan: 2
                            text: "Tipografia por área"
                            color: theme.text
                            font.bold: true
                        }
                        Text { text: "Família dos menus:"; color: theme.text }
                        HeaderComboBox {
                            id: uiFamilyCombo
                            editable: true
                            Layout.fillWidth: true
                            model: Qt.fontFamilies()
                        }
                        Text { text: "Menus e diálogos (pt):"; color: theme.text }
                        AppSpinBox {
                            id: fontSpin
                            from: 9
                            to: 22
                            Layout.preferredWidth: 120
                        }

                        Text { text: "Projetos e conversas (pt):"; color: theme.text }
                        AppSpinBox {
                            id: sidebarFontSpin
                            from: 9
                            to: 22
                            Layout.preferredWidth: 120
                        }
                        Text { text: "Família das mensagens:"; color: theme.text }
                        HeaderComboBox {
                            id: chatFamilyCombo
                            editable: true
                            Layout.fillWidth: true
                            model: Qt.fontFamilies()
                        }
                        Text { text: "Mensagens e escrita (pt):"; color: theme.text }
                        AppSpinBox {
                            id: chatFontSpin
                            from: 9
                            to: 28
                            Layout.preferredWidth: 120
                        }
                        Text { text: "Código e saídas (pt):"; color: theme.text }
                        AppSpinBox {
                            id: codeFontSpin
                            from: 9
                            to: 28
                            Layout.preferredWidth: 120
                        }
                        Text {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            text: "Cada tamanho altera apenas sua área. Arraste a divisória lateral para ajustar a largura; use ‹ para recolher a navegação."
                            color: theme.muted
                            wrapMode: Text.Wrap
                            font.pointSize: 10
                        }

                        AppCheckBox {
                            id: enterToSendCheck
                            Layout.columnSpan: 2
                            text: "Enter envia a mensagem"
                        }

                        AppCheckBox {
                            id: showReasoningCheck
                            Layout.columnSpan: 2
                            text: "Mostrar raciocínio dos modelos"
                        }

                        AppCheckBox {
                            id: notifyCheck
                            Layout.columnSpan: 2
                            text: "Notificar quando uma resposta terminar"
                        }

                        AppCheckBox {
                            id: closeToTrayCheck
                            Layout.columnSpan: 2
                            text: "Fechar para a bandeja (continuar em segundo plano)"
                        }

                        Text {
                            Layout.columnSpan: 2
                            text: "Instruções globais (aplicadas a todas as conversas):"
                            color: theme.text
                        }

                        ScrollView {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120

                            CopyableTextArea {
                                id: globalInstructionsArea
                                wrapMode: TextArea.Wrap
                                placeholderText: "Ex.: responda sempre em português; prefiro respostas curtas."
                            }
                        }
                    }
                }
            }

            Item {
                ScrollView {
                    id: audioScroll
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(680, audioScroll.availableWidth)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Item {}
                        AppCheckBox {
                            id: ttsEnabledCheck
                            text: "Ler as respostas em voz alta (TTS)"
                        }

                        Text { text: "Voz:"; color: theme.text }
                        HeaderComboBox {
                            id: ttsVoiceCombo
                            Layout.fillWidth: true
                            model: app.ttsVoices()
                        }

                        Text { text: "Velocidade:"; color: theme.text }
                        AppSpinBox {
                            id: ttsRateSpin
                            from: -100
                            to: 100
                            stepSize: 10
                            Layout.preferredWidth: 140
                        }

                        Item {}
                        RowLayout {
                            AppButton {
                                text: "Testar voz"
                                icon.source: "qrc:/resources/icons/mic.svg"
                                onClicked: app.testVoice("Olá! Esta é a voz do DeepSeek Chat.")
                            }

                            AppButton {
                                text: "Parar"
                                icon.source: "qrc:/resources/icons/stop.svg"
                                ghost: true
                                onClicked: app.stopSpeaking()
                            }

                            Text {
                                Layout.fillWidth: true
                                text: app.ttsStatus
                                color: theme.muted
                                wrapMode: Text.Wrap
                            }
                        }

                        Item {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.muted
                            text: "Linux: sudo pacman -S speech-dispatcher espeak-ng. "
                                  + "Windows/macOS usam a voz nativa do sistema."
                        }

                        Item {}
                        AppCheckBox {
                            id: sttEnabledCheck
                            text: "Ditado por voz (gravar e transcrever com Whisper)"
                        }

                        Text { text: "Comando:"; color: theme.text }
                        AppTextField {
                            id: sttCommandField
                            Layout.fillWidth: true
                            placeholderText: "whisper-cli -m modelo.bin -f {file} -l pt -nt"
                        }

                        Item {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.muted
                            text: "Use {file} para o áudio gravado (WAV). "
                                  + "Linux: sudo pacman -S whisper-cpp — Windows: whisper-cli.exe. "
                                  + "Baixe um modelo (ex.: ggml-small.bin)."
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 14

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12

                        Text { text: "Nome exibido:"; color: theme.text }
                        AppTextField {
                            id: displayNameField
                            Layout.fillWidth: true
                        }
                    }

                    AppButton {
                        text: "Salvar nome"
                        icon.source: "qrc:/resources/icons/save.svg"
                        onClicked: app.changeDisplayName(displayNameField.text)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.border
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12

                        Text { text: "Senha atual:"; color: theme.text }
                        AppTextField {
                            id: oldPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }

                        Text { text: "Nova senha:"; color: theme.text }
                        AppTextField {
                            id: newPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }

                        Text { text: "Confirmar:"; color: theme.text }
                        AppTextField {
                            id: confirmPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }
                    }

                    AppButton {
                        text: "Alterar senha"
                        icon.source: "qrc:/resources/icons/pencil.svg"
                        onClicked: {
                            if (newPasswordField.text !== confirmPasswordField.text) {
                                dialog.statusText = "As senhas novas não conferem."
                                return
                            }
                            app.changePassword(oldPasswordField.text, newPasswordField.text)
                            oldPasswordField.text = ""
                            newPasswordField.text = ""
                            confirmPasswordField.text = ""
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            Item {
                ScrollView {
                    id: connectionScroll
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(680, connectionScroll.availableWidth)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Text { text: "URL da API:"; color: theme.text }
                        AppTextField {
                            id: apiUrlField
                            Layout.fillWidth: true
                            placeholderText: "https://SEU_DOMINIO/deepseek"
                        }

                        Item {}
                        RowLayout {
                            AppButton {
                                text: "Testar conexão"
                                icon.source: "qrc:/resources/icons/refresh.svg"
                                onClicked: {
                                    dialog.connectionStatus = "Testando..."
                                    app.testConnection(apiUrlField.text)
                                }
                            }

                            Text {
                                text: dialog.connectionStatus
                                color: theme.muted
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                        }

                        Text { text: "Modelo padrão:"; color: theme.text }
                        HeaderComboBox {
                            id: defaultModelCombo
                            Layout.fillWidth: true
                            model: app.models.map(function (m) { return m.id })
                        }

                        Text { text: "Permissões padrão da IA:"; color: theme.text }
                        HeaderComboBox {
                            id: defaultPermissionCombo
                            Layout.fillWidth: true
                            model: ["Solicitar aprovação", "Aprovar por mim", "Acesso completo"]
                        }

                        Item {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.muted
                            text: "Modo usado em projetos novos ou sem permissão definida. "
                                  + "Cada projeto pode ter o próprio modo em "
                                  + "Projeto → Configurações → Permissões da IA."
                        }

                        Item {}
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: theme.muted
                            text: "A chave da API DeepSeek e as credenciais do banco ficam "
                                  + "no servidor (config.php). O aplicativo usa apenas o token "
                                  + "de acesso."
                        }

                        Text {
                            Layout.columnSpan: 2
                            text: "Preços (US$ por 1M de tokens)"
                            font.bold: true
                            color: theme.text
                        }

                        Text { text: "Flash — entrada (cache hit):"; color: theme.text }
                        AppDoubleSpinBox {
                            id: flashInputHitSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.001
                            Layout.fillWidth: true
                        }

                        Text { text: "Flash — entrada (cache miss):"; color: theme.text }
                        AppDoubleSpinBox {
                            id: flashInputMissSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "Flash — saída:"; color: theme.text }
                        AppDoubleSpinBox {
                            id: flashOutputSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — entrada (cache hit):"; color: theme.text }
                        AppDoubleSpinBox {
                            id: proInputHitSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.001
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — entrada (cache miss):"; color: theme.text }
                        AppDoubleSpinBox {
                            id: proInputMissSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — saída:"; color: theme.text }
                        AppDoubleSpinBox {
                            id: proOutputSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Item {}
                        AppCheckBox {
                            id: peakCheck
                            text: "Aplicar preço de pico (dobro) no horário de pico"
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10

                    Text {
                        text: "Comandos globais ficam disponíveis em qualquer conversa."
                        color: theme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        AppButton {
                            text: "Adicionar"
                            icon.source: "qrc:/resources/icons/plus.svg"
                            onClicked: globalCommandEditor.openFor(-1, "", "")
                        }

                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: app.globalCommands
                        spacing: 4

                        delegate: Rectangle {
                            required property var modelData

                            width: ListView.view.width
                            height: 42
                            radius: 8
                            color: globalMouse.containsMouse ? theme.selected : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: "/" + modelData.name
                                color: theme.text
                            }

                            RowLayout {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                spacing: 6

                                AppButton {
                                    text: "Editar"
                                    icon.source: "qrc:/resources/icons/pencil.svg"
                                    onClicked: globalCommandEditor.openFor(modelData.id,
                                                                           modelData.name,
                                                                           modelData.prompt)
                                }

                                AppButton {
                                    text: "Remover"
                                    icon.source: "qrc:/resources/icons/trash.svg"
                                    danger: true
                                    onClicked: app.removeGlobalCommand(modelData.id)
                                }
                            }

                            MouseArea {
                                id: globalMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: theme.muted
                        text: "Servidores MCP (Model Context Protocol) são configurados em "
                              + "mcp.json, na mesma pasta do config.ini. As ferramentas deles "
                              + "ficam disponíveis para a IA (prefixo mcp__) e pedem aprovação "
                              + "conforme o modo de permissão."
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        AppButton {
                            text: "Adicionar plugin"
                            icon.source: "qrc:/resources/icons/plus.svg"
                            primary: true
                            onClicked: pluginEditor.open()
                        }

                        AppButton {
                            text: "Abrir mcp.json"
                            icon.source: "qrc:/resources/icons/file.svg"
                            onClicked: app.openMcpFile()
                        }

                        AppButton {
                            text: "Recarregar servidores"
                            icon.source: "qrc:/resources/icons/refresh.svg"
                            onClicked: app.reloadMcp()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: app.mcpServers.length === 0
                        text: "Nenhum plugin MCP configurado. Abra mcp.json para adicionar um servidor."
                        color: theme.muted
                        wrapMode: Text.Wrap
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: app.mcpServers
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            implicitHeight: pluginRow.implicitHeight + 24
                            radius: 12
                            color: theme.sidebar
                            RowLayout {
                                id: pluginRow
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                Rectangle {
                                    width: 8; height: 8; radius: 4
                                    color: modelData.ready ? "#37a46c" : theme.muted
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        color: theme.text
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.status + " · " + modelData.tools + " ferramentas"
                                        color: theme.muted
                                        wrapMode: Text.Wrap
                                    }
                                }
                                AppButton {
                                    text: modelData.enabled ? "Desativar" : "Ativar"
                                    ghost: true
                                    onClicked: app.setMcpServerEnabled(modelData.name, !modelData.enabled)
                                }
                            }
                        }
                    }

                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 10

                    Image {
                        source: "qrc:/resources/deepseek-128.png"
                        sourceSize.width: 96
                        sourceSize.height: 96
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "DeepSeek Chat"
                        font.pixelSize: 24
                        font.bold: true
                        color: theme.text
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Versão " + app.appVersion
                        color: theme.muted
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Text {
                        text: "Desenvolvido por <b>Carlos Alberto Perego</b> — "
                              + "<a href=\"https://carlos.pw\">carlos.pw</a>"
                        textFormat: Text.RichText
                        color: theme.muted
                        horizontalAlignment: Text.AlignHCenter
                        Layout.alignment: Qt.AlignHCenter
                        onLinkActivated: (link) => app.openExternal(link)
                    }

                    RowLayout {
                        spacing: 8
                        Layout.alignment: Qt.AlignHCenter

                        AppButton {
                            text: "Verificar atualizações"
                            icon.source: "qrc:/resources/icons/refresh.svg"
                            onClicked: app.checkForUpdates()
                        }

                        AppButton {
                            visible: app.updateAvailable
                            text: "Baixar atualização"
                            icon.source: "qrc:/resources/icons/save.svg"
                            primary: true
                            onClicked: app.downloadUpdate()
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: theme.muted
                        text: app.updateStatus
                    }

                    Text {
                        text: "Cliente desktop para a API DeepSeek via backend PHP.\n"
                              + "Projetos, arquivos, comandos, custos e histórico no servidor."
                        color: theme.text
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        Layout.alignment: Qt.AlignHCenter
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }

            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10

            Text {
                text: dialog.statusText
                color: theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            AppButton {
                text: "Abrir config.ini"
                icon.source: "qrc:/resources/icons/file.svg"
                ghost: true
                onClicked: app.openConfigFile()
            }

            AppButton {
                text: "Salvar"
                icon.source: "qrc:/resources/icons/save.svg"
                primary: true
                onClicked: {
                    dialog.saveAll()
                    dialog.statusText = "Configurações salvas."
                }
            }

            AppButton {
                text: "Fechar"
                icon.source: "qrc:/resources/icons/close.svg"
                ghost: true
                onClicked: dialog.close()
            }
        }
    }

    AppDialog {
        id: pluginEditor
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent ? Math.min(480, parent.width - 48) : 480
        title: "Adicionar plugin MCP"
        modal: true
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "Configure um servidor MCP stdio instalado no computador. Ao adicionar, o executável será iniciado."
                color: theme.muted
                wrapMode: Text.Wrap
            }
            AppTextField { id: pluginName; Layout.fillWidth: true; placeholderText: "Nome (letras, números ou _)" }
            AppTextField { id: pluginCommand; Layout.fillWidth: true; placeholderText: "Executável ou caminho do servidor" }
            AppTextField { id: pluginArguments; Layout.fillWidth: true; placeholderText: "Argumentos (opcional)" }
            Text {
                Layout.fillWidth: true
                text: "Separe os argumentos por espaços; use aspas duplas para caminhos com espaços. Variáveis de ambiente e pasta de trabalho podem ser definidas em mcp.json."
                color: theme.muted
                wrapMode: Text.Wrap
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                AppButton { text: "Cancelar"; ghost: true; onClicked: pluginEditor.close() }
                AppButton {
                    text: "Adicionar"
                    primary: true
                    enabled: pluginName.text.trim().length > 0 && pluginCommand.text.trim().length > 0
                    onClicked: {
                        if (app.addMcpServer(pluginName.text, pluginCommand.text, pluginArguments.text)) {
                            pluginName.clear(); pluginCommand.clear(); pluginArguments.clear(); pluginEditor.close()
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: globalCommandEditor
        objectName: "globalCommandEditor"
        modal: true
        anchors.centerIn: parent
        width: 460
        font.pointSize: theme.fontSize
        title: globalCommandId > 0 ? "Editar comando" : "Novo comando"
        standardButtons: Dialog.Ok | Dialog.Cancel

        property int globalCommandId: -1

        function openFor(id, name, prompt) {
            globalCommandId = id
            globalCommandName.text = name
            globalCommandPrompt.text = prompt
            open()
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            AppTextField {
                id: globalCommandName
                Layout.fillWidth: true
                placeholderText: "nome (sem /)"
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 160

                CopyableTextArea {
                    id: globalCommandPrompt
                    wrapMode: TextArea.Wrap
                    placeholderText: "prompt"
                }
            }
        }

        onAccepted: {
            if (globalCommandId > 0)
                app.updateGlobalCommand(globalCommandId, globalCommandName.text,
                                        globalCommandPrompt.text)
            else
                app.addGlobalCommand(globalCommandName.text, globalCommandPrompt.text)
        }
    }

    Connections {
        target: app

        function onConnectionTestDone(message) {
            dialog.connectionStatus = message
        }

        function onUserChanged() {
            displayNameField.text = app.displayName
        }
    }
}
