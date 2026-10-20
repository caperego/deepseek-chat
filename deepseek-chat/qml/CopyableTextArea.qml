import QtQuick
import QtQuick.Controls

TextArea {
    id: control

    selectByMouse: true
    color: theme.text
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize
    leftPadding: 12
    rightPadding: 12
    background: Rectangle {
        radius: 12
        color: theme.inputBg
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? theme.accent : theme.border
    }

    Menu {
        id: selectionMenu

        MenuItem {
            text: "Copiar texto"
            enabled: control.selectedText.length > 0
            onTriggered: app.copyToClipboard(control.selectedText)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        cursorShape: control.selectedText.length > 0
                     ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: function(mouse) {
            selectionMenu.popup(mouse.x, mouse.y)
        }
    }
}
