import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: delegate

    property bool isUser: msgRole === "user"
    property bool isSystem: msgRole === "system"
    property bool isTool: msgRole === "tool"
    property bool detailOpen: false
    readonly property bool isLast: index === ListView.view.count - 1

    HoverHandler {
        id: messageHover
    }

    function codeBlocks(text) {
        var out = []
        var re = /```[^\n]*\n([\s\S]*?)```/g
        var m
        while ((m = re.exec(text)) !== null)
            out.push(m[1])
        return out.join("\n\n")
    }

    function detailPath() {
        if (!detail || detail.length === 0)
            return ""
        try {
            var o = JSON.parse(detail)
            return o && o.path ? String(o.path) : ""
        } catch (e) {
            return ""
        }
    }

    function imageUrl(src) {
        if (!src)
            return ""
        if (/^(file:|https?:|data:|qrc:)/i.test(src))
            return src
        var path = String(src).replace(/\\/g, "/")
        if (/^[A-Za-z]:\//.test(path) || path.charAt(0) === "/")
            return "file:///" + path
        var workdir = app.activeProjectWorkdir
        if (workdir.length === 0)
            return ""
        var base = workdir.replace(/\\/g, "/")
        if (base.charAt(base.length - 1) !== "/")
            base += "/"
        return "file:///" + base + path
    }

    function detailIsImage() {
        return /\.(png|jpe?g|gif|webp|bmp)$/i.test(delegate.detailPath())
    }

    function escapeHtml(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    function keywordsFor(lang) {
        var l = String(lang || "").toLowerCase()
        if (l.indexOf("py") === 0)
            return "def|class|import|from|as|if|elif|else|for|while|return|yield|lambda|with|try|except|finally|raise|pass|break|continue|in|is|not|and|or|None|True|False|self|async|await|global|nonlocal|assert|del"
        if (l === "js" || l === "javascript" || l === "ts" || l === "typescript")
            return "function|var|let|const|if|else|for|while|do|return|class|extends|new|try|catch|finally|throw|switch|case|break|continue|typeof|instanceof|this|super|null|undefined|true|false|async|await|yield|import|export|from|default|interface|type|enum|public|private|protected|readonly"
        if (l === "bash" || l === "sh" || l === "shell" || l === "zsh")
            return "if|then|else|elif|fi|for|while|do|done|case|esac|function|return|export|local|echo|exit|source|alias|sudo|cd|mkdir|grep|sed|awk|curl|wget|git|npm|npx|python|pip"
        if (l === "sql")
            return "select|from|where|insert|into|values|update|set|delete|join|left|right|inner|outer|on|group|by|order|having|limit|create|table|alter|drop|index|primary|key|foreign|references|and|or|not|null|as|distinct|union|count|sum|avg|min|max"
        if (l === "json" || l === "xml" || l === "html")
            return "true|false|null"
        return "function|var|let|const|if|else|for|while|do|return|class|def|import|from|export|new|try|catch|finally|throw|switch|case|break|continue|public|private|protected|static|void|int|float|double|char|bool|string|namespace|using|struct|enum|typedef|true|false|null|None|True|False|and|or|not|in|is|self|print|echo|async|await|yield|lambda|package|end"
    }

    function highlight(code, lang) {
        var keywordRegex = new RegExp("^(" + keywordsFor(lang) + ")$")
        var tokens = escapeHtml(code).split(/(&quot;[^&]*?&quot;|&#39;[^&]*?&#39;|`[^`]*`|\/\/[^\n]*|#[^\n]*|--[^\n]*|\b\d+(?:\.\d+)?\b|\b[A-Za-z_][A-Za-z0-9_]*\b)/g)
        var out = ""
        for (var i = 0; i < tokens.length; ++i) {
            var token = tokens[i]
            if (!token)
                continue
            var style = ""
            if (token.indexOf("&quot;") === 0 || token.indexOf("&#39;") === 0
                    || token.indexOf("`") === 0)
                style = "color:#98c379"
            else if (token.indexOf("//") === 0 || token.indexOf("#") === 0
                     || token.indexOf("--") === 0)
                style = "color:#7f848e;font-style:italic"
            else if (/^\d/.test(token))
                style = "color:#d19a66"
            else if (keywordRegex.test(token))
                style = "color:#c678dd"
            out += style.length > 0 ? '<span style="' + style + '">' + token + '</span>'
                                    : token
        }
        return out
    }

    function inlineFormat(text) {
        var codeBg = theme.dark ? "#2a2a2a" : "#eeeeee"
        var codeFg = theme.dark ? "#ececec" : "#1f1f1f"
        var s = text
        s = s.replace(/`([^`\n]+)`/g, '<code style="background:' + codeBg
                      + ';color:' + codeFg + ';padding:1px 4px;border-radius:4px;">$1</code>')
        s = s.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>")
        s = s.replace(/__([^_\n]+)__/g, "<b>$1</b>")
        s = s.replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, "$1<i>$2</i>")
        s = s.replace(/~~([^~\n]+)~~/g, "<s>$1</s>")
        s = s.replace(/(^|[^"=])(https?:\/\/[^\s<"]+)/g, '$1<a href="$2">$2</a>')
        s = s.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, function (match, alt, src) {
            var url = delegate.imageUrl(src)
            if (url.length === 0)
                return match
            return '<img src="' + url + '" width="480">'
                    + (alt.length > 0 ? '<br><i>' + alt + '</i>' : '')
        })
        return s
    }

    function richText(text) {
        var parts = text.split(/```/)
        var out = ""
        var codeBg = theme.dark ? "#1b1b1b" : "#f5f5f5"
        var codeFg = theme.dark ? "#ececec" : "#1f1f1f"

        for (var i = 0; i < parts.length; ++i) {
            var part = parts[i]
            if (i % 2 === 1) {
                var newline = part.indexOf("\n")
                var lang = newline >= 0 ? part.substring(0, newline).trim() : ""
                var body = newline >= 0 ? part.substring(newline + 1) : part
                out += '<br><pre style="background:' + codeBg + ';color:' + codeFg
                        + ';padding:8px;border-radius:6px;white-space:pre;">'
                        + highlight(body, lang) + '</pre><br>'
                continue
            }

            var lines = escapeHtml(part).split("\n")
            var inTable = false
            for (var j = 0; j < lines.length; ++j) {
                var line = lines[j]
                var trimmed = line.trim()

                if (trimmed.indexOf("|") === 0 && trimmed.length > 1) {
                    if (!inTable) {
                        out += '<br><pre style="background:' + codeBg + ';color:' + codeFg
                                + ';padding:6px;border-radius:6px;white-space:pre;">'
                        inTable = true
                    }
                    out += inlineFormat(trimmed) + "\n"
                    continue
                }
                if (inTable) {
                    out += "</pre><br>"
                    inTable = false
                }

                if (trimmed.length === 0) {
                    out += "<br>"
                    continue
                }
                if (/^(-{3,}|\*{3,}|_{3,})$/.test(trimmed)) {
                    out += '<br><hr><br>'
                    continue
                }

                var heading = trimmed.match(/^(#{1,6})\s+(.*)$/)
                if (heading) {
                    out += "<br><b>" + inlineFormat(heading[2]) + "</b><br>"
                    continue
                }

                var item = trimmed.match(/^([-*+]|\d+\.)\s+(.*)$/)
                if (item) {
                    var indent = line.length - line.replace(/^\s+/, "").length
                    var depth = Math.floor(indent / 2)
                    var pad = ""
                    for (var k = 0; k < depth; ++k)
                        pad += "&nbsp;&nbsp;&nbsp;&nbsp;"
                    var marker = /^\d/.test(item[1]) ? item[1] : "&bull;"
                    out += "<br>" + pad + marker + "&nbsp;&nbsp;" + inlineFormat(item[2])
                    continue
                }

                var quote = trimmed.match(/^>\s?(.*)$/)
                if (quote) {
                    out += '<br><i>' + inlineFormat(quote[1]) + "</i>"
                    continue
                }

                out += inlineFormat(line) + "<br>"
            }
            if (inTable)
                out += "</pre><br>"
        }

        var cleaned = out.replace(/(<br>\s*){3,}/g, "<br><br>")
        cleaned = cleaned.replace(/^(<br>)+/, "")
        cleaned = cleaned.replace(/(<br>)+$/, "")
        return cleaned
    }

    implicitHeight: layout.implicitHeight

    TextMetrics {
        id: metrics
        font.pointSize: theme.chatFontSize
        text: displayContent
    }

    ColumnLayout {
        id: layout
        width: parent.width
        spacing: 6

        Rectangle {
            visible: isTool
            Layout.fillWidth: true
            radius: 8
            color: theme.dark ? "#242424" : "#f4f4f5"
            border.color: theme.border
            implicitHeight: toolColumn.implicitHeight + 14

            ColumnLayout {
                id: toolColumn
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                anchors.topMargin: 7
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        id: toolText
                        Layout.fillWidth: true
                        text: displayContent + (streaming ? " …" : "")
                        color: theme.muted
                        wrapMode: Text.Wrap
                        font.pointSize: Math.max(9, theme.chatFontSize - 2)
                    }

                    AppButton {
                        visible: detail.length > 0
                        text: delegate.detailOpen ? "Ocultar" : "Ver saída"
                        ghost: true
                        implicitHeight: 22
                        font.pointSize: Math.max(9, theme.chatFontSize - 3)
                        onClicked: delegate.detailOpen = !delegate.detailOpen
                    }

                    AppButton {
                        visible: delegate.detailPath().length > 0
                        text: "Painel"
                        ghost: true
                        implicitHeight: 22
                        font.pointSize: Math.max(9, theme.chatFontSize - 3)
                        onClicked: app.openArtifact(delegate.detailPath())
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    visible: delegate.detailOpen && detail.length > 0
                    radius: 6
                    color: theme.dark ? "#171717" : "#ffffff"
                    border.color: theme.border
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 6

                        TextArea {
                            text: detail
                            readOnly: true
                            wrapMode: TextArea.NoWrap
                            font.family: "monospace"
                            font.pointSize: Math.max(9, theme.chatFontSize - 3)
                            color: theme.text
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 260
                    visible: delegate.detailIsImage()
                    radius: 6
                    color: theme.dark ? "#171717" : "#ffffff"
                    border.color: theme.border
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: delegate.detailIsImage()
                                ? delegate.imageUrl(delegate.detailPath()) : ""
                        fillMode: Image.PreserveAspectFit
                        sourceSize.width: 640
                        asynchronous: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: app.openArtifact(delegate.detailPath())
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: attachmentNames.length > 0 && !isTool
            text: "Anexos: " + attachmentNames.join(", ")
            color: theme.muted
            font.pointSize: Math.max(9, theme.chatFontSize - 2)
            wrapMode: Text.Wrap
        }

        Rectangle {
            Layout.fillWidth: true
            visible: reasoning.length > 0 && app.showReasoning && !isTool
            radius: 8
            color: theme.dark ? "#2a2a2a" : "#f3f4f6"
            implicitHeight: reasonText.implicitHeight + 16

            Text {
                id: reasonText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                text: reasoning
                color: theme.muted
                font.italic: true
                font.pointSize: Math.max(9, theme.chatFontSize - 1)
                wrapMode: Text.Wrap
                maximumLineCount: 5
                elide: Text.ElideRight
            }
        }

        Rectangle {
            Layout.alignment: isUser ? Qt.AlignRight : Qt.AlignLeft
            Layout.fillWidth: !isUser
            Layout.preferredWidth: isUser ? Math.min(680, Math.max(90, metrics.width + 26)) : -1
            visible: !isTool
            radius: isUser ? 16 : 0
            color: isUser ? theme.userBubble
                          : (isSystem ? theme.systemBubble : "transparent")
            implicitHeight: bodyText.contentHeight + (isUser ? 18 : 0)

            TextEdit {
                id: bodyText
                width: parent.width - (isUser ? 20 : 0)
                x: isUser ? 10 : 0
                y: isUser ? 9 : 0
                text: (isUser || isSystem)
                      ? displayContent + (streaming ? " ▌" : "")
                      : delegate.richText(displayContent) + (streaming ? " ▌" : "")
                textFormat: (isUser || isSystem) ? TextEdit.PlainText : TextEdit.RichText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.pointSize: theme.chatFontSize
                onLinkActivated: function (link) { app.openExternal(link) }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: errorText.length > 0
            text: "Erro: " + errorText
            color: theme.danger
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.topMargin: 4
            Layout.alignment: isUser ? Qt.AlignRight : Qt.AlignLeft
            spacing: 6
            visible: !streaming && !isSystem && !isTool
            opacity: messageHover.hovered ? 1.0 : 0.65

            Behavior on opacity {
                NumberAnimation { duration: 120 }
            }

            Item {
                implicitWidth: 22
                implicitHeight: 22

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/resources/icons/copy.svg"
                    sourceSize.width: 17
                    sourceSize.height: 17
                    opacity: copyMouse.containsMouse ? 1.0 : 0.9
                }

                MouseArea {
                    id: copyMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.copyToClipboard(content)

                    ToolTip.visible: containsMouse
                    ToolTip.text: "Copiar"
                }
            }

            Item {
                visible: content.indexOf("```") >= 0
                implicitWidth: 22
                implicitHeight: 22

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/resources/icons/code.svg"
                    sourceSize.width: 17
                    sourceSize.height: 17
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.copyToClipboard(delegate.codeBlocks(content))

                    ToolTip.visible: containsMouse
                    ToolTip.text: "Copiar código"
                }
            }

            Item {
                visible: isUser && messageId > 0
                implicitWidth: 22
                implicitHeight: 22

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/resources/icons/pencil.svg"
                    sourceSize.width: 17
                    sourceSize.height: 17
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: editDialog.open()

                    ToolTip.visible: containsMouse
                    ToolTip.text: "Editar"
                }
            }

            Item {
                visible: !isUser && delegate.isLast
                implicitWidth: 22
                implicitHeight: 22

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/resources/icons/refresh.svg"
                    sourceSize.width: 17
                    sourceSize.height: 17
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: app.regenerateLast()

                    ToolTip.visible: containsMouse
                    ToolTip.text: "Regenerar"
                }
            }
        }

        Repeater {
            model: files

            delegate: Rectangle {
                id: fileCard
                required property var modelData

                Layout.fillWidth: true
                radius: 10
                color: theme.card
                border.color: theme.border
                implicitHeight: fileColumn.implicitHeight + 20

                ColumnLayout {
                    id: fileColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "Arquivo sugerido: " + modelData.path
                        color: theme.text
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        AppButton {
                            text: "Salvar no projeto"
                            icon.source: "qrc:/resources/icons/save.svg"
                            primary: true
                            onClicked: app.applyFile(modelData.path, modelData.content)
                        }

                        AppButton {
                            text: "Prévia"
                            icon.source: "qrc:/resources/icons/eye.svg"
                            ghost: true
                            onClicked: previewDialog.open()
                        }
                    }
                }

                Dialog {
                    id: previewDialog
                    title: fileCard.modelData.path
                    modal: true
                    font.pointSize: theme.chatFontSize
                    anchors.centerIn: parent
                    width: 760
                    height: 540
                    standardButtons: Dialog.Close

                    ScrollView {
                        anchors.fill: parent

                        TextArea {
                            text: fileCard.modelData.content
                            readOnly: true
                            wrapMode: TextArea.NoWrap
                            font.family: "monospace"
                            font.pointSize: theme.chatFontSize
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: editDialog
        modal: true
        anchors.centerIn: parent
        width: 560
        title: "Editar mensagem"
        font.pointSize: theme.fontSize
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            width: parent.width
            spacing: 10

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 220

                TextArea {
                    id: editArea
                    wrapMode: TextArea.Wrap
                }
            }
        }

        onOpened: editArea.text = content
        onAccepted: app.editMessage(messageId, editArea.text)
    }
}
