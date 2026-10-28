import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl
import QtWebEngine

ColumnLayout {
    id: pane
    objectName: "browserPane"
    spacing: 6
    property int currentIndex: 0
    property string loadError: ""
    property bool findOpen: false
    property bool devToolsOpen: false
    property var pendingPermission: null
    property var pendingCertificate: null
    property var activeDownload: null
    readonly property var current: views.count > 0 && currentIndex < views.count
                                   ? views.itemAt(currentIndex) : null
    readonly property string currentUrl: current ? current.url.toString() : ""

    function normalize(text) {
        var address = text.trim()
        if (!address.length) return ""
        if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(address))
            address = /^(localhost|127\.0\.0\.1|\[::1\])([:/]|$)/.test(address) ? "http://" + address : "https://" + address
        return address
    }
    function navigate(text) {
        var address = normalize(text)
        if (!address.length) return
        if (!/^https?:\/\//i.test(address)) {
            loadError = "Use um endereço HTTP ou HTTPS."
            return
        }
        loadError = ""
        if (!current) newTab(address)
        else current.url = address
    }
    function newTab(url) {
        tabsModel.append({ initialUrl: url && url.length ? normalize(url) : "" })
        currentIndex = tabsModel.count - 1
        if (!url || !url.length) focusAddress()
    }
    function openUrl(url) {
        // Links from the conversation open in their own tab, keeping the current page.
        if (!current || currentUrl === "" || currentUrl === "about:blank") navigate(url)
        else newTab(url)
    }
    function closeTab(index) {
        if (tabsModel.count <= 1) {
            if (current) current.url = "about:blank"
            address.text = ""
            return
        }
        tabsModel.remove(index)
        currentIndex = Math.min(currentIndex, tabsModel.count - 1)
    }
    function focusAddress() {
        address.forceActiveFocus()
        address.selectAll()
    }
    function showFind() {
        findOpen = true
        findField.forceActiveFocus()
        findField.selectAll()
    }
    function find(backward) {
        if (!current) return
        current.findText(findField.text, backward ? WebEngineView.FindBackward : 0)
    }
    function zoom(step) {
        if (!current) return
        current.zoomFactor = step === 0 ? 1.0
            : Math.max(0.25, Math.min(5.0, Math.round((current.zoomFactor + step) * 10) / 10))
    }
    function attachPageText() {
        if (!current || currentUrl === "" || currentUrl === "about:blank") return
        var view = current
        view.runJavaScript("document.body ? document.body.innerText : ''", function(text) {
            app.attachPageText(view.title, view.url.toString(), String(text || ""))
        })
    }
    function attachScreenshot() {
        if (!current) return
        current.grabToImage(function(result) { app.attachImage(result.image) })
    }
    function permissionLabel(permission) {
        switch (permission.permissionType) {
        case WebEnginePermission.PermissionType.MediaAudioCapture: return "usar o microfone"
        case WebEnginePermission.PermissionType.MediaVideoCapture: return "usar a câmera"
        case WebEnginePermission.PermissionType.MediaAudioVideoCapture: return "usar a câmera e o microfone"
        case WebEnginePermission.PermissionType.Geolocation: return "acessar sua localização"
        case WebEnginePermission.PermissionType.Notifications: return "mostrar notificações"
        case WebEnginePermission.PermissionType.ClipboardReadWrite: return "ler a área de transferência"
        case WebEnginePermission.PermissionType.DesktopVideoCapture:
        case WebEnginePermission.PermissionType.DesktopAudioVideoCapture: return "capturar sua tela"
        default: return "usar um recurso do sistema"
        }
    }
    // Agent requests: navigate or run a script, wait until the page settles,
    // then reply with a snapshot of the visible page.
    property var settleWaiters: []
    function whenSettled(view, callback) {
        var now = Date.now()
        var waiters = settleWaiters.slice()
        waiters.push({ view: view, callback: callback, earliest: now + 400, deadline: now + 20000 })
        settleWaiters = waiters
        settleTimer.start()
    }
    function agentRequest(id, request) {
        if (request.newTab || !current) newTab("")
        var view = current
        if (!view) {
            app.agentBrowserReply(id, { error: "O navegador integrado não está disponível." })
            return
        }
        var reply = function(action) {
            whenSettled(view, function() {
                view.runJavaScript(request.snapshot, function(snapshot) {
                    var result = snapshot || {}
                    result.url = view.url.toString()
                    result.title = view.title
                    if (action !== undefined && action !== null) result.action = action
                    if (pane.loadError.length > 0) result.load_error = pane.loadError
                    app.agentBrowserReply(id, result)
                })
            })
        }
        if (request.url) {
            loadError = ""
            view.url = request.url
            reply(null)
        } else if (request.back) {
            // Chromium hides entries reached by script clicks from goBack() (history
            // intervention); history.back() still returns to them.
            var before = view.url.toString()
            var steps = Math.max(1, Number(request.back) || 1)
            if (steps === 1 && view.canGoBack) view.goBack()
            else view.runJavaScript("history.go(-" + steps + ")")
            whenSettled(view, function() { reply({ went_back: view.url.toString() !== before }) })
        } else if (request.script) {
            view.runJavaScript(request.script, function(result) { reply(result) })
        } else {
            reply(null)
        }
    }
    Timer {
        id: settleTimer
        interval: 200
        repeat: true
        onTriggered: {
            var now = Date.now()
            var pending = []
            var ready = []
            for (var i = 0; i < pane.settleWaiters.length; ++i) {
                var waiter = pane.settleWaiters[i]
                if (now > waiter.deadline || (now >= waiter.earliest && !waiter.view.loading)) ready.push(waiter)
                else pending.push(waiter)
            }
            pane.settleWaiters = pending
            if (pending.length === 0) stop()
            for (var j = 0; j < ready.length; ++j) ready[j].callback()
        }
    }

    function handleDownload(download) {
        download.downloadDirectory = app.downloadsDirectory()
        download.accept()
        activeDownload = download
    }

    ListModel { id: tabsModel; ListElement { initialUrl: "" } }

    // The private profile keeps nothing on disk; the named one keeps logins.
    WebEngineProfile {
        id: privateProfile
        offTheRecord: true
        onDownloadRequested: function(download) { pane.handleDownload(download) }
    }
    WebEngineProfile {
        id: persistentProfile
        storageName: "navegador"
        persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
        onDownloadRequested: function(download) { pane.handleDownload(download) }
    }

    // Tab strip
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        ListView {
            id: tabStrip
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            orientation: ListView.Horizontal
            spacing: 4
            clip: true
            model: tabsModel
            boundsBehavior: Flickable.StopAtBounds
            delegate: Rectangle {
                required property int index
                readonly property var view: views.count > index ? views.itemAt(index) : null
                width: Math.min(200, Math.max(110, tabLabel.implicitWidth + 40))
                height: 32
                radius: 8
                color: index === pane.currentIndex ? theme.selected : (tabHover.hovered ? theme.sidebar : "transparent")
                border.color: index === pane.currentIndex ? theme.border : "transparent"
                HoverHandler { id: tabHover }
                TapHandler { onTapped: pane.currentIndex = index }
                TapHandler { acceptedButtons: Qt.MiddleButton; onTapped: pane.closeTab(index) }
                ToolTip.visible: tabHover.hovered
                ToolTip.text: view ? view.url.toString() : ""
                Text {
                    id: tabLabel
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: tabClose.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: view && view.title.length > 0 && view.url.toString() !== "about:blank"
                          ? view.title : "Nova aba"
                    color: theme.text
                    font.pointSize: Math.max(9, theme.fontSize - 2)
                    elide: Text.ElideRight
                }
                AppButton {
                    id: tabClose
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 26
                    implicitHeight: 26
                    leftPadding: 4
                    rightPadding: 4
                    ghost: true
                    icon.source: "qrc:/resources/icons/close.svg"
                    icon.width: 12
                    icon.height: 12
                    Accessible.name: "Fechar aba"
                    onClicked: pane.closeTab(index)
                }
            }
        }
        AppButton {
            ghost: true
            implicitWidth: 34
            icon.source: "qrc:/resources/icons/plus.svg"
            ToolTip.text: "Nova aba (Ctrl+T)"
            ToolTip.visible: hovered
            onClicked: pane.newTab("")
        }
    }

    // Toolbar
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        AppButton {
            ghost: true; implicitWidth: 36
            icon.source: "qrc:/resources/icons/arrow-left.svg"
            enabled: pane.current && pane.current.canGoBack
            onClicked: pane.current.goBack()
            ToolTip.text: "Voltar (Alt+←)"; ToolTip.visible: hovered
        }
        AppButton {
            ghost: true; implicitWidth: 36
            icon.source: "qrc:/resources/icons/arrow-right.svg"
            enabled: pane.current && pane.current.canGoForward
            onClicked: pane.current.goForward()
            ToolTip.text: "Avançar (Alt+→)"; ToolTip.visible: hovered
        }
        AppButton {
            ghost: true; implicitWidth: 36
            readonly property bool loading: pane.current && pane.current.loading
            icon.source: loading ? "qrc:/resources/icons/stop.svg" : "qrc:/resources/icons/refresh.svg"
            enabled: pane.current !== null
            onClicked: loading ? pane.current.stop() : pane.current.reload()
            ToolTip.text: loading ? "Parar" : "Recarregar (F5)"; ToolTip.visible: hovered
        }
        AppTextField {
            id: address
            objectName: "browserAddress"
            Layout.fillWidth: true
            placeholderText: "https://… ou localhost:3000"
            onAccepted: pane.navigate(text)
        }
        AppButton {
            ghost: true; implicitWidth: 36
            icon.source: "qrc:/resources/icons/paperclip.svg"
            enabled: pane.currentUrl !== "" && pane.currentUrl !== "about:blank"
            ToolTip.text: "Enviar a página para o chat"; ToolTip.visible: hovered
            onClicked: attachMenu.popup()
            Menu {
                id: attachMenu
                MenuItem { text: "Anexar texto da página"; onTriggered: pane.attachPageText() }
                MenuItem { text: "Anexar captura da página"; onTriggered: pane.attachScreenshot() }
                MenuItem { text: "Inserir link na mensagem"; onTriggered: app.composerTextRequested(pane.currentUrl) }
            }
        }
        AppButton {
            ghost: true; implicitWidth: 36
            icon.source: "qrc:/resources/icons/gear.svg"
            ToolTip.text: "Mais opções"; ToolTip.visible: hovered
            onClicked: moreMenu.popup()
            Menu {
                id: moreMenu
                MenuItem { text: "Buscar na página (Ctrl+F)"; onTriggered: pane.showFind() }
                MenuItem { text: "Aumentar zoom (Ctrl++)"; onTriggered: pane.zoom(0.1) }
                MenuItem { text: "Diminuir zoom (Ctrl+-)"; onTriggered: pane.zoom(-0.1) }
                MenuItem {
                    text: "Zoom normal (Ctrl+0) · " + Math.round((pane.current ? pane.current.zoomFactor : 1) * 100) + "%"
                    onTriggered: pane.zoom(0)
                }
                MenuSeparator {}
                MenuItem {
                    text: pane.devToolsOpen ? "Fechar ferramentas do desenvolvedor" : "Ferramentas do desenvolvedor (F12)"
                    onTriggered: pane.devToolsOpen = !pane.devToolsOpen
                }
                MenuItem {
                    text: "Abrir no navegador externo"
                    enabled: pane.currentUrl !== "" && pane.currentUrl !== "about:blank"
                    onTriggered: app.openExternal(pane.currentUrl)
                }
                MenuItem { text: "Abrir pasta de downloads"; onTriggered: app.openPath(app.downloadsDirectory()) }
                MenuSeparator {}
                MenuItem {
                    text: "Abrir links do chat aqui"
                    checkable: true
                    checked: app.openLinksInternally
                    onTriggered: app.openLinksInternally = checked
                }
                MenuItem {
                    text: "Manter logins e cookies"
                    checkable: true
                    checked: app.browserPersistent
                    onTriggered: app.browserPersistent = checked
                }
            }
        }
    }

    ProgressBar {
        Layout.fillWidth: true
        visible: pane.current && pane.current.loading
        value: pane.current ? pane.current.loadProgress / 100 : 0
    }

    // Find in page
    RowLayout {
        Layout.fillWidth: true
        visible: pane.findOpen
        spacing: 4
        AppTextField {
            id: findField
            objectName: "browserFind"
            Layout.fillWidth: true
            placeholderText: "Buscar na página…"
            onTextChanged: pane.find(false)
            onAccepted: pane.find(false)
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    pane.findOpen = false
                    if (pane.current) pane.current.findText("")
                    event.accepted = true
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                           && (event.modifiers & Qt.ShiftModifier)) {
                    pane.find(true)
                    event.accepted = true
                }
            }
        }
        Label { id: findCount; color: theme.muted }
        AppButton { ghost: true; implicitWidth: 34; icon.source: "qrc:/resources/icons/arrow-left.svg"; onClicked: pane.find(true); ToolTip.text: "Anterior (Shift+Enter)"; ToolTip.visible: hovered }
        AppButton { ghost: true; implicitWidth: 34; icon.source: "qrc:/resources/icons/arrow-right.svg"; onClicked: pane.find(false); ToolTip.text: "Próximo (Enter)"; ToolTip.visible: hovered }
        AppButton {
            ghost: true; implicitWidth: 34
            icon.source: "qrc:/resources/icons/close.svg"
            onClicked: { pane.findOpen = false; if (pane.current) pane.current.findText("") }
        }
    }

    // Notices: load errors, permissions, certificates and downloads.
    component Notice: Rectangle {
        default property alias content: noticeRow.data
        Layout.fillWidth: true
        implicitHeight: noticeRow.implicitHeight + 12
        radius: 8
        color: theme.sidebar
        border.color: theme.border
        RowLayout {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: 6
            spacing: 6
        }
    }
    Notice {
        visible: pane.loadError.length > 0
        Label { Layout.fillWidth: true; text: pane.loadError; color: theme.text; wrapMode: Text.Wrap }
        AppButton { ghost: true; icon.source: "qrc:/resources/icons/close.svg"; onClicked: pane.loadError = "" }
    }
    Notice {
        visible: pane.pendingPermission !== null
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.text
            text: pane.pendingPermission
                  ? pane.pendingPermission.origin.toString() + " quer " + pane.permissionLabel(pane.pendingPermission) + "."
                  : ""
        }
        AppButton { text: "Negar"; ghost: true; onClicked: { pane.pendingPermission.deny(); pane.pendingPermission = null } }
        AppButton { text: "Permitir"; primary: true; onClicked: { pane.pendingPermission.grant(); pane.pendingPermission = null } }
    }
    Notice {
        visible: pane.pendingCertificate !== null
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: theme.danger
            text: pane.pendingCertificate
                  ? "Certificado inválido em " + pane.pendingCertificate.url.toString() + ": " + pane.pendingCertificate.description
                  : ""
        }
        AppButton { text: "Voltar"; primary: true; onClicked: { pane.pendingCertificate.rejectCertificate(); pane.pendingCertificate = null } }
        AppButton { text: "Continuar mesmo assim"; danger: true; ghost: true; onClicked: { pane.pendingCertificate.acceptCertificate(); pane.pendingCertificate = null } }
    }
    Notice {
        id: downloadNotice
        visible: pane.activeDownload !== null
        readonly property bool finished: pane.activeDownload ? pane.activeDownload.isFinished : false
        IconImage { source: "qrc:/resources/icons/download.svg"; sourceSize.width: 16; sourceSize.height: 16; color: theme.text }
        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            color: theme.text
            text: {
                var d = pane.activeDownload
                if (!d) return ""
                if (d.state === WebEngineDownloadRequest.DownloadCompleted) return d.downloadFileName + " salvo em Downloads"
                if (d.state === WebEngineDownloadRequest.DownloadCancelled) return d.downloadFileName + " cancelado"
                if (d.state === WebEngineDownloadRequest.DownloadInterrupted) return d.downloadFileName + " falhou: " + d.interruptReasonString
                var percent = d.totalBytes > 0 ? Math.round(d.receivedBytes * 100 / d.totalBytes) + "%" : Math.round(d.receivedBytes / 1024) + " KB"
                return "Baixando " + d.downloadFileName + " · " + percent
            }
        }
        AppButton {
            visible: !downloadNotice.finished
            text: "Cancelar"; ghost: true
            onClicked: pane.activeDownload.cancel()
        }
        AppButton {
            visible: downloadNotice.finished
            text: "Abrir pasta"; ghost: true
            onClicked: app.openPath(pane.activeDownload.downloadDirectory)
        }
        AppButton { ghost: true; icon.source: "qrc:/resources/icons/close.svg"; onClicked: pane.activeDownload = null }
    }

    SplitView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        orientation: Qt.Vertical

        StackLayout {
            SplitView.fillHeight: true
            currentIndex: pane.currentIndex
            Repeater {
                id: views
                model: tabsModel
                delegate: WebEngineView {
                    required property int index
                    required property string initialUrl
                    objectName: index === 0 ? "workspaceWebView" : "browserTab" + index
                    profile: app.browserPersistent ? persistentProfile : privateProfile
                    backgroundColor: "white"
                    settings.localContentCanAccessFileUrls: false
                    settings.localContentCanAccessRemoteUrls: false
                    Component.onCompleted: if (initialUrl.length > 0) url = initialUrl
                    onUrlChanged: if (index === pane.currentIndex) address.text = url.toString() === "about:blank" ? "" : url.toString()
                    onNavigationRequested: function(request) {
                        var target = request.url.toString()
                        if (request.isMainFrame && target !== "about:blank" && !/^https?:\/\//i.test(target)) request.reject()
                    }
                    onNewWindowRequested: function(request) {
                        var target = request.requestedUrl.toString()
                        if (/^https?:\/\//i.test(target)) pane.newTab(target)
                    }
                    onLoadingChanged: function(info) {
                        if (index !== pane.currentIndex) return
                        if (info.status === WebEngineView.LoadFailedStatus && info.errorString.length > 0) pane.loadError = info.errorString
                        else if (info.status === WebEngineView.LoadStartedStatus) pane.loadError = ""
                    }
                    onPermissionRequested: function(permission) { pane.pendingPermission = permission }
                    onCertificateError: function(error) {
                        error.defer()
                        pane.pendingCertificate = error
                    }
                    onFindTextFinished: function(result) {
                        findCount.text = result.numberOfMatches > 0
                            ? result.activeMatch + "/" + result.numberOfMatches
                            : (findField.text.length > 0 ? "0" : "")
                    }
                }
            }
        }

        WebEngineView {
            id: devTools
            visible: pane.devToolsOpen
            SplitView.preferredHeight: 260
            SplitView.minimumHeight: 120
            profile: app.browserPersistent ? persistentProfile : privateProfile
            inspectedView: pane.devToolsOpen ? pane.current : null
        }
    }

    onCurrentIndexChanged: {
        address.text = currentUrl === "about:blank" ? "" : currentUrl
        loadError = ""
        findCount.text = ""
    }

    Shortcut { sequence: "Ctrl+T"; enabled: pane.visible; onActivated: pane.newTab("") }
    Shortcut { sequence: "Ctrl+W"; enabled: pane.visible; onActivated: pane.closeTab(pane.currentIndex) }
    Shortcut { sequence: "F5"; enabled: pane.visible && pane.current !== null; onActivated: pane.current.reload() }
    Shortcut { sequence: "F12"; enabled: pane.visible; onActivated: pane.devToolsOpen = !pane.devToolsOpen }
    Shortcut { sequence: "Ctrl+="; enabled: pane.visible; onActivated: pane.zoom(0.1) }
    Shortcut { sequence: "Ctrl++"; enabled: pane.visible; onActivated: pane.zoom(0.1) }
    Shortcut { sequence: "Ctrl+-"; enabled: pane.visible; onActivated: pane.zoom(-0.1) }
    Shortcut { sequence: "Ctrl+0"; enabled: pane.visible; onActivated: pane.zoom(0) }
    Shortcut { sequence: "Alt+Left"; enabled: pane.visible && pane.current && pane.current.canGoBack; onActivated: pane.current.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: pane.visible && pane.current && pane.current.canGoForward; onActivated: pane.current.goForward() }
}
