import QtQuick
import QtQuick.Controls
import NewsX

Pane {
    id: root
    padding: 0
    required property var appController
    property color foreground: "#f2f2f2"
    property color secondary: "#bdbdbd"
    property color surface: "#171717"
    property color pageBackground: "#000000"
    property bool darkTheme: true
    property real fontScale: 1.15
    property bool unobscured: true
    property bool pointerOverWindow: false
    readonly property string homeUrl: "https://www.xiaohongshu.com/explore"
    signal back()
    background: Rectangle { color: root.pageBackground }
    palette.buttonText: foreground
    palette.button: surface
    function refresh() { browser.reload() }
    function goBack() { root.back() }
    function isPointerOverWindow() { return browser.isPointerOverWindow() }
    function openExternal() { root.appController.openExternal(browser.currentUrl.toString() || root.homeUrl) }
    Timer {
        running: root.visible && root.unobscured
        triggeredOnStart: true
        repeat: true
        interval: 40
        onTriggered: root.pointerOverWindow = root.isPointerOverWindow()
        onRunningChanged: if (!running) root.pointerOverWindow = false
    }
    DetailHeader {
        id: titleBar
        width: parent.width
        title: "小红书"
        fontScale: root.fontScale
        foreground: root.foreground
        secondary: root.secondary
        rightActionIcon: "\ue72c"
        rightActionHint: "刷新网页"
        onBack: root.back()
        onRightAction: root.refresh()
    }
    ProgressBar {
        id: progress
        anchors.top: titleBar.bottom
        width: parent.width
        height: 2
        indeterminate: browser.loading
        opacity: browser.loading ? 1 : 0
    }
    WeiboWebView {
        id: browser
        anchors.top: progress.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        site: "xiaohongshu"
        url: root.homeUrl
        darkTheme: root.darkTheme
        backgroundColor: root.pageBackground
        visible: root.unobscured
        onCloseRequested: root.back()
    }
    Column {
        anchors.centerIn: browser
        width: Math.max(0, parent.width - 24)
        spacing: 12
        visible: !browser.contentReady && !browser.errorString.length
        BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible }
        Text {
            width: parent.width
            text: "正在打开小红书…"
            color: root.secondary
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: text
        }
    }
    Column {
        anchors.centerIn: browser
        width: Math.max(0, parent.width - 24)
        spacing: 12
        visible: browser.errorString.length > 0
        Text {
            width: parent.width
            text: browser.errorString
            wrapMode: Text.Wrap
            color: root.foreground
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8
            Button { text: "重试"; onClicked: root.refresh() }
            Button { text: "浏览器打开"; onClicked: root.openExternal() }
        }
    }
}
