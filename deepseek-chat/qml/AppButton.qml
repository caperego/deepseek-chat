import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool primary: false
    property bool danger: false
    property bool ghost: false
    property int cornerRadius: 12

    implicitHeight: Math.max(36, contentItem.implicitHeight + topPadding + bottomPadding)
    leftPadding: 12
    rightPadding: 12
    topPadding: 7
    bottomPadding: 7
    spacing: 7

    icon.width: 18
    icon.height: 18
    icon.color: primary ? "#ffffff" : (danger ? theme.danger : theme.text)

    palette.buttonText: primary ? "#ffffff" : (danger ? theme.danger : theme.text)
    font.pointSize: theme.fontSize
    font.family: theme.fontFamily

    hoverEnabled: true
    opacity: enabled ? 1.0 : 0.45
    Accessible.name: text.length > 0 ? text : ToolTip.text

    background: Rectangle {
        radius: control.cornerRadius
        color: {
            if (control.primary) {
                if (control.pressed)
                    return Qt.darker(theme.accent, 1.15)
                return control.hovered ? Qt.lighter(theme.accent, 1.12) : theme.accent
            }
            if (control.ghost)
                return control.hovered ? theme.selected : "transparent"
            if (control.pressed)
                return theme.dark ? "#3a3a3a" : "#e5e7eb"
            return control.hovered ? (theme.dark ? "#333333" : "#f3f4f6") : theme.card
        }
        border.width: control.visualFocus ? 2 : ((control.primary || control.ghost) ? 0 : 1)
        border.color: control.visualFocus ? theme.accent : theme.border

        Behavior on color {
            ColorAnimation { duration: 120 }
        }
    }
}
