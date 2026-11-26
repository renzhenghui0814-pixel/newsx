import QtQuick
import QtQuick.Controls
import NewsX

Pane {
    id: root
    padding: 0
    background: Rectangle { color: root.pageBackground }
    palette.buttonText: foreground
    palette.button: surface
    required property var appController
    property string topic: ""
    property real fontScale: 1.15
    property color foreground: "#f2f2f2"
    property color secondary: "#bdbdbd"
    property color surface: "#171717"
    property color pageBackground: "#000000"
    property bool darkTheme: true
    property bool unobscured: true
    property bool openingFromSource: false
    property bool pointerOverWindow: false
    readonly property bool pageReady: browser.contentReady
    readonly property bool pageFailed: browser.errorString.length > 0
    readonly property bool loginBlockedBeforeContent: browser.loginRequired && !browser.contentReady
    property bool pagePresented: false
    readonly property bool waitingForPage: !pagePresented && !browser.contentReady && !browser.loginRequired && !pageFailed
    property bool loadingDelayElapsed: false
    readonly property string mobileUrl: "https://m.weibo.cn/p/index?containerid="
        + encodeURIComponent("100103type=1&q=#" + topic + "#") + "&page_type=searchall"
    signal back()
    signal fallback()
    function refresh() { browser.reload() }
    function isPointerOverWindow() { return browser.isPointerOverWindow() }
    Timer {
        running: root.visible && root.unobscured
        triggeredOnStart: true
        repeat: true
        interval: 40
        onTriggered: root.pointerOverWindow = root.isPointerOverWindow()
        onRunningChanged: if (!running) root.pointerOverWindow = false
    }
    DetailHeader {
        id: header
        width: parent.width
        title: "微博官网"
        fontScale: root.fontScale
        foreground: root.foreground
        secondary: root.secondary
        externalUrl: browser.currentUrl.toString() || root.mobileUrl
        rightActionIcon: "\ue72c"
        rightActionHint: "刷新网页"
        onRightAction: root.refresh()
        onBack: root.back()
        onExternal: root.appController.openExternal(externalUrl)
    }
    WeiboWebView {
        id: browser
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: loginNotice.visible && browser.contentReady ? loginNotice.top : parent.bottom
        url: root.mobileUrl
        session: root.appController.weiboSession
        darkTheme: root.darkTheme
        backgroundColor: root.pageBackground
        hideSiteBack: true
        visible: root.unobscured
        onCloseRequested: root.back()
        onContentReadyChanged: if (contentReady) root.pagePresented = true
    }
    Column {
        anchors.centerIn: browser
        width: Math.min(parent.width - 24, 400)
        spacing: 14
        visible: browser.errorString.length > 0
        Text {
            width: parent.width
            text: browser.errorString
            color: root.foreground
            font.pixelSize: 13
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Button { text: "重试网页"; onClicked: browser.reload() }
            Button { text: "备用阅读"; onClicked: root.fallback() }
        }
    }
    Timer {
        interval: 400
        running: root.waitingForPage
        onTriggered: root.loadingDelayElapsed = true
    }
    onWaitingForPageChanged: if (!waitingForPage) loadingDelayElapsed = false
    Text {
        anchors.centerIn: browser
        visible: root.waitingForPage && root.loadingDelayElapsed
        text: "正在加载微博…"
        color: root.secondary
        font.pixelSize: Math.round(12 * root.fontScale)
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    Text {
        id: loginNotice
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        visible: !root.openingFromSource && browser.loginRequired && browser.errorString.length === 0
        height: Math.round(32 * root.fontScale)
        verticalAlignment: Text.AlignVCenter
        text: "登录后加载更多"
        color: root.secondary
        font.pixelSize: Math.round(12 * root.fontScale)
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
