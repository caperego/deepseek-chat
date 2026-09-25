import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    spacing: 0

    Sidebar {
        Layout.preferredWidth: 284
        Layout.fillHeight: true
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: app.viewMode

        ChatView {}
        ProjectView {}
    }

    Rectangle {
        Layout.preferredWidth: app.artifactVisible ? 460 : 0
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

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextArea {
                    id: artifactArea
                    wrapMode: TextArea.NoWrap
                    font.family: "monospace"
                    font.pointSize: Math.max(9, theme.fontSize - 2)
                    color: theme.text
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

                    TextArea {
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
