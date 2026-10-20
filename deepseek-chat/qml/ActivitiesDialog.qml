import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

AppDialog {
    id: dialog

    modal: true
    font.pointSize: theme.fontSize
    width: 820
    height: 560
    title: "Registro de atividades"
    padding: 0

    onOpened: app.refreshActivities()

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 34
            color: theme.card
            border.color: theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12

                Text {
                    Layout.preferredWidth: 170
                    text: "Data"
                    font.bold: true
                    color: theme.text
                }

                Text {
                    Layout.preferredWidth: 170
                    text: "Ação"
                    font.bold: true
                    color: theme.text
                }

                Text {
                    Layout.fillWidth: true
                    text: "Detalhes"
                    font.bold: true
                    color: theme.text
                }
            }
        }

        ListView {
            id: activityList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: app.activities
            spacing: 1

            delegate: Rectangle {
                required property var modelData
                required property int index

                width: activityList.width
                height: 34
                color: index % 2 === 0 ? "transparent" : theme.sidebar

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12

                    Text {
                        Layout.preferredWidth: 170
                        text: modelData.date
                        color: theme.muted
                    }

                    Text {
                        Layout.preferredWidth: 170
                        text: modelData.action
                        color: theme.text
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.details
                        color: theme.text
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12

            Item { Layout.fillWidth: true }

            AppButton {
                text: "Atualizar"
                icon.source: "qrc:/resources/icons/refresh.svg"
                onClicked: app.refreshActivities()
            }

            AppButton {
                text: "Fechar"
                icon.source: "qrc:/resources/icons/close.svg"
                ghost: true
                onClicked: dialog.close()
            }
        }
    }
}
