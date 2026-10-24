import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine

ColumnLayout {
    id: pane
    objectName: "browserPane"
    spacing: 6
    property alias currentUrl: web.url
    property string loadError: ""
    function navigate(text) {
        var address = text.trim()
        if (!address.length) return
        if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(address))
            address = /^(localhost|127\.0\.0\.1|\[::1\])([:/]|$)/.test(address) ? "http://" + address : "https://" + address
        if (!/^https?:\/\//i.test(address)) {
            loadError = "Use um endereço HTTP ou HTTPS."
            return
        }
        loadError = ""
        web.url = address
    }
    RowLayout {
        Layout.fillWidth: true
        AppButton { text: "‹"; enabled: web.canGoBack; onClicked: web.goBack(); ToolTip.text: "Voltar"; ToolTip.visible: hovered }
        AppButton { text: "›"; enabled: web.canGoForward; onClicked: web.goForward(); ToolTip.text: "Avançar"; ToolTip.visible: hovered }
        AppButton { text: web.loading ? "Parar" : "Atualizar"; onClicked: web.loading ? web.stop() : web.reload() }
        AppTextField {
            id: address
            objectName: "browserAddress"
            Layout.fillWidth: true
            placeholderText: "https://… ou localhost:3000"
            onAccepted: pane.navigate(text)
        }
        AppButton { text: "Ir"; primary: true; onClicked: pane.navigate(address.text) }
        AppButton { icon.source: "qrc:/resources/icons/open.svg"; ToolTip.text: "Abrir no navegador externo"; ToolTip.visible: hovered; onClicked: app.openExternal(web.url.toString()) }
    }
    ProgressBar { Layout.fillWidth: true; visible: web.loading; value: web.loadProgress / 100 }
    Label { Layout.fillWidth: true; visible: pane.loadError.length > 0; text: pane.loadError; color: theme.muted; wrapMode: Text.Wrap }
    WebEngineProfile {
        id: browsingProfile
        offTheRecord: true
        onDownloadRequested: function(download) { download.cancel(); pane.loadError = "Para baixar arquivos, abra a página no navegador externo." }
    }
    WebEngineView {
        id: web
        objectName: "workspaceWebView"
        Layout.fillWidth: true
        Layout.fillHeight: true
        profile: browsingProfile
        backgroundColor: "white"
        settings.localContentCanAccessFileUrls: false
        settings.localContentCanAccessRemoteUrls: false
        onUrlChanged: address.text = url.toString() === "about:blank" ? "" : url.toString()
        onNavigationRequested: function(request) {
            var target = request.url.toString()
            if (target !== "about:blank" && !/^https?:\/\//i.test(target)) request.reject()
        }
        onNewWindowRequested: function(request) {
            var target = request.requestedUrl.toString()
            if (/^https?:\/\//i.test(target)) pane.navigate(target)
        }
        onLoadingChanged: function(info) {
            if (info.status === WebEngineView.LoadFailedStatus) pane.loadError = info.errorString
            else if (info.status === WebEngineView.LoadStartedStatus) pane.loadError = ""
        }
    }
}
