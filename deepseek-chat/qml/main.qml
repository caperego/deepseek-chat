import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1280
    height: 840
    minimumWidth: 920
    minimumHeight: 600
    visible: true
    color: theme.bg
    font.pointSize: theme.fontSize
    font.family: theme.fontFamily
    title: app.loggedIn
           ? "DeepSeek Chat — " + (app.displayName.length > 0 ? app.displayName : app.userName)
           : "DeepSeek Chat"

    palette.window: theme.bg
    palette.windowText: theme.text
    palette.base: theme.inputBg
    palette.alternateBase: theme.sidebar
    palette.text: theme.text
    palette.button: theme.card
    palette.buttonText: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: "#ffffff"
    palette.placeholderText: theme.muted
    palette.mid: theme.border
    palette.dark: theme.border
    palette.toolTipBase: theme.card
    palette.toolTipText: theme.text

    StackLayout {
        anchors.fill: parent
        currentIndex: app.loggedIn || app.restoringSession ? 1 : 0
        LoginView {}
        MainView { enabled: !app.restoringSession }
    }

    Rectangle {
        visible: app.restoringSession
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        width: loadingLabel.implicitWidth + 28
        height: loadingLabel.implicitHeight + 20
        radius: 10
        color: theme.card
        border.color: theme.border
        z: 100
        Label {
            id: loadingLabel
            anchors.centerIn: parent
            text: "Carregando suas conversas…"
            color: theme.muted
        }
    }

    Rectangle {
        id: toast
        property string message: ""

        function show(msg) {
            toast.message = msg
            toast.opacity = 1
            toastTimer.restart()
        }

        width: Math.min(620, Math.max(220, toastLabel.implicitWidth + 36))
        height: toastLabel.implicitHeight + 22
        radius: 10
        color: theme.dark ? "#3a3a3a" : "#111827"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 30
        opacity: 0
        visible: opacity > 0.01
        z: 1000

        Text {
            id: toastLabel
            anchors.centerIn: parent
            width: parent.width - 28
            text: toast.message
            color: "#ffffff"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: toastTimer
            interval: 2600
            onTriggered: toast.opacity = 0
        }

        Behavior on opacity {
            NumberAnimation { duration: 250 }
        }
    }

    Connections {
        target: app

        function onToast(message) {
            toast.show(message)
        }
    }
}
