import QtQuick
import QtQuick.Controls

Dialog {
    id: control
    Overlay.modal: Rectangle { color: "#3d000000" }
    background: Rectangle {
        radius: 28
        color: theme.card
        border.color: theme.border
    }
    header: Label {
        visible: control.title.length > 0
        text: control.title
        color: theme.text
        font.family: theme.fontFamily
        font.pointSize: theme.fontSize + 2
        font.bold: true
        leftPadding: 20
        rightPadding: 20
        topPadding: 18
        bottomPadding: 12
        elide: Text.ElideRight
    }
    footer: DialogButtonBox {
        visible: count > 0
        spacing: 8
        padding: 14
        alignment: Qt.AlignRight
        delegate: AppButton {
            primary: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                     || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
                     || DialogButtonBox.buttonRole === DialogButtonBox.ApplyRole
            ghost: !primary
        }
        background: Item {}
    }
}
