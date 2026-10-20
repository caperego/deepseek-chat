import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

AppButton {
    id: control
    ghost: true
    font.pointSize: theme.sidebarFontSize
    implicitHeight: Math.max(38, label.implicitHeight + 14)

    contentItem: Row {
        spacing: 11
        IconImage {
            anchors.verticalCenter: parent.verticalCenter
            width: control.icon.width
            height: control.icon.height
            source: control.icon.source
            color: control.icon.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            color: control.palette.buttonText
            font: control.font
            elide: Text.ElideRight
            width: Math.max(0, control.availableWidth - control.icon.width - parent.spacing)
        }
    }
}
