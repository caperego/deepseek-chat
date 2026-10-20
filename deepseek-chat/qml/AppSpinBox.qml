import QtQuick
import QtQuick.Controls

SpinBox {
    id: control
    implicitHeight: 40
    implicitWidth: 132
    leftPadding: 32
    rightPadding: 32
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize
    background: Rectangle {
        radius: 12
        color: theme.inputBg
        border.color: control.visualFocus ? theme.accent : theme.border
        border.width: control.visualFocus ? 2 : 1
    }
    down.indicator: Rectangle {
        x: 2
        y: 2
        width: 30
        height: control.height - 4
        radius: 7
        color: control.down.pressed ? theme.selected : "transparent"
        Text { anchors.centerIn: parent; text: "−"; color: theme.text; font.pixelSize: 19 }
    }
    up.indicator: Rectangle {
        x: control.width - width - 2
        y: 2
        width: 30
        height: control.height - 4
        radius: 7
        color: control.up.pressed ? theme.selected : "transparent"
        Text { anchors.centerIn: parent; text: "+"; color: theme.text; font.pixelSize: 19 }
    }
}
