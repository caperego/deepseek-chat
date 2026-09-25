import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog

    modal: true
    width: 800
    height: 620
    title: "Configurações"
    padding: 0
    font.pointSize: theme.fontSize

    property string connectionStatus: ""
    property string statusText: ""

    function loadSettings() {
        var settings = app.currentSettings()
        apiUrlField.text = settings.apiUrl
        themeCombo.currentIndex = settings.theme === "dark" ? 1
                                : (settings.theme === "system" ? 2 : 0)
        fontSpin.value = settings.fontSize
        chatFontSpin.value = settings.chatFontSize
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

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: "Aparência" }
            TabButton { text: "Áudio" }
            TabButton { text: "Perfil" }
            TabButton { text: "Conexão" }
            TabButton { text: "Comandos" }
            TabButton { text: "MCP" }
            TabButton { text: "Sobre" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(620, parent.width)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Text { text: "Tema:"; color: theme.text }
                        ComboBox {
                            id: themeCombo
                            Layout.fillWidth: true
                            model: ["Claro (estilo ChatGPT)", "Escuro", "Sistema (padrão do SO)"]
                        }

                        Text { text: "Fonte da interface:"; color: theme.text }
                        SpinBox {
                            id: fontSpin
                            from: 9
                            to: 22
                            Layout.preferredWidth: 120
                        }

                        Text { text: "Fonte do chat:"; color: theme.text }
                        SpinBox {
                            id: chatFontSpin
                            from: 9
                            to: 28
                            Layout.preferredWidth: 120
                        }

                        Item {}
                        CheckBox {
                            id: enterToSendCheck
                            text: "Enter envia a mensagem"
                        }

                        Item {}
                        CheckBox {
                            id: showReasoningCheck
                            text: "Mostrar raciocínio dos modelos"
                        }

                        Item {}
                        CheckBox {
                            id: notifyCheck
                            text: "Notificar quando uma resposta terminar"
                        }

                        Item {}
                        CheckBox {
                            id: closeToTrayCheck
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

                            TextArea {
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
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(680, parent.width)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Item {}
                        CheckBox {
                            id: ttsEnabledCheck
                            text: "Ler as respostas em voz alta (TTS)"
                        }

                        Text { text: "Voz:"; color: theme.text }
                        ComboBox {
                            id: ttsVoiceCombo
                            Layout.fillWidth: true
                            model: app.ttsVoices()
                        }

                        Text { text: "Velocidade:"; color: theme.text }
                        SpinBox {
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
                        CheckBox {
                            id: sttEnabledCheck
                            text: "Ditado por voz (gravar e transcrever com Whisper)"
                        }

                        Text { text: "Comando:"; color: theme.text }
                        TextField {
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
                        TextField {
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
                        TextField {
                            id: oldPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }

                        Text { text: "Nova senha:"; color: theme.text }
                        TextField {
                            id: newPasswordField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                        }

                        Text { text: "Confirmar:"; color: theme.text }
                        TextField {
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
                    anchors.fill: parent
                    anchors.margins: 20

                    GridLayout {
                        width: Math.min(680, parent.width)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 14

                        Text { text: "URL da API:"; color: theme.text }
                        TextField {
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
                        ComboBox {
                            id: defaultModelCombo
                            Layout.fillWidth: true
                            model: app.models.map(function (m) { return m.id })
                        }

                        Text { text: "Permissões padrão da IA:"; color: theme.text }
                        ComboBox {
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
                        DoubleSpinBox {
                            id: flashInputHitSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.001
                            Layout.fillWidth: true
                        }

                        Text { text: "Flash — entrada (cache miss):"; color: theme.text }
                        DoubleSpinBox {
                            id: flashInputMissSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "Flash — saída:"; color: theme.text }
                        DoubleSpinBox {
                            id: flashOutputSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — entrada (cache hit):"; color: theme.text }
                        DoubleSpinBox {
                            id: proInputHitSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.001
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — entrada (cache miss):"; color: theme.text }
                        DoubleSpinBox {
                            id: proInputMissSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Text { text: "V4-Pro — saída:"; color: theme.text }
                        DoubleSpinBox {
                            id: proOutputSpin
                            from: 0
                            to: 1000
                            decimals: 4
                            stepSize: 0.01
                            Layout.fillWidth: true
                        }

                        Item {}
                        CheckBox {
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

                    RowLayout {
                        spacing: 8

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

                    Item { Layout.fillHeight: true }
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

    Dialog {
        id: globalCommandEditor
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

            TextField {
                id: globalCommandName
                Layout.fillWidth: true
                placeholderText: "nome (sem /)"
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 160

                TextArea {
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
