import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog
    objectName: "automationsDialog"
    title: "Automações"
    modal: true
    width: parent ? Math.min(800, parent.width - 48) : 800
    height: parent ? Math.min(760, parent.height - 48) : 760
    padding: 20

    contentItem: ColumnLayout {
        spacing: 12
        ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ColumnLayout {
                width: formScroll.availableWidth
                spacing: 12
        Text {
            Layout.fillWidth: true
            text: "Agende instruções para executar em conversas próprias. O aplicativo precisa estar aberto e conectado à sua conta. Ações que exigem permissão pedem aprovação."
            wrapMode: Text.Wrap
            color: theme.muted
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            AppTextField {
                id: titleField
                Layout.columnSpan: 2
                Layout.fillWidth: true
                placeholderText: "Nome da tarefa"
                maximumLength: 120
            }
            Text { text: "Projeto"; color: theme.text }
            HeaderComboBox {
                id: projectCombo
                Layout.fillWidth: true
                property var projects: [{id: 0, name: "Sem projeto"}].concat(app.projects)
                model: projects.map(function(project) { return project.name })
            }
            Text { text: "Iniciar em (minutos)"; color: theme.text }
            AppSpinBox { id: delaySpin; from: 1; to: 525600; value: 5; editable: true }
            Text { text: "Repetir"; color: theme.text }
            HeaderComboBox {
                id: repeatCombo
                Layout.fillWidth: true
                model: ["Uma vez", "A cada hora", "A cada 24 horas", "A cada 7 dias"]
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            CopyableTextArea {
                id: promptField
                wrapMode: TextArea.Wrap
                placeholderText: "O que esta tarefa deve fazer?"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Usa o modelo selecionado no chat."; color: theme.muted; Layout.fillWidth: true; wrapMode: Text.Wrap }
            AppButton {
                text: "Agendar tarefa"
                primary: true
                enabled: titleField.text.trim().length > 0 && promptField.text.trim().length > 0
                onClicked: {
                    if (app.addAutomation(titleField.text, promptField.text, delaySpin.value,
                                          [0, 60, 1440, 10080][repeatCombo.currentIndex],
                                          projectCombo.projects[projectCombo.currentIndex].id)) {
                        titleField.clear()
                        promptField.clear()
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.border }
        Text { text: "Tarefas agendadas"; color: theme.text; font.bold: true }
        Repeater {
            model: app.automations
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: taskColumn.implicitHeight + 24
                radius: 12
                color: theme.sidebar
                ColumnLayout {
                    id: taskColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    Text { Layout.fillWidth: true; text: modelData.title; color: theme.text; font.bold: true; elide: Text.ElideRight }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.status + (modelData.enabled ? " · Próxima: " + new Date(Number(modelData.nextRun)).toLocaleString(Qt.locale(), "dd/MM HH:mm") : " · Inativa")
                        color: theme.muted
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        AppButton {
                            text: "Parar"
                            ghost: true
                            danger: true
                            visible: modelData.running
                            onClicked: app.stopAutomation(modelData.id)
                        }
                        AppButton {
                            text: modelData.enabled ? "Pausar" : "Ativar"
                            ghost: true
                            enabled: !modelData.running
                            onClicked: app.toggleAutomation(modelData.id)
                        }
                        AppButton {
                            text: "Ver resultado"
                            ghost: true
                            visible: Number(modelData.conversationId) > 0
                            onClicked: { app.openConversation(modelData.conversationId); dialog.close() }
                        }
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: "Excluir"
                            ghost: true
                            danger: true
                            enabled: !modelData.running
                            onClicked: app.removeAutomation(modelData.id)
                        }
                    }
                }
            }
        }
        Label { visible: app.automations.length === 0; text: "Nenhuma tarefa agendada"; color: theme.muted }
            }
        }
        AppButton { text: "Fechar"; Layout.alignment: Qt.AlignRight; ghost: true; onClicked: dialog.close() }
    }
}
