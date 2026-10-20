import QtQuick
import QtQuick.Controls

TabButton {
    id: control
    implicitHeight: Math.max(38, contentItem.implicitHeight + 14)
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize
    background: Rectangle {
        radius: 8
        color: control.checked ? theme.selected : (control.hovered ? theme.selected : "transparent")
        border.width: control.visualFocus ? 2 : 0
        border.color: theme.accent
    }
}
