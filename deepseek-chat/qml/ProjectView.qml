import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: view
    property int editingMemoryId: 0

    function loadFields() {
        nameField.text = app.activeProjectName
        descriptionField.text = app.activeProjectDescription
        workdirField.text = app.activeProjectWorkdir
        promptField.text = app.activeProjectPrompt
        var modelIndex = projectModelCombo.model.indexOf(app.activeProjectModel)
        if (modelIndex >= 0)
            projectModelCombo.currentIndex = modelIndex
        var effortIndex = projectEffortCombo.model.indexOf(app.activeProjectEffort)
        if (effortIndex >= 0)
            projectEffortCombo.currentIndex = effortIndex
        var permissionIndex = ["ask", "auto", "full"].indexOf(app.activeProjectPermissionMode)
        projectPermissionCombo.currentIndex = permissionIndex >= 0 ? permissionIndex : 0
        editingMemoryId = 0
        memoryField.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 62
            color: theme.bg

            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 18

                Text {
                    Layout.fillWidth: true
                    text: app.activeProjectName.length > 0 ? app.activeProjectName : "Projeto"
                    font.bold: true
                    font.pixelSize: 18
                    color: theme.text
                    elide: Text.ElideRight
                }

                AppButton {
                    text: "Nova conversa"
                    icon.source: "qrc:/resources/icons/chat.svg"
                    onClicked: app.newConversation(app.activeProjectId)
                }

                AppButton {
                    text: "Abrir pasta"
                    icon.source: "qrc:/resources/icons/open.svg"
                    onClicked: app.openWorkdir()
                }
            }
        }

        AppTabBar {
            id: tabs
            Layout.fillWidth: true

            AppTabButton { text: "Memórias" }
            AppTabButton { text: "Instruções" }
            AppTabButton { text: "Arquivos" }
            AppTabButton { text: "Comandos" }
            AppTabButton { text: "Configurações" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: "Memórias deste projeto entram no contexto das próximas conversas. Salve apenas informações que deseja reutilizar."
                        color: theme.muted
                        wrapMode: Text.Wrap
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 100
                        CopyableTextArea {
                            id: memoryField
                            wrapMode: TextArea.Wrap
                            placeholderText: "Decisão, preferência ou fato importante deste projeto..."
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: view.editingMemoryId > 0 ? "Atualizar memória" : "Adicionar memória"
                            primary: true
                            onClicked: {
                                if (memoryField.text.trim().length === 0)
                                    return
                                var saved = view.editingMemoryId > 0
                                            ? app.updateProjectMemory(view.editingMemoryId, memoryField.text)
                                            : app.addProjectMemory(memoryField.text)
                                if (saved) {
                                    view.editingMemoryId = 0
                                    memoryField.text = ""
                                }
                            }
                        }
                        AppButton {
                            visible: view.editingMemoryId > 0
                            text: "Cancelar edição"
                            ghost: true
                            onClicked: {
                                view.editingMemoryId = 0
                                memoryField.text = ""
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        id: memoriesList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: app.projectMemories
                        delegate: Rectangle {
                            required property var modelData
                            width: memoriesList.width
                            height: Math.max(64, memoryText.implicitHeight + 20)
                            radius: 8
                            color: theme.card
                            border.color: theme.border

                            Text {
                                id: memoryText
                                anchors.left: parent.left
                                anchors.right: memoryActions.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                text: modelData.content
                                color: theme.text
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                id: memoryActions
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                AppButton {
                                    text: "Editar"
                                    ghost: true
                                    onClicked: {
                                        view.editingMemoryId = modelData.id
                                        memoryField.text = modelData.content
                                    }
                                }
                                AppButton {
                                    text: "Excluir"
                                    danger: true
                                    ghost: true
                                    onClicked: app.deleteProjectMemory(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    Text {
                        text: "Instruções enviadas como system prompt em toda conversa deste projeto."
                        color: theme.muted
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        CopyableTextArea {
                            id: promptField
                            wrapMode: TextArea.Wrap
                            placeholderText: "Ex.: Você é um assistente especialista em Python. Responda em português."
                        }
                    }

                    AppButton {
                        text: "Salvar instruções"
                        icon.source: "qrc:/resources/icons/save.svg"
                        primary: true
                        Layout.alignment: Qt.AlignRight
                        onClicked: view.saveProject()
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        Text {
                            Layout.fillWidth: true
                            color: theme.muted
                            elide: Text.ElideMiddle
                            text: app.activeProjectWorkdir.length > 0
                                  ? "Pasta: " + app.activeProjectWorkdir
                                  : "Nenhuma pasta de trabalho definida no projeto."
                        }

                        AppButton {
                            text: "Alterar pasta"
                            icon.source: "qrc:/resources/icons/folder.svg"
                            onClicked: view.changeWorkdir()
                        }

                        AppButton {
                            text: "Atualizar"
                            icon.source: "qrc:/resources/icons/refresh.svg"
                            onClicked: app.refreshWorkdirFiles()
                        }

                        AppButton {
                            text: "Abrir pasta"
                            icon.source: "qrc:/resources/icons/open.svg"
                            onClicked: app.openWorkdir()
                        }
                    }

                    ListView {
                        id: workdirList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: app.workdirFiles
                        spacing: 4

                        delegate: Rectangle {
                            required property var modelData

                            width: workdirList.width
                            height: 42
                            radius: 8
                            color: workMouse.containsMouse ? theme.selected : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10 + (modelData.depth ? modelData.depth * 14 : 0)
                                anchors.right: workActions.left
                                anchors.rightMargin: 8
                                text: (modelData.kind === "image" ? "[img] " : "")
                                      + (modelData.isDir ? modelData.name + "/" : modelData.name)
                                color: theme.text
                                elide: Text.ElideMiddle
                            }

                            RowLayout {
                                id: workActions
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                spacing: 6

                                Text {
                                    visible: !modelData.isDir
                                    text: modelData.sizeText
                                    color: theme.muted
                                    font.pixelSize: 12
                                }

                                AppButton {
                                    visible: !modelData.isDir
                                    text: "Anexar"
                                    icon.source: "qrc:/resources/icons/paperclip.svg"
                                    onClicked: app.attachWorkdirFile(modelData.path)
                                }

                                AppButton {
                                    visible: !modelData.isDir && modelData.kind !== "image"
                                    text: "Painel"
                                    icon.source: "qrc:/resources/icons/eye.svg"
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Editar arquivo e consultar prévia, diferenças e versões"
                                    onClicked: app.openArtifact(modelData.path)
                                }

                                AppButton {
                                    text: "Abrir"
                                    icon.source: "qrc:/resources/icons/open.svg"
                                    onClicked: app.openPath(modelData.path)
                                }
                            }

                            MouseArea {
                                id: workMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: theme.border
                    }

                    RowLayout {
                        AppButton {
                            text: "Adicionar arquivos ao projeto"
                            icon.source: "qrc:/resources/icons/plus.svg"
                            onClicked: app.addProjectFiles()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        id: filesList
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        clip: true
                        model: app.projectItems
                        spacing: 4

                        delegate: Rectangle {
                            required property var modelData

                            width: filesList.width
                            height: 42
                            radius: 8
                            color: fileMouse.containsMouse ? theme.selected : "transparent"

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                text: (modelData.kind === "image" ? "[img] " : "") + modelData.name
                                color: theme.text
                                elide: Text.ElideRight
                            }

                            RowLayout {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                spacing: 6

                                AppButton {
                                    text: "Anexar"
                                    icon.source: "qrc:/resources/icons/paperclip.svg"
                                    onClicked: app.attachProjectItem(modelData.id)
                                }

                                AppButton {
                                    text: "Remover"
                                    icon.source: "qrc:/resources/icons/trash.svg"
                                    danger: true
                                    onClicked: app.removeProjectItem(modelData.id)
                                }
                            }

                            MouseArea {
                                id: fileMouse
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
                    anchors.margins: 18
                    spacing: 10

                    RowLayout {
                        AppButton {
                            text: "Adicionar"
                            icon.source: "qrc:/resources/icons/plus.svg"
                            onClicked: commandEditor.openFor(-1, "", "")
                        }

                        Item { Layout.fillWidth: true }
                    }

                    ListView {
                        id: commandsList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: app.projectCommands
                        spacing: 4

                        delegate: Rectangle {
                            required property var modelData

                            width: commandsList.width
                            height: 42
                            radius: 8
                            color: commandMouse.containsMouse ? theme.selected : "transparent"

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
                                    onClicked: commandEditor.openFor(modelData.id,
                                                                     modelData.name,
                                                                     modelData.prompt)
                                }

                                AppButton {
                                    text: "Remover"
                                    icon.source: "qrc:/resources/icons/trash.svg"
                                    danger: true
                                    onClicked: app.removeProjectCommand(modelData.id)
                                }
                            }

                            MouseArea {
                                id: commandMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }
                }
            }

            Item {
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 18

                    GridLayout {
                        width: Math.min(640, parent.width)
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12

                        Text { text: "Nome:"; color: theme.text }
                        AppTextField {
                            id: nameField
                            Layout.fillWidth: true
                        }

                        Text { text: "Descrição:"; color: theme.text }
                        AppTextField {
                            id: descriptionField
                            Layout.fillWidth: true
                        }

                        Text { text: "Pasta de trabalho:"; color: theme.text }
                        RowLayout {
                            Layout.fillWidth: true
                            AppTextField {
                                id: workdirField
                                Layout.fillWidth: true
                            }
                            AppButton {
                                text: "..."
                                icon.source: "qrc:/resources/icons/folder.svg"
                                onClicked: workdirField.text = app.chooseDirectory("Escolha a pasta de trabalho")
                            }
                        }

                        Text { text: "Modelo padrão:"; color: theme.text }
                        HeaderComboBox {
                            id: projectModelCombo
                            Layout.fillWidth: true
                            model: app.models.map(function (m) { return m.id })
                        }

                        Text { text: "Git:"; color: theme.text }
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                id: gitStatusLabel
                                Layout.fillWidth: true
                                color: theme.muted
                                elide: Text.ElideRight
                                text: ""
                            }
                            AppButton {
                                text: "Verificar"
                                icon.source: "qrc:/resources/icons/refresh.svg"
                                onClicked: gitStatusLabel.text = app.gitStatusText()
                            }
                        }

                        Text { text: "Esforço padrão:"; color: theme.text }
                        HeaderComboBox {
                            id: projectEffortCombo
                            Layout.fillWidth: true
                            model: ["low", "high", "max"]
                        }

                        Text { text: "Permissões da IA:"; color: theme.text }
                        HeaderComboBox {
                            id: projectPermissionCombo
                            Layout.fillWidth: true
                            model: ["Solicitar aprovação", "Aprovar por mim", "Acesso completo"]
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.columnSpan: 2
                            wrapMode: Text.Wrap
                            color: theme.muted
                            text: "Solicitar aprovação: pede permissão para executar comandos, "
                                  + "gravar/baixar arquivos e acessar a internet.\n"
                                  + "Aprovar por mim: leitura e gravação automáticas dentro da "
                                  + "pasta do projeto; pede permissão para comandos, fora dela "
                                  + "e na internet.\n"
                                  + "Acesso completo: sem pedir permissão — executa comandos, "
                                  + "qualquer arquivo do computador e qualquer URL."
                        }

                        Item {}
                        RowLayout {
                            AppButton {
                                text: "Salvar projeto"
                                icon.source: "qrc:/resources/icons/save.svg"
                                primary: true
                                onClicked: view.saveProject()
                            }
                            AppButton {
                                text: "Excluir projeto"
                                icon.source: "qrc:/resources/icons/trash.svg"
                                danger: true
                                onClicked: confirmDelete.open()
                            }
                        }
                    }
                }
            }
        }
    }

    function changeWorkdir() {
        var dir = app.chooseDirectory("Escolha a pasta de trabalho")
        if (dir.length === 0)
            return
        app.saveActiveProject({
            "name": app.activeProjectName,
            "description": app.activeProjectDescription,
            "workdir": dir,
            "systemPrompt": app.activeProjectPrompt,
            "defaultModel": app.activeProjectModel,
            "defaultEffort": app.activeProjectEffort,
            "permissionMode": app.activeProjectPermissionMode
        })
    }

    function saveProject() {
        app.saveActiveProject({
            "name": nameField.text,
            "description": descriptionField.text,
            "workdir": workdirField.text,
            "systemPrompt": promptField.text,
            "defaultModel": projectModelCombo.currentText,
            "defaultEffort": projectEffortCombo.currentText,
            "permissionMode": ["ask", "auto", "full"][projectPermissionCombo.currentIndex]
        })
    }

    AppDialog {
        id: commandEditor
        modal: true
    font.pointSize: theme.fontSize
        anchors.centerIn: parent
        width: 460
        title: commandId > 0 ? "Editar comando" : "Novo comando"
        standardButtons: Dialog.Ok | Dialog.Cancel

        property int commandId: -1

        function openFor(id, name, prompt) {
            commandId = id
            commandName.text = name
            commandPrompt.text = prompt
            open()
        }

        ColumnLayout {
            width: parent.width
            spacing: 10

            AppTextField {
                id: commandName
                Layout.fillWidth: true
                placeholderText: "nome (sem /)"
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 160

                CopyableTextArea {
                    id: commandPrompt
                    wrapMode: TextArea.Wrap
                    placeholderText: "prompt"
                }
            }
        }

        onAccepted: {
            if (commandId > 0)
                app.updateProjectCommand(commandId, commandName.text, commandPrompt.text)
            else
                app.addProjectCommand(commandName.text, commandPrompt.text)
        }
    }

    AppDialog {
        id: confirmDelete
    font.pointSize: theme.fontSize
        modal: true
        anchors.centerIn: parent
        width: 400
        title: "Excluir projeto"
        standardButtons: Dialog.Yes | Dialog.No

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            color: theme.text
            text: "Excluir o projeto \"" + app.activeProjectName
                  + "\" com conversas, arquivos e comandos?"
        }

        onAccepted: app.deleteActiveProject()
    }

    Connections {
        target: app

        function onActiveProjectChanged() {
            view.loadFields()
        }
    }

    Component.onCompleted: loadFields()
}
