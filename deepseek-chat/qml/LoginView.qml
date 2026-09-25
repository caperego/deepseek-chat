import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: view

    property string errorText: ""

    Rectangle {
        anchors.fill: parent
        color: theme.bg
    }

    Rectangle {
        id: card
        width: 450
        anchors.centerIn: parent
        radius: 18
        color: theme.card
        border.color: theme.border
        border.width: 1
        implicitHeight: content.implicitHeight + 48
        height: implicitHeight

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 24
            spacing: 14

            Image {
                source: "qrc:/resources/deepseek-128.png"
                sourceSize.width: 76
                sourceSize.height: 76
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "DeepSeek Chat"
                font.pixelSize: 26
                font.bold: true
                color: theme.text
                Layout.alignment: Qt.AlignHCenter
            }

            Text {
                text: "Entre para continuar suas conversas"
                color: theme.muted
                Layout.alignment: Qt.AlignHCenter
            }

            TabBar {
                id: tabs
                Layout.fillWidth: true

                TabButton { text: "Entrar" }
                TabButton { text: "Criar conta" }
            }

            StackLayout {
                currentIndex: tabs.currentIndex
                Layout.fillWidth: true

                ColumnLayout {
                    spacing: 10

                    TextField {
                        id: loginUser
                        placeholderText: "usuário"
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: loginPass
                        placeholderText: "senha"
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                        onAccepted: loginButton.clicked()
                    }

                    AppButton {
                        id: loginButton
                        text: "Entrar"
                        icon.source: "qrc:/resources/icons/send.svg"
                        primary: true
                        Layout.fillWidth: true
                        onClicked: {
                            view.errorText = ""
                            app.login(loginUser.text, loginPass.text)
                        }
                    }
                }

                ColumnLayout {
                    spacing: 10

                    TextField {
                        id: regUser
                        placeholderText: "usuário (mínimo 3 caracteres)"
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: regName
                        placeholderText: "nome (opcional)"
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: regPass
                        placeholderText: "senha"
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                    }

                    TextField {
                        id: regPass2
                        placeholderText: "confirmar senha"
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                        onAccepted: registerButton.clicked()
                    }

                    AppButton {
                        id: registerButton
                        text: "Criar conta"
                        icon.source: "qrc:/resources/icons/plus.svg"
                        primary: true
                        Layout.fillWidth: true
                        onClicked: {
                            view.errorText = ""
                            if (regPass.text !== regPass2.text) {
                                view.errorText = "As senhas não conferem."
                                return
                            }
                            app.registerUser(regUser.text, regName.text, regPass.text)
                        }
                    }
                }
            }

            Text {
                text: view.errorText
                color: theme.danger
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                visible: view.errorText.length > 0
            }

            RowLayout {
                Layout.fillWidth: true

                AppButton {
                    text: "config.ini"
                    icon.source: "qrc:/resources/icons/gear.svg"
                    ghost: true
                    onClicked: app.openConfigFile()
                }

                Item { Layout.fillWidth: true }

                AppButton {
                    text: "Sair"
                    icon.source: "qrc:/resources/icons/logout.svg"
                    ghost: true
                    onClicked: Qt.quit()
                }
            }
        }
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        text: "Desenvolvido por <b>Carlos Alberto Perego</b> — <a href=\"https://carlos.pw\">carlos.pw</a>"
        textFormat: Text.RichText
        color: theme.muted
        font.pixelSize: Math.max(10, theme.fontSize - 3)
        onLinkActivated: (link) => app.openExternal(link)
    }

    Connections {
        target: app

        function onLoginFailed(message) {
            view.errorText = message
        }

        function onRegisterFailed(message) {
            view.errorText = message
        }
    }
}
