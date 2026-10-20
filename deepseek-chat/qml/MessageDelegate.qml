import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: delegate

    property bool isUser: msgRole === "user"
    property bool isSystem: msgRole === "system"
    property bool isTool: msgRole === "tool"
    property bool detailOpen: false
    property bool reasoningOpen: false
    property bool internalOpen: false
    readonly property bool isLast: index === ListView.view.count - 1
    readonly property int internalStart: !isUser && !isSystem && !isTool
        ? displayContent.search(/<(?:｜|\|)+DSML(?:｜|\|)+[^>]*>/) : -1
    readonly property string conversationText: internalStart >= 0
        ? displayContent.substring(0, internalStart).trim() : displayContent
    readonly property string internalText: internalStart >= 0
        ? displayContent.substring(internalStart) : ""

    function activityLabel() {
        var text = String(content || "")
        if (/negado pelo usuário/i.test(text))
            return "Ação não autorizada"
        if (/cancelad/i.test(text))
            return "Ação cancelada"
        var result = {}
        try { result = JSON.parse(detail || "{}") } catch (e) {}
        if (!result || typeof result !== "object")
            result = {}
        var failed = !!result.error || result.timed_out === true
                || (typeof result.exit_code === "number" && result.exit_code !== 0)
                || (typeof result.status === "number" && result.status >= 400)
                || /falh|inválid|não encontrad|não informado|sem backup|parado|erro MCP|desconhecida/i.test(text)
        if (failed && !streaming)
            return "Não foi possível concluir esta etapa"
        var match = text.match(/^\s*⚙\s+([\w]+)/)
        var name = match ? match[1] : ""
        var activities = {
            run_command: ["Executando…", "Execução concluída"],
            list_files: ["Verificando pasta…", "Pasta verificada"],
            read_file: ["Lendo arquivo…", "Arquivo lido"],
            write_file: ["Salvando arquivo…", "Arquivo salvo"],
            find_files: ["Localizando arquivos…", "Busca concluída"],
            grep_files: ["Buscando nos arquivos…", "Busca concluída"],
            fetch_url: ["Consultando página…", "Página consultada"],
            browse_url: ["Consultando página…", "Página consultada"],
            search_web: ["Pesquisando…", "Pesquisa concluída"],
            download_file: ["Baixando arquivo…", "Download concluído"],
            restore_file: ["Restaurando arquivo…", "Arquivo restaurado"]
        }
        var labels = activities[name] || ["Processando…", "Etapa concluída"]
        return labels[streaming ? 0 : 1]
    }

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

    function codeBlockAt(text, targetIndex) {
        var parts = String(text || "").split(/```/)
        var codeIndex = 0
        for (var i = 1; i < parts.length; i += 2) {
            var part = parts[i]
            var newline = part.indexOf("\n")
            var body = newline >= 0 ? part.substring(newline + 1) : part
            body = body.replace(/\n$/, "")
            if (codeIndex === targetIndex)
                return body
            ++codeIndex
        }
        return ""
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
        var escapedCode = escapeHtml(code).replace(/"/g, '&quot;').replace(/'/g, '&#39;')
        var tokens = escapedCode.split(/(&quot;[^&]*?&quot;|&#39;[^&]*?&#39;|`[^`]*`|\/\/[^\n]*|#[^\n]*|--[^\n]*|\b\d+(?:\.\d+)?\b|\b[A-Za-z_][A-Za-z0-9_]*\b)/g)
        var out = ""
        for (var i = 0; i < tokens.length; ++i) {
            var token = tokens[i]
            if (!token)
                continue
            var style = ""
            if (token.indexOf("&quot;") === 0 || token.indexOf("&#39;") === 0
                    || token.indexOf("`") === 0)
                style = theme.dark ? "color:#98c379" : "color:#357a38"
            else if (token.indexOf("//") === 0 || token.indexOf("#") === 0
                     || token.indexOf("--") === 0)
                style = (theme.dark ? "color:#7f848e" : "color:#686d76") + ";font-style:italic"
            else if (/^\d/.test(token))
                style = theme.dark ? "color:#d19a66" : "color:#985b13"
            else if (keywordRegex.test(token))
                style = theme.dark ? "color:#c678dd" : "color:#8e3ba3"
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
                      + ';color:' + codeFg + ';font-family:Consolas;font-size:' + theme.codeFontSize
                      + 'pt;padding:1px 4px;border-radius:4px;">$1</code>')
        s = s.replace(/\*\*([^*\n]+)\*\*/g, "<b>$1</b>")
        s = s.replace(/__([^_\n]+)__/g, "<b>$1</b>")
        s = s.replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, "$1<i>$2</i>")
        s = s.replace(/~~([^~\n]+)~~/g, "<s>$1</s>")
        s = s.replace(/(^|[^"=])(https?:\/\/[^\s<"]+)/g, '$1<a style="color:' + theme.accent + ';" href="$2">$2</a>')
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
        var codeIndex = 0

        for (var i = 0; i < parts.length; ++i) {
            var part = parts[i]
            if (i % 2 === 1) {
                var newline = part.indexOf("\n")
                var lang = newline >= 0 ? part.substring(0, newline).trim() : ""
                var body = newline >= 0 ? part.substring(newline + 1) : part
                body = body.replace(/\n$/, "")
                var languageLabel = lang.length > 0 ? lang.toUpperCase() : "CÓDIGO"
                out += '<table width="100%" cellspacing="0" cellpadding="8" bgcolor="'
                        + codeBg + '"><tr><td><b>' + languageLabel + '</b></td><td align="right">'
                        + '<a style="color:' + theme.muted + ';text-decoration:none;" href="copy-code:' + codeIndex + '">⧉ Copiar código</a></td></tr>'
                        + '<tr><td colspan="2"><pre style="color:' + codeFg
                        + ';font-family:Consolas;font-size:' + theme.codeFontSize
                        + 'pt;white-space:pre;margin:0;">'
                        + highlight(body, lang) + '</pre></td></tr></table><br>'
                ++codeIndex
                continue
            }

            var lines = escapeHtml(part).split("\n")
            for (var j = 0; j < lines.length; ++j) {
                var line = lines[j]
                var trimmed = line.trim()

                var headers = tableCells(trimmed)
                var separators = j + 1 < lines.length ? tableCells(lines[j + 1].trim()) : []
                if (headers.length > 1 && headers.length === separators.length
                        && separators.every(function (cell) { return /^:?-{3,}:?$/.test(cell) })) {
                    out += '<table width="100%" border="1" cellspacing="0" cellpadding="6"'
                            + ' style="border-color:' + theme.border + ';">'
                    var alignments = separators.map(function (cell) {
                        return cell.charAt(0) === ':' && cell.charAt(cell.length - 1) === ':'
                            ? 'center' : (cell.charAt(cell.length - 1) === ':' ? 'right' : 'left')
                    })
                    out += tableRow(headers, alignments, true, codeBg)
                    j += 2
                    while (j < lines.length && lines[j].trim().indexOf('|') >= 0) {
                        var cells = tableCells(lines[j].trim())
                        if (cells.length !== headers.length)
                            break
                        out += tableRow(cells, alignments, false, codeBg)
                        ++j
                    }
                    --j
                    out += '</table><br>'
                    continue
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
        }

        var cleaned = out.replace(/(<br>\s*){3,}/g, "<br><br>")
        cleaned = cleaned.replace(/^(<br>)+/, "")
        cleaned = cleaned.replace(/(<br>)+$/, "")
        return cleaned
    }

    function tableCells(line) {
        if (line.indexOf('|') < 0)
            return []
        var body = line.replace(/^\|/, '')
        if (body.endsWith('|') && !body.endsWith('\\|'))
            body = body.slice(0, -1)
        var cells = []
        var cell = ''
        for (var i = 0; i < body.length; ++i) {
            if (body.charAt(i) === '\\' && body.charAt(i + 1) === '|') {
                cell += '|'
                ++i
            } else if (body.charAt(i) === '|') {
                cells.push(cell.trim())
                cell = ''
            } else {
                cell += body.charAt(i)
            }
        }
        cells.push(cell.trim())
        return cells
    }

    function tableRow(cells, alignments, header, background) {
        var out = '<tr>'
        for (var i = 0; i < cells.length; ++i) {
            out += '<td align="' + alignments[i] + '"'
                    + (header ? ' bgcolor="' + background + '"' : '') + '>'
                    + (header ? '<b>' : '') + inlineFormat(cells[i])
                    + (header ? '</b>' : '') + '</td>'
        }
        return out + '</tr>'
    }

    implicitHeight: layout.implicitHeight

    TextMetrics {
        id: metrics
        font.pointSize: theme.chatFontSize
        font.family: theme.chatFontFamily
        text: displayContent
    }

    ColumnLayout {
        id: layout
        width: Math.min(parent.width, 820)
        anchors.horizontalCenter: parent.horizontalCenter
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
                        text: delegate.activityLabel()
                        color: theme.muted
                        wrapMode: Text.Wrap
                        font.pointSize: 10
                    }

                    AppButton {
                        visible: detail.length > 0 || content.length > 0
                        text: delegate.detailOpen ? "Ocultar detalhes" : "Ver detalhes"
                        ghost: true
                        implicitHeight: 22
                        font.pointSize: 10
                        onClicked: delegate.detailOpen = !delegate.detailOpen
                    }

                    AppButton {
                        visible: delegate.detailPath().length > 0
                        text: "Painel"
                        ghost: true
                        implicitHeight: 22
                        font.pointSize: 10
                        onClicked: app.openArtifact(delegate.detailPath())
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 200
                    visible: delegate.detailOpen
                    radius: 6
                    color: theme.dark ? "#171717" : "#ffffff"
                    border.color: theme.border
                    clip: true

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 6

                        CopyableTextArea {
                            text: content + (detail.length > 0 ? "\n\n" + detail : "")
                            readOnly: true
                            wrapMode: TextArea.NoWrap
                            font.family: "Consolas"
                            font.pointSize: theme.codeFontSize
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

        ColumnLayout {
            Layout.fillWidth: true
            visible: !isTool && !isUser && !isSystem
                     && ((reasoning.length > 0 && app.showReasoning)
                         || (streaming && conversationText.length === 0))
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: streaming && conversationText.length === 0 ? "Pensando…" : "Pensamento"
                    color: theme.muted
                    font.pointSize: 10
                }
                AppButton {
                    visible: reasoning.length > 0 && app.showReasoning
                    text: delegate.reasoningOpen ? "Ocultar detalhes" : "Ver detalhes"
                    ghost: true
                    implicitHeight: 22
                    font.pointSize: 10
                    onClicked: delegate.reasoningOpen = !delegate.reasoningOpen
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(240, reasonText.contentHeight + 16)
                visible: delegate.reasoningOpen && reasoning.length > 0 && app.showReasoning
                clip: true
                CopyableTextArea {
                    id: reasonText
                    text: reasoning
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    color: theme.muted
                    font.pointSize: Math.max(9, theme.chatFontSize - 1)
                    font.family: theme.chatFontFamily
                }
            }
        }

        Rectangle {
            Layout.alignment: isUser ? Qt.AlignRight : Qt.AlignLeft
            Layout.fillWidth: !isUser
            Layout.preferredWidth: isUser ? Math.min(680, Math.max(90, metrics.width + 26)) : -1
            Layout.maximumWidth: layout.width
            visible: !isTool
            radius: isUser ? 16 : 0
            color: isUser ? theme.userBubble
                          : (isSystem ? theme.systemBubble : "transparent")
            implicitHeight: bodyText.contentHeight + (isUser ? 18 : 0)

            CopyableTextEdit {
                id: bodyText
                width: parent.width - (isUser ? 20 : 0)
                x: isUser ? 10 : 0
                y: isUser ? 9 : 0
                text: (isUser || isSystem)
                      ? displayContent + (streaming ? " ▌" : "")
                      : delegate.richText(conversationText)
                        + (streaming && conversationText.length > 0 ? " ▌" : "")
                textFormat: (isUser || isSystem) ? TextEdit.PlainText : TextEdit.RichText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: theme.text
                font.pointSize: theme.chatFontSize
                font.family: theme.chatFontFamily
                onLinkActivated: function (link) {
                    if (link.indexOf("copy-code:") === 0) {
                        app.copyToClipboard(delegate.codeBlockAt(conversationText,
                                                                Number(link.substring(10))))
                        return
                    }
                    app.openExternal(link)
                }

            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: delegate.internalText.length > 0
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Esta etapa não tem execução confirmada."
                    color: theme.muted
                    font.pointSize: Math.max(9, theme.chatFontSize - 2)
                    wrapMode: Text.Wrap
                }
                AppButton {
                    text: delegate.internalOpen ? "Ocultar detalhes" : "Ver detalhes"
                    ghost: true
                    onClicked: delegate.internalOpen = !delegate.internalOpen
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                visible: delegate.internalOpen
                clip: true
                CopyableTextArea {
                    text: delegate.internalText
                    readOnly: true
                    wrapMode: TextArea.Wrap
                    font.family: "Consolas"
                    font.pointSize: theme.codeFontSize
                    color: theme.muted
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: !isUser && !isTool && sources.length > 0
            spacing: 4

            Text {
                text: "Fontes consultadas"
                color: theme.muted
                font.bold: true
                font.pointSize: Math.max(9, theme.chatFontSize - 2)
            }

            Repeater {
                model: sources
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton {
                        text: (modelData.title || modelData.url) + " ↗"
                        ghost: true
                        onClicked: app.openExternal(modelData.url)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.excerpt || ""
                        color: theme.muted
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pointSize: Math.max(9, theme.chatFontSize - 3)
                    }
                }
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
                    ToolTip.text: "Copiar todos os códigos"
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

            AppButton {
                visible: messageId > 0
                text: "Ramificar"
                ghost: true
                onClicked: app.branchFromMessage(messageId)
            }

            AppButton {
                visible: messageId > 0 && app.activeProjectId > 0
                text: "Memorizar"
                ghost: true
                onClicked: app.rememberMessage(messageId)
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

                AppDialog {
                    id: previewDialog
                    parent: Overlay.overlay
                    title: fileCard.modelData.path
                    modal: true
                    font.pointSize: theme.fontSize
                    anchors.centerIn: parent
                    width: 760
                    height: 540
                    standardButtons: Dialog.Close

                    ScrollView {
                        anchors.fill: parent

                        CopyableTextArea {
                            text: fileCard.modelData.content
                            readOnly: true
                            wrapMode: TextArea.NoWrap
                            font.family: "Consolas"
                            font.pointSize: theme.codeFontSize
                        }
                    }
                }
            }
        }
    }

    AppDialog {
        id: editDialog
        parent: Overlay.overlay
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

                CopyableTextArea {
                    id: editArea
                    wrapMode: TextArea.Wrap
                    font.pointSize: theme.chatFontSize
                    font.family: theme.chatFontFamily
                }
            }
        }

        onOpened: editArea.text = content
        onAccepted: app.editMessage(messageId, editArea.text)
    }
}
