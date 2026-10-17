import QtQuick
import QtQuick.Controls

TextEdit {
    id: control

    selectByMouse: true

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
