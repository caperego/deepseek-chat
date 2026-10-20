import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: Math.max(40, contentHeight + 18)
    leftPadding: 12
    rightPadding: 12
    color: theme.text
    placeholderTextColor: theme.muted
    selectionColor: theme.accent
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize
    background: Rectangle {
        radius: 12
        color: theme.inputBg
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? theme.accent : theme.border
    }
}
