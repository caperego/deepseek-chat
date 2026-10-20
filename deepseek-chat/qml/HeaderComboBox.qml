import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T

ComboBox {
    id: control

    implicitHeight: Math.max(38, contentItem.implicitHeight + 14)
    leftPadding: 12
    rightPadding: 32
    hoverEnabled: true
    font.family: theme.fontFamily
    font.pointSize: theme.fontSize

    background: Rectangle {
        radius: 12
        color: control.pressed || control.hovered ? theme.selected : theme.card
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? theme.accent : theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
    }

    indicator: Text {
        x: control.width - width - 12
        y: (control.height - height) / 2
        text: "⌄"
        color: theme.muted
        font.pixelSize: 18
    }

    contentItem: T.TextField {
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: theme.text
        selectionColor: theme.accent
        selectedTextColor: "white"
        verticalAlignment: Text.AlignVCenter
    }

    delegate: ItemDelegate {
        required property int index
        width: ListView.view.width
        text: control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        background: Rectangle {
            radius: 8
            color: parent.highlighted || parent.hovered ? theme.selected : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 6
        width: control.width
        height: Math.min(320, contentItem.implicitHeight + 12)
        padding: 6
        topMargin: 12
        bottomMargin: 12
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle { radius: 12; color: theme.card; border.color: theme.border }
    }
}
