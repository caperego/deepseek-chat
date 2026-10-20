import QtQuick
import QtQuick.Controls

CheckBox {
    id: control
    spacing: 10
    implicitHeight: Math.max(34, contentItem.implicitHeight + 10)
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize
    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 20
        height: 20
        radius: 5
        color: control.checked ? theme.accent : theme.inputBg
        border.width: control.visualFocus ? 2 : 1
        border.color: control.checked || control.visualFocus ? theme.accent : theme.border
        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: "white"
            font.pixelSize: 15
            font.bold: true
        }
    }
}
