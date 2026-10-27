import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// File opened by the agent or by the user: edit, preview, diff and versions.
ColumnLayout {
    id: artifact
    spacing: 8
    property alias text: artifactArea.text
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas" : "monospace"

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: app.displayPath(app.artifactPath)
            color: theme.text
            font.bold: true
            elide: Text.ElideMiddle
            HoverHandler { id: pathHover }
            ToolTip.visible: pathHover.hovered
            ToolTip.text: app.artifactPath
        }

        AppButton {
            text: artifact.width < 420 ? "" : "Copiar"
            ToolTip.text: "Copiar arquivo"
            ToolTip.visible: hovered
            icon.source: "qrc:/resources/icons/copy.svg"
            ghost: true
            onClicked: app.copyToClipboard(artifactArea.text)
        }

        AppButton {
            text: artifact.width < 420 ? "" : "Abrir"
            ToolTip.text: "Abrir no aplicativo padrão"
            ToolTip.visible: hovered
            icon.source: "qrc:/resources/icons/open.svg"
            ghost: true
            onClicked: app.openPath(app.artifactPath)
        }

        AppButton {
            text: artifact.width < 420 ? "" : "Salvar"
            ToolTip.text: "Salvar arquivo (Ctrl+S)"
            ToolTip.visible: hovered
            icon.source: "qrc:/resources/icons/save.svg"
            primary: true
            onClicked: app.saveArtifact(artifactArea.text)
        }

        AppButton {
            icon.source: "qrc:/resources/icons/close.svg"
            ghost: true
            ToolTip.text: "Fechar arquivo"
            ToolTip.visible: hovered
            onClicked: app.closeArtifact()
        }
    }

    AppTabBar {
        id: artifactTabs
        Layout.fillWidth: true
        AppTabButton { text: "Editar" }
        AppTabButton { text: "Prévia" }
        AppTabButton { text: "Alterações" }
        AppTabButton { text: "Versões" }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: artifactTabs.currentIndex

        ScrollView {
            CopyableTextArea {
                id: artifactArea
                objectName: "artifactEditor"
                wrapMode: TextArea.NoWrap
                font.family: artifact.monoFamily
                font.pointSize: theme.codeFontSize
                color: theme.text
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
                        app.saveArtifact(artifactArea.text)
                        event.accepted = true
                    }
                }
            }
        }

        Item {
            id: previewPane
            readonly property bool svg: /\.svg$/i.test(app.artifactPath)
            readonly property bool markdown: /\.(md|markdown)$/i.test(app.artifactPath)
            readonly property bool html: /\.(html|htm)$/i.test(app.artifactPath)

            Image {
                anchors.fill: parent
                anchors.margins: 10
                visible: parent.svg
                source: parent.svg && artifactTabs.currentIndex === 1
                        ? app.artifactPreviewUrl(artifactArea.text) : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            ScrollView {
                anchors.fill: parent
                visible: !parent.svg
                CopyableTextEdit {
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: previewPane.markdown ? TextEdit.MarkdownText
                                : (previewPane.html ? TextEdit.RichText
                                   : TextEdit.PlainText)
                    text: artifactTabs.currentIndex === 1 ? artifactArea.text : ""
                    color: theme.text
                    font.pointSize: theme.chatFontSize
                    font.family: theme.chatFontFamily
                    onLinkActivated: function(link) { app.openLink(link) }
                }
            }
        }

        ScrollView {
            CopyableTextArea {
                text: artifactTabs.currentIndex === 2
                      ? app.artifactDiff(artifactArea.text) : ""
                readOnly: true
                wrapMode: TextArea.NoWrap
                font.family: artifact.monoFamily
                font.pointSize: theme.codeFontSize
                color: theme.text
            }
        }

        ScrollView {
            ColumnLayout {
                width: parent.width
                spacing: 8
                Text {
                    text: app.artifactVersions.length > 0
                          ? "Versões anteriores deste arquivo" : "Nenhuma versão anterior."
                    color: theme.muted
                }
                Repeater {
                    model: app.artifactVersions
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            text: modelData.label
                            color: theme.text
                            Layout.fillWidth: true
                        }
                        AppButton {
                            text: "Restaurar"
                            onClicked: app.restoreArtifactVersion(modelData.path)
                        }
                    }
                }
            }
        }
    }
}
