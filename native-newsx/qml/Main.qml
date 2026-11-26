import QtQuick
import QtQuick.Controls
import NewsX
import "components"

Window {
    id: window
    width: app.initialWidth
    height: compactActive ? compactCardHeight : expandedHeight
    x: app.initialX < 0 ? Screen.width - width - 36 : app.initialX
    y: app.initialY < 0 ? 60 : app.initialY
    minimumWidth: 332
    minimumHeight: minimalModeEnabled ? 1 : 390
    maximumWidth: 900
    maximumHeight: 1040
    visible: true
    title: "NewsX"
    color: "transparent"
    opacity: app.opacity
    flags: Qt.Tool | Qt.FramelessWindowHint | (app.alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)

    property string page: "home"
    property bool weiboUseFallback: false
    property bool weiboOpening: false
    property bool weiboLoginNotice: false
    property string selectedSymbol: ""
    property string selectedTitle: ""
    property string selectedUrl: ""
    property string sourcePage: "home"
    property int playingPost: -1
    property var modeMenu: null
    readonly property bool modeMenuVisible: modeMenu ? modeMenu.visible : false
    readonly property bool modeMenuHovered: modeMenu ? modeMenuVisible && modeMenu.pointerInside : false
    readonly property bool shellHovered: shellHover.hovered || modeMenuHovered || (websiteLoader.item
        ? (websiteLoader.item.pointerOverWindow || websiteLoader.item.isPointerOverWindow()) : false)
        || (xiaohongshuLoader.item ? (xiaohongshuLoader.item.pointerOverWindow
            || xiaohongshuLoader.item.isPointerOverWindow()) : false)
    property bool controlsVisible: (shellHovered || window.modeMenuVisible) && headerReady && !compactActive
    readonly property string windowMode: app.windowMode
    property bool minimalModeEnabled: app.minimalMode
    property bool shouldCompact: minimalModeEnabled && page === "home" && !weiboOpening && !expandOnHover && !window.modeMenuVisible
    property bool compactActive: false
    property bool headerReady: !app.minimalMode
    property bool animationsReady: false
    property bool expandOnHover: false
    property string compactCardKind: app.minimalCard
    readonly property real compactCardHeight: Math.max(stocksCard.implicitHeight,
        hotCard.implicitHeight, clsCard.implicitHeight)
    property string nextCompactCardKind: "stocks"
    property int cardSwitchDirection: 1
    property int expandedHeight: app.initialHeight
    property bool opacityOpen: false
    property string stockPopup: ""
    property string selectedExchange: "SH"
    property string stockPopupError: ""
    property bool frequencyOpen: false
    property bool weiboAccountOpen: false
    property string frequencyError: ""
    property bool dark: app.theme === "dark"
    property real imageScale: Math.max(1, Math.min(2, window.screen ? window.screen.devicePixelRatio : 1))
    property real fontScale: 1.15
    property color canvasColor: dark ? "#000000" : "#eff3f6"
    property color cardColor: dark ? "#171717" : "#f9fbfc"
    property color foreground: dark ? "#f2f2f2" : "#213340"
    property color secondary: dark ? "#bdbdbd" : "#617480"
    property color divider: dark ? "#373737" : "#d8e2e8"
    property color accent: dark ? "#8cc8ef" : "#397fbd"
    property color linkInk: dark ? "#68d7ff" : "#176da9"
    palette: dark ? darkControls : undefined
    Palette {
        id: darkControls
        window: "#171717"
        windowText: "#f2f2f2"
        base: "#111111"
        text: "#f2f2f2"
        placeholderText: "#bdbdbd"
        button: "#262626"
        buttonText: "#f2f2f2"
        toolTipBase: "#222222"
        toolTipText: "#f2f2f2"
        mid: "#373737"
        dark: "#303030"
        brightText: "#f2f2f2"
        highlight: "#8cc8ef"
        highlightedText: "#000000"
        link: "#68d7ff"
    }
    Component.onCompleted: {
        compactActive = app.minimalMode
        Qt.callLater(() => animationsReady = true)
    }
    onShellHoveredChanged: {
        if (shellHovered) {
            if (!compactActive && minimalModeEnabled && page === "home") expandOnHover = true
        } else {
            expandOnHover = false
            opacityOpen = false
        }
    }
    onShouldCompactChanged: {
        if (shouldCompact) {
            headerRevealTimer.stop()
            headerReady = false
            collapseTimer.restart()
        } else {
            collapseTimer.stop()
            if (compactActive) {
                compactActive = false
                headerRevealTimer.restart()
            } else {
                headerReady = true
            }
        }
    }
    onCompactActiveChanged: {
        if (compactActive) {
            geometryTimer.stop()
            opacityOpen = false
            homeView.contentY = 0
        } else {
            cardSwitchAnimation.stop()
            compactCard.switchOpacity = 1
            compactCard.switchOffset = 0
        }
    }
    EdgeHideController {
        id: edgeHide
        hostWindow: window
        enabled: window.windowMode === "hidden"
        blocked: window.modeMenuVisible || window.opacityOpen || window.frequencyOpen
            || window.weiboAccountOpen || window.stockPopup.length > 0 || compactHeightAnimation.running
        onSettled: geometryTimer.restart()
    }
    onPageChanged: if (page === "home") app.requestIdleCleanup()

    function closeModeMenu() {
        if (modeMenu) modeMenu.close()
    }
    function toggleModeMenu(button) {
        if (modeMenuVisible) { closeModeMenu(); return }
        opacityOpen = false
        const popup = modeMenuComponent.createObject(shell)
        if (!popup) return
        modeMenu = popup
        const point = button.mapToItem(shell, 0, button.height)
        popup.x = Math.max(12, Math.min(shell.width - popup.width - 12, point.x))
        popup.y = point.y + 4
        popup.currentIndex = -1
        popup.open()
    }
    function selectWindowMode(value) {
        closeModeMenu()
        // Let the popup release mouse capture before changing window geometry.
        Qt.callLater(() => {
            window.expandOnHover = value === "minimal" && window.shellHovered
            app.setWindowMode(value)
        })
    }
    function fs(size) { return Math.round(size * fontScale) }
    function marketHeaderTime(value) {
        const raw = String(value || "")
        return /^\d{2}:\d{2}:\d{2}$/.test(raw)
            ? Qt.formatDate(new Date(), "MM-dd") + " " + raw.slice(0, 5) : raw
    }

    function showPage(value) {
        playingPost = -1
        stockPopup = ""
        frequencyOpen = false
        weiboAccountOpen = false
        app.cancelMedia()
        page = value
        weiboOpening = false
        weiboLoginNotice = false
    }
    function openWeiboAccount() {
        frequencyOpen = false
        stockPopup = ""
        opacityOpen = false
        weiboAccountOpen = !weiboAccountOpen
    }
    function openFrequency() {
        weiboAccountOpen = false
        stockPopup = ""
        frequencyError = ""
        frequencyMinutesInput.text = String(page === "weibo" ? app.hotRefreshMinutes : app.headlineRefreshMinutes)
        frequencyOpen = true
        frequencyMinutesInput.forceActiveFocus()
        frequencyMinutesInput.selectAll()
    }
    function saveFrequency() {
        const raw = frequencyMinutesInput.text.trim()
        if (!/^\d+$/.test(raw) || Number(raw) < 1 || Number(raw) > 1440) {
            frequencyError = "请输入 1–1440 分钟"
            return
        }
        if (page === "weibo") app.setHotRefreshMinutes(Number(raw))
        else if (page === "cls") app.setHeadlineRefreshMinutes(Number(raw))
        frequencyOpen = false
        frequencyError = ""
    }
    function saveStockPopup() {
        if (stockPopup === "add") {
            if (!/^\d{6}$/.test(stockCode.text.trim()) ||
                    !app.addStock(selectedExchange + stockCode.text.trim())) {
                stockPopupError = "请输入有效且未添加的六位代码"
                return
            }
            stockCode.text = ""
        } else if (stockPopup === "site") {
            if (!app.setFront(siteAddress.text.trim())) {
                stockPopupError = "地址格式：tcp://主机:端口"
                return
            }
        }
        stockPopup = ""
        stockPopupError = ""
    }
    function goBack() {
        if (page === "xiaohongshu" && xiaohongshuLoader.item) xiaohongshuLoader.item.goBack()
        else if (weiboOpening) weiboOpening = false
        else if (page === "quote" || page === "weiboDetail" || page === "article") showPage(sourcePage)
        else showPage("home")
    }
    function openQuote(symbol) { sourcePage = page; selectedSymbol = symbol; showPage("quote") }
    function openHot(title, url, rank) {
        const target = "https://m.weibo.cn/p/index?containerid="
            + encodeURIComponent("100103type=1&q=#" + title + "#") + "&page_type=searchall"
        weiboOpening = false
        weiboLoginNotice = false
        selectedTitle = title
        selectedUrl = target
        weiboUseFallback = false
        sourcePage = page
        weiboOpening = true
    }
    function escapePostText(value) {
        return String(value || "").replace(/&/g, "&amp;").replace(/</g, "&lt;")
            .replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/\n/g, "<br>")
    }
    function richPostText(value, links, expanded) {
        const raw = String(value || "")
        const shown = expanded ? raw : raw.slice(0, 150)
        const pattern = /https?:\/\/[^\s<>]+|#[^#\n]+#|@[\u4e00-\u9fa5A-Za-z0-9_-]{1,30}/g
        const candidates = []
        const namedLinks = links || []
        for (let i = 0; i < namedLinks.length; ++i) {
            const label = String(namedLinks[i].label || "")
            if (!label) continue
            let position = shown.indexOf(label)
            while (position >= 0) {
                candidates.push({start: position, label: label, url: String(namedLinks[i].url || "")})
                position = shown.indexOf(label, position + label.length)
            }
        }
        let match
        while ((match = pattern.exec(shown)) !== null) {
            const token = match[0]
            candidates.push({start: match.index, label: token,
                url: token.startsWith("#") ? "https://s.weibo.com/weibo?q=" + encodeURIComponent(token)
                    : token.startsWith("@") ? "https://s.weibo.com/user?q=" + encodeURIComponent(token.slice(1))
                    : token})
        }
        candidates.sort((a, b) => a.start - b.start || b.label.length - a.label.length)
        let html = ""
        let cursor = 0
        for (let i = 0; i < candidates.length; ++i) {
            const link = candidates[i]
            if (link.start < cursor || !link.url) continue
            html += escapePostText(shown.slice(cursor, link.start))
            html += '<a style="color:' + window.linkInk + ';text-decoration:none" href="'
                + escapePostText(link.url) + '">' + escapePostText(link.label) + '</a>'
            cursor = link.start + link.label.length
        }
        html += escapePostText(shown.slice(cursor))
        if (raw.length > 150)
            html += expanded ? ' <a style="color:' + window.linkInk + '" href="newsx:collapse">收起</a>'
                : '…<a style="color:' + window.linkInk + '" href="newsx:expand">展开</a>'
        return html
    }
    function postTime(value) {
        const raw = String(value || "")
        const match = raw.match(/^\w+ (\w+) (\d+) (\d\d:\d\d):\d\d [+-]\d{4} (\d{4})$/)
        if (!match) return raw
        const month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"].indexOf(match[1]) + 1
        return month > 0 ? match[4] + "-" + String(month).padStart(2, "0")
            + "-" + match[2].padStart(2, "0") + " " + match[3] : raw
    }
    function compactCount(value) {
        const number = Number(value || 0)
        return number >= 10000 ? (number / 10000).toFixed(1) + "万" : String(number)
    }
    function openHeadline(url, title) { sourcePage = page; selectedTitle = title; selectedUrl = url; showPage("article"); app.openArticle(url) }
    onXChanged: geometryTimer.restart()
    onYChanged: geometryTimer.restart()
    onWidthChanged: if (!compactActive) geometryTimer.restart()
    onHeightChanged: {
        if (!compactActive) {
            if (!compactHeightAnimation.running) expandedHeight = height
            geometryTimer.restart()
        }
    }
    Behavior on height {
        enabled: window.animationsReady
        NumberAnimation {
            id: compactHeightAnimation
            duration: window.compactActive ? 420 : 180
            easing.type: Easing.InOutCubic
            onRunningChanged: {
                if (!running && !window.compactActive && !window.shouldCompact
                        && Math.abs(window.height - window.expandedHeight) < 1) {
                    headerRevealTimer.stop()
                    window.headerReady = true
                }
            }
        }
    }
    Timer {
        id: geometryTimer
        interval: 350
        onTriggered: {
            const saved = edgeHide.savedGeometry()
            app.saveWindow(saved.x, saved.y, window.width, window.expandedHeight)
        }
    }
    Timer { id: collapseTimer; interval: 230; onTriggered: if (window.shouldCompact) window.compactActive = true }
    Timer {
        id: headerRevealTimer
        interval: 200
        onTriggered: if (!compactHeightAnimation.running && !window.shouldCompact
                && Math.abs(window.height - window.expandedHeight) < 1) window.headerReady = true
    }

    Shortcut { sequence: "Escape"; onActivated: { if (window.modeMenuVisible) window.closeModeMenu(); else if (weiboAccountOpen) weiboAccountOpen = false; else if (frequencyOpen) frequencyOpen = false; else if (stockPopup.length > 0) stockPopup = ""; else if (opacityOpen) opacityOpen = false; else window.goBack() } }
    Shortcut { sequence: "Ctrl+1"; onActivated: window.showPage("stocks") }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.showPage("weibo") }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.showPage("cls") }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.showPage("cls") }
    Shortcut { sequence: "Ctrl+5"; onActivated: window.showPage("xiaohongshu") }
    Shortcut {
        sequence: "Ctrl+R"
        onActivated: {
            if (xiaohongshuLoader.item && xiaohongshuLoader.visible) xiaohongshuLoader.item.refresh()
            else if (websiteLoader.item && websiteLoader.visible) websiteLoader.item.refresh()
            else if (page === "weibo") app.refreshHot()
            else if (page === "cls") app.refreshHeadlines()
            else app.refresh()
        }
    }

    Rectangle {
        id: shell
        anchors.fill: parent
        radius: 18
        color: window.compactActive ? "transparent" : window.canvasColor
        border.color: window.compactActive ? "transparent" : window.divider
        border.width: 1
        clip: true
        Behavior on color { ColorAnimation { duration: 190 } }
        Behavior on border.color { ColorAnimation { duration: 190 } }

        HoverHandler {
            id: shellHover
        }

        Item {
            id: header
            z: 11
            x: 12
            y: 4
            width: parent.width - 24
            height: 52
            opacity: window.compactActive ? 0 : 1
            enabled: !window.compactActive
            Behavior on opacity { NumberAnimation { duration: 150 } }
            MouseArea { anchors.fill: parent; onPressed: { window.opacityOpen = false; window.startSystemMove() } }
            Row {
                id: brand
                spacing: 7
                x: window.controlsVisible ? 6 : (header.width - width) / 2
                anchors.verticalCenter: parent.verticalCenter
                Behavior on x { NumberAnimation { duration: 170; easing.type: Easing.OutCubic } }
                Rectangle { width: 26; height: 26; radius: 8; color: window.accent; anchors.verticalCenter: parent.verticalCenter
                    Text { text: "N"; anchors.centerIn: parent; color: window.canvasColor; font.family: "Segoe UI"; font.weight: Font.Bold; font.pixelSize: window.fs(15) }
                }
                Text { text: "NewsX"; color: window.foreground; font.family: "Segoe UI"; font.weight: Font.DemiBold; font.pixelSize: window.fs(18); anchors.verticalCenter: parent.verticalCenter }
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: window.width < 390 ? 4 : 8
                opacity: window.controlsVisible ? 1 : 0
                enabled: window.controlsVisible
                Behavior on opacity { NumberAnimation { duration: 130 } }
                Repeater {
                    model: [ {hint: "小红书", action: "xiaohongshu"},
                             {hint: "模式切换", action: "mode"},
                             {hint: "切换主题", action: "theme"},
                             {hint: "调整透明度", action: "opacity"},
                             {hint: "关闭组件", action: "close"} ]
                    delegate: Rectangle {
                        required property var modelData
                        property color iconColor: actionArea.containsMouse || xiaohongshuButton.hovered ? window.accent : window.secondary
                        width: window.width < 390 ? 32 : 40
                        height: 40; radius: 10
                        color: actionArea.containsMouse || xiaohongshuButton.hovered ? (window.dark ? "#2a2a2a" : "#dae7ee") : "transparent"
                        ToolButton {
                            id: xiaohongshuButton
                            visible: modelData.action === "xiaohongshu"
                            anchors.fill: parent
                            icon.source: "qrc:/resources/xiaohongshu.svg"
                            icon.width: 24; icon.height: 24
                            icon.color: parent.iconColor
                            background: Item {}
                            Accessible.name: "打开小红书"
                            onClicked: window.showPage("xiaohongshu")
                            ToolTip.visible: hovered && window.controlsVisible
                            ToolTip.text: "小红书"
                        }
                        Text {
                            visible: modelData.action !== "opacity" && modelData.action !== "mode" && modelData.action !== "xiaohongshu"
                            anchors.centerIn: parent
                            text: modelData.action === "theme" ? (window.dark ? "\ue706" : "\ue708") : "\ue8bb"
                            color: parent.iconColor
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: window.fs(modelData.action === "close" ? 15 : 18)
                        }
                        ToolButton {
                            id: modeButton
                            visible: modelData.action === "mode"
                            hoverEnabled: false
                            anchors.fill: parent
                            icon.source: "qrc:/resources/mode-" + window.windowMode + ".svg"
                            icon.width: 22; icon.height: 22
                            icon.color: parent.iconColor
                            background: Item {}
                            Accessible.name: "模式切换"
                            Accessible.description: window.windowMode === "normal" ? "当前为常规模式"
                                : window.windowMode === "minimal" ? "当前为极简模式" : "当前为隐藏模式"
                            onClicked: window.toggleModeMenu(modeButton)
                        }
                        Canvas {
                            visible: modelData.action === "opacity"
                            anchors.centerIn: parent
                            width: 20; height: 20
                            property color ink: parent.iconColor
                            onInkChanged: requestPaint()
                            onPaint: {
                                const painter = getContext("2d")
                                painter.clearRect(0, 0, width, height)
                                painter.fillStyle = ink
                                painter.strokeStyle = ink
                                painter.lineWidth = 1.7
                                painter.beginPath()
                                painter.arc(10, 10, 5.8, Math.PI / 2, Math.PI * 1.5)
                                painter.closePath()
                                painter.fill()
                                painter.beginPath()
                                painter.arc(10, 10, 7.2, 0, Math.PI * 2)
                                painter.stroke()
                            }
                        }
                        MouseArea { id: actionArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            visible: modelData.action !== "xiaohongshu"
                            onClicked: {
                                if (modelData.action === "mode") window.toggleModeMenu(modeButton)
                                else if (modelData.action === "theme") { window.opacityOpen = false; app.toggleTheme() }
                                else if (modelData.action === "opacity") window.opacityOpen = !window.opacityOpen
                                else window.close()
                            }
                            ToolTip.visible: containsMouse && window.controlsVisible && !window.modeMenuVisible
                            ToolTip.text: modelData.hint
                        }
                    }
                }
            }
        }

        Component {
            id: modeMenuComponent
            Menu {
                id: popup
                readonly property bool pointerInside: modeMenuPointer.hovered
                parent: shell
                width: 190
                padding: 6
                // A separate popup window stays above the native WebView2 child
                // without hiding or interrupting the page beneath the menu.
                popupType: Popup.Window
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                onClosed: {
                    currentIndex = -1
                    // Do not retain a hidden popup window or its previous input state.
                    const closedPopup = popup
                    Qt.callLater(() => {
                        if (window.modeMenu === closedPopup) window.modeMenu = null
                        closedPopup.destroy()
                    })
                }
                palette.window: window.cardColor
                palette.windowText: window.foreground
                palette.text: window.foreground
                palette.light: Qt.tint(window.cardColor, Qt.rgba(window.accent.r, window.accent.g, window.accent.b, 0.14))
                palette.midlight: Qt.tint(window.cardColor, Qt.rgba(window.accent.r, window.accent.g, window.accent.b, 0.24))
                palette.dark: window.divider
                background: Rectangle {
                    radius: 14
                    color: window.cardColor
                    border.color: window.divider
                    border.width: 1
                }
                contentItem: ListView {
                    implicitHeight: contentHeight
                    model: popup.contentModel
                    currentIndex: popup.currentIndex
                    interactive: false
                    clip: true
                    spacing: 2
                    highlightMoveDuration: 0
                    highlightResizeDuration: 0
                    HoverHandler { id: modeMenuPointer }
                }
                ModeMenuItem {
                    text: "常规模式"; modeValue: "normal"
                    currentMode: window.windowMode
                    pointerInMenu: modeMenuPointer.hovered
                    fontScale: window.fontScale
                    foreground: window.foreground; accent: window.accent
                    onTriggered: window.selectWindowMode(modeValue)
                }
                ModeMenuItem {
                    text: "极简模式"; modeValue: "minimal"
                    currentMode: window.windowMode
                    pointerInMenu: modeMenuPointer.hovered
                    fontScale: window.fontScale
                    foreground: window.foreground; accent: window.accent
                    onTriggered: window.selectWindowMode(modeValue)
                }
                ModeMenuItem {
                    text: "隐藏模式"; modeValue: "hidden"
                    currentMode: window.windowMode
                    pointerInMenu: modeMenuPointer.hovered
                    fontScale: window.fontScale
                    foreground: window.foreground; accent: window.accent
                    onTriggered: window.selectWindowMode(modeValue)
                }
            }
        }

        Rectangle {
            z: 30
            visible: edgeHide.hidden
            readonly property bool horizontalHandle: edgeHide.dockEdge === Qt.TopEdge || edgeHide.dockEdge === Qt.BottomEdge
            x: horizontalHandle ? (parent.width - width) / 2
                : edgeHide.dockEdge === Qt.LeftEdge ? parent.width - width : 0
            y: horizontalHandle ? (edgeHide.dockEdge === Qt.TopEdge ? parent.height - height : 0)
                : (parent.height - height) / 2
            width: horizontalHandle ? Math.min(64, parent.width - 32) : 4
            height: horizontalHandle ? 4 : Math.min(64, parent.height - 32)
            radius: 2
            color: window.accent
        }

        MouseArea {
            anchors.fill: parent
            z: 8
            visible: window.opacityOpen
            onClicked: window.opacityOpen = false
        }

        Rectangle {
            visible: window.opacityOpen
            z: 9
            x: opacityPanel.x + 2; y: opacityPanel.y + 4
            width: opacityPanel.width; height: opacityPanel.height
            radius: 16; color: "#22000000"
        }

        Rectangle {
            id: opacityPanel
            visible: window.opacityOpen
            z: 10
            width: 78; height: 178; radius: 16
            x: shell.width - width - 14; y: 55
            color: window.cardColor
            border.color: window.divider
            border.width: 1
            Text {
                text: Math.round(app.opacity * 100) + "%"
                anchors.top: parent.top; anchors.topMargin: 12
                anchors.horizontalCenter: parent.horizontalCenter
                color: window.foreground; font.family: "Segoe UI"; font.pixelSize: window.fs(12)
            }
            Slider {
                id: opacitySlider
                anchors.horizontalCenter: parent.horizontalCenter
                y: 38; width: 34; height: 122
                orientation: Qt.Vertical
                from: 0.45; to: 1.0; value: app.opacity
                onMoved: app.setOpacity(value)
                background: Rectangle {
                    x: (opacitySlider.width - width) / 2
                    y: opacitySlider.topPadding
                    width: 4; height: opacitySlider.availableHeight
                    radius: 2
                    color: window.dark ? "#484848" : "#d3dee4"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: parent.height * (opacitySlider.value - opacitySlider.from)
                            / (opacitySlider.to - opacitySlider.from)
                        radius: 2; color: window.accent
                    }
                }
                handle: Rectangle {
                    x: (opacitySlider.width - width) / 2
                    y: opacitySlider.topPadding +
                       (1 - (opacitySlider.value - opacitySlider.from)
                        / (opacitySlider.to - opacitySlider.from))
                       * (opacitySlider.availableHeight - height)
                    width: 14; height: 14; radius: 7
                    color: window.accent
                }
            }
        }

        Item {
            id: content
            x: window.compactActive ? 0 : 14
            y: window.compactActive ? 0 : 61
            width: shell.width - (window.compactActive ? 0 : 28)
            height: shell.height - (window.compactActive ? 0 : 74)
            Behavior on x { NumberAnimation { duration: window.compactActive ? 420 : 180; easing.type: Easing.InOutCubic } }
            Behavior on y { NumberAnimation { duration: window.compactActive ? 420 : 180; easing.type: Easing.InOutCubic } }
            Behavior on width { NumberAnimation { duration: window.compactActive ? 420 : 180; easing.type: Easing.InOutCubic } }

            Flickable {
                id: homeView
                //拖动不滚动页面，只允许滚轮与滚动条
                interactive: false
                WheelScroller { view: homeView }
                anchors.fill: parent
                visible: window.page === "home" && !window.weiboOpening
                opacity: window.compactActive ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 160 } }
                contentWidth: width
                contentHeight: homeColumn.implicitHeight
                clip: true
                ScrollBarIndicator { host: shell; view: homeView; offsetY: content.y; hovered: shellHover.hovered && window.headerReady && !window.shouldCompact && !window.compactActive && !compactHeightAnimation.running; ink: window.dark ? "#a0a0a0" : "#7e97a4" }
                Column {
                    id: homeColumn
                    width: homeView.width
                    spacing: 12
                    NewsCard {
                        id: stocksCard
                        width: parent.width
                        fontScale: window.fontScale
                        title: "自选行情"
                        timestamp: window.marketHeaderTime(app.marketTime)
                        surface: window.cardColor; foreground: window.foreground; secondary: window.secondary
                        rows: app.stocks.filter(item => item.followed)
                        columnHeader: Component {
                            Item {
                                width: parent ? parent.width : 300; height: Math.round(26 * window.fontScale)
                                Text { text: "名称 / 代码"; color: window.secondary; font.pixelSize: window.fs(11); anchors.left: parent.left; anchors.leftMargin: 4; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: "最新价 / 涨跌幅"; color: window.secondary; font.pixelSize: window.fs(11); width: Math.round(100 * window.fontScale); horizontalAlignment: Text.AlignRight; anchors.right: amountHeader.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter }
                                Text { id: amountHeader; text: "成交额"; color: window.secondary; font.pixelSize: window.fs(11); width: Math.round(78 * window.fontScale); horizontalAlignment: Text.AlignRight; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter }
                            }
                        }
                        rowDelegate: Component { StockRow { showTurnover: true; fontScale: window.fontScale; darkMode: window.dark; foreground: window.foreground; secondary: window.secondary; onOpened: symbol => window.openQuote(symbol) } }
                        onOpened: window.showPage("stocks")
                    }
                    Item {
                        property bool wide: homeColumn.width >= 610
                        width: parent.width
                        height: wide ? Math.max(hotCard.height, clsCard.height) : hotCard.height + clsCard.height + 12
                        NewsCard {
                            id: hotCard
                            fontScale: window.fontScale
                            title: "微博热搜"; timestamp: app.hotTime
                            width: parent.wide ? (parent.width - 12) / 2 : parent.width
                            surface: window.cardColor; foreground: window.foreground; secondary: window.secondary
                            rows: app.hotItems
                            rowDelegate: Component { HotRow { fontScale: window.fontScale; darkMode: window.dark; foreground: window.foreground; secondary: window.secondary; onOpened: (title, url, rank) => window.openHot(title, url, rank) } }
                            onOpened: window.showPage("weibo")
                        }
                        NewsCard {
                            id: clsCard
                            fontScale: window.fontScale
                            title: "财联社头条"
                            timestamp: app.headlineTime
                            x: parent.wide ? hotCard.width + 12 : 0
                            y: parent.wide ? 0 : hotCard.height + 12
                            width: parent.wide ? (parent.width - 12) / 2 : parent.width
                            surface: window.cardColor; foreground: window.foreground; secondary: window.secondary
                            rows: app.headlines
                            rowDelegate: Component { HeadlineRow { fontScale: window.fontScale; darkMode: window.dark; foreground: window.foreground; secondary: window.secondary; onOpened: (url, title) => window.openHeadline(url, title) } }
                            onOpened: window.showPage("cls")
                        }
                    }
                }
            }

            Item {
                id: compactStage
                z: 1
                width: parent.width
                height: window.compactCardHeight
                enabled: window.compactActive
                visible: window.page === "home" && (window.compactActive || compactStageFade.running)
                opacity: window.compactActive ? 1 : 0
                Behavior on opacity { NumberAnimation { id: compactStageFade; duration: 160 } }
                NewsCard {
                    id: compactCard
                    property real switchOpacity: 1
                    property real switchOffset: 0
                    x: switchOffset
                    width: parent.width
                    height: parent.height
                    opacity: switchOpacity
                    fontScale: window.fontScale
                    title: window.compactCardKind === "stocks" ? "自选行情"
                        : window.compactCardKind === "weibo" ? "微博热搜" : "财联社头条"
                    origin: ""
                    timestamp: window.compactCardKind === "stocks" ? window.marketHeaderTime(app.marketTime)
                        : window.compactCardKind === "weibo" ? app.hotTime : app.headlineTime
                    surface: window.cardColor; foreground: window.foreground; secondary: window.secondary
                    rows: window.compactCardKind === "stocks" ? app.stocks.filter(item => item.followed)
                        : window.compactCardKind === "weibo" ? app.hotItems : app.headlines
                    columnHeader: window.compactCardKind === "stocks" ? stocksCard.columnHeader : null
                    rowDelegate: window.compactCardKind === "stocks" ? stocksCard.rowDelegate
                        : window.compactCardKind === "weibo" ? hotCard.rowDelegate : clsCard.rowDelegate
                    pageDotsEnabled: true
                    showPageDots: shellHover.hovered && window.compactActive
                    headerDragEnabled: true
                    pageDotLabels: ["自选行情", "微博热搜", "财联社头条"]
                    selectedDot: window.compactCardKind === "stocks" ? 0
                        : window.compactCardKind === "weibo" ? 1 : 2
                    onBodyHoveredChanged: if (bodyHovered && !headerPressed && window.compactActive && window.minimalModeEnabled && window.page === "home") window.expandOnHover = true
                    onHeaderPressedChanged: if (!headerPressed && bodyHovered && window.compactActive && window.minimalModeEnabled && window.page === "home") window.expandOnHover = true
                    onDotSelected: index => {
                        const kind = ["stocks", "weibo", "cls"][index]
                        if (kind === window.compactCardKind || cardSwitchAnimation.running) return
                        window.nextCompactCardKind = kind
                        window.cardSwitchDirection = index > compactCard.selectedDot ? 1 : -1
                        cardSwitchAnimation.start()
                    }
                    onDragRequested: window.startSystemMove()
                    onOpened: window.showPage(window.compactCardKind)
                }
            }
            SequentialAnimation {
                id: cardSwitchAnimation
                ParallelAnimation {
                    NumberAnimation { target: compactCard; property: "switchOpacity"; from: 1; to: 0; duration: 90 }
                    NumberAnimation { target: compactCard; property: "switchOffset"; from: 0; to: -window.cardSwitchDirection * 12; duration: 90; easing.type: Easing.InCubic }
                }
                ScriptAction { script: app.setMinimalCard(window.nextCompactCardKind) }
                ParallelAnimation {
                    NumberAnimation { target: compactCard; property: "switchOpacity"; from: 0; to: 1; duration: 160; easing.type: Easing.OutCubic }
                    NumberAnimation { target: compactCard; property: "switchOffset"; from: window.cardSwitchDirection * 12; to: 0; duration: 160; easing.type: Easing.OutCubic }
                }
            }

            Loader {
                anchors.fill: parent
                active: window.page === "stocks"
                sourceComponent: Component { Item {
                anchors.fill: parent
                DetailHeader { id: stocksHeader; fontScale: window.fontScale; width: parent.width; title: "自选行情"; foreground: window.foreground; secondary: window.secondary; onBack: window.showPage("home") }
                Item {
                    id: stockColumns
                    anchors.top: stocksHeader.bottom
                    width: parent.width; height: Math.round(24 * window.fontScale)
                    Text { text: "关注"; color: window.secondary; font.pixelSize: window.fs(11); anchors.left: parent.left; anchors.leftMargin: 4; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "名称 / 代码"; color: window.secondary; font.pixelSize: window.fs(11); anchors.left: parent.left; anchors.leftMargin: Math.round(36 * window.fontScale); anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "最新价 / 涨跌幅"; color: window.secondary; font.pixelSize: window.fs(11); width: Math.round(100 * window.fontScale); horizontalAlignment: Text.AlignRight; anchors.right: stockAmountHeader.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter }
                    Text { id: stockAmountHeader; text: "成交额"; color: window.secondary; font.pixelSize: window.fs(11); width: Math.round(78 * window.fontScale); horizontalAlignment: Text.AlignRight; anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter }
                }
                ListView {
                    id: stockList
                    //拖动不滚动页面，只允许滚轮与滚动条
                    interactive: false
                    WheelScroller { view: stockList }
                    ScrollBarIndicator { host: shell; view: stockList; offsetY: content.y; hovered: shellHover.hovered; ink: window.dark ? "#a0a0a0" : "#7e97a4" }
                    anchors.top: stockColumns.bottom
                    anchors.bottom: stockFooter.top
                    width: parent.width
                    model: app.stocks
                    reuseItems: true
                    clip: true
                    spacing: 0
                    delegate: StockRow {
                        fontScale: window.fontScale
                        required property var modelData
                        width: ListView.view.width
                        row: modelData; showFollow: true; showTurnover: true
                        detailed: true; darkMode: window.dark
                        foreground: window.foreground; secondary: window.secondary; divider: window.divider
                        onOpened: symbol => window.openQuote(symbol)
                        onFollowClicked: symbol => app.toggleFollow(symbol)
                    }
                }
                Item {
                    id: stockFooter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: -10
                    width: parent.width; height: Math.round(36 * window.fontScale)
                    Rectangle { width: parent.width; height: 1; color: window.divider }
                    Row {
                        visible: window.controlsVisible
                        anchors.left: parent.left; anchors.leftMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        Repeater {
                            model: [ {icon: "\ue710", hint: "添加股票", action: "add"},
                                     {icon: "\ue774", hint: "设置行情站点", action: "site"} ]
                            delegate: Rectangle {
                                required property var modelData
                                width: 28; height: 28; radius: 7
                                color: footerArea.containsMouse ? (window.dark ? "#2a2a2a" : "#dceaf4") : "transparent"
                                Text { anchors.centerIn: parent; text: modelData.icon; color: footerArea.containsMouse ? window.accent : window.secondary; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(18) }
                                MouseArea {
                                    id: footerArea; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        window.stockPopupError = ""
                                        if (modelData.action === "site") siteAddress.text = app.front
                                        window.stockPopup = window.stockPopup === modelData.action ? "" : modelData.action
                                    }
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: modelData.hint
                                }
                            }
                        }
                    }
                    Text {
                        text: app.marketStatus + (app.marketTime.length > 0 ? " · " + app.marketTime : "")
                        color: window.secondary; font.pixelSize: window.fs(11)
                        anchors.right: parent.right; anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft
                        width: Math.max(80, parent.width - 100)
                    }
                }
            } }
            }

            Loader {
                anchors.fill: parent
                active: window.page === "weibo" || window.page === "cls"
                visible: !window.weiboOpening
                sourceComponent: Component { Item {
                anchors.fill: parent
                DetailHeader {
                    id: boardHeader
                    fontScale: window.fontScale; width: parent.width
                    title: window.page === "weibo" ? "微博热搜" : "财联社头条"
                    foreground: window.foreground; secondary: window.secondary
                    rightActionIcon: "\ue72c"
                    rightActionHint: "刷新数据"
                    onBack: window.showPage("home")
                    onRightAction: if (window.page === "weibo") app.refreshHot(); else app.refreshHeadlines()
                }
                ListView {
                    id: boardList
                    //拖动不滚动页面，只允许滚轮与滚动条
                    interactive: false
                    WheelScroller { view: boardList }
                    ScrollBarIndicator { host: shell; view: boardList; offsetY: content.y; hovered: shellHover.hovered; ink: window.dark ? "#a0a0a0" : "#7e97a4" }
                    anchors.top: boardHeader.bottom; anchors.bottom: boardFooter.top; width: parent.width
                    model: window.page === "weibo" ? app.hotItems : app.headlines
                    reuseItems: true; clip: true
                    delegate: Loader {
                        id: boardRowLoader
                        required property var modelData
                        width: ListView.view.width
                        sourceComponent: window.page === "weibo" ? hotDelegate : clsDelegate
                        Binding {
                            target: boardRowLoader.item
                            property: "row"
                            value: boardRowLoader.modelData
                            when: boardRowLoader.item !== null
                        }
                    }
                }
                Item {
                    id: boardFooter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: -10
                    width: parent.width; height: Math.round(34 * window.fontScale)
                    Rectangle { width: parent.width; height: 1; color: window.divider }
                    Rectangle {
                        visible: window.controlsVisible
                        x: 1; y: 4; width: 28; height: 28; radius: 8
                        color: frequencyArea.containsMouse ? (window.dark ? "#2a2a2a" : "#dceaf4") : "transparent"
                        Text {
                            anchors.centerIn: parent; text: "\ue713"
                            color: frequencyArea.containsMouse ? window.accent : window.secondary
                            font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(18)
                        }
                        MouseArea {
                            id: frequencyArea; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.openFrequency()
                            ToolTip.visible: containsMouse
                            ToolTip.text: "设置刷新频率"
                        }
                    }
                    ToolButton {
                        id: weiboLoginButton
                        visible: window.controlsVisible && window.page === "weibo"
                        x: 35; y: 4; width: 28; height: 28
                        icon.source: "qrc:/resources/weibo.svg"
                        padding: 0
                        icon.width: window.fs(18); icon.height: window.fs(18)
                        icon.color: hovered ? window.accent : window.secondary
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: app.weiboLoggedIn ? "管理微博登录" : "登录新浪微博"
                        Accessible.description: app.weiboLoginStatus
                        onClicked: window.openWeiboAccount()
                        ToolTip.visible: !window.weiboAccountOpen && (hovered || activeFocus)
                        ToolTip.text: app.weiboLoginStatus + (app.weiboLoggedIn ? " · 点击管理" : " · 点击登录")
                        background: Rectangle {
                            radius: 8
                            color: weiboLoginButton.hovered ? (window.dark ? "#2a2a2a" : "#dceaf4") : "transparent"
                            border.width: weiboLoginButton.activeFocus ? 1 : 0
                            border.color: window.accent
                        }
                    }
                    Text {
                        text: window.page === "weibo"
                            ? [app.weiboLoggedIn ? "已登录" : "匿名", app.hotOrigin, app.hotStatus, app.hotTime].filter(value => value).join(" · ")
                            : [app.headlineStatus, app.headlineTime].filter(value => value).join(" · ")
                        color: window.secondary; font.pixelSize: window.fs(11)
                        anchors.right: parent.right; anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight; elide: Text.ElideLeft
                        width: Math.max(80, parent.width - 100)
                    }
                }
            } }
            }
            Component { id: hotDelegate; HotRow { fontScale: window.fontScale; detailed: true; darkMode: window.dark; divider: window.divider; foreground: window.foreground; secondary: window.secondary; onOpened: (title, url, rank) => window.openHot(title, url, rank) } }
            Component { id: clsDelegate; HeadlineRow { fontScale: window.fontScale; detailed: true; darkMode: window.dark; divider: window.divider; foreground: window.foreground; secondary: window.secondary; onOpened: (url, title) => window.openHeadline(url, title) } }

            Loader {
                anchors.fill: parent
                active: window.page === "quote"
                sourceComponent: Component { Item {
                id: quotePage
                anchors.fill: parent
                property var quote: app.stocks.find(item => item.symbol === window.selectedSymbol) || ({})
                property color tone: !quote.change ? window.foreground
                    : quote.changeValue < 0 ? (window.dark ? "#80dcb9" : "#228268")
                    : (window.dark ? "#ff9da5" : "#b54450")
                property int bookRowHeight: Math.max(Math.round(14 * fontScale), Math.min(Math.round(23 * fontScale), Math.floor((window.height - 268) / 10)))
                property real maxBookVolume: bookMaxVolume()
                function bookMaxVolume() {
                    let maximum = 1
                    const sides = [quote.asks || [], quote.bids || []]
                    for (const side of sides)
                        for (const level of side)
                            maximum = Math.max(maximum, Number(level.volumeValue || 0))
                    return maximum
                }
                DetailHeader {
                    id: quoteHeader
                    fontScale: window.fontScale
                    width: parent.width
                    title: quotePage.quote.name || window.selectedSymbol
                    rightLabel: window.selectedSymbol
                    showDivider: false
                    foreground: window.foreground; secondary: window.secondary
                    onBack: window.goBack()
                }
                Flickable {
                    id: quoteView
                    //拖动不滚动页面，只允许滚轮与滚动条
                    interactive: false
                    WheelScroller { view: quoteView }
                    ScrollBarIndicator { host: shell; view: quoteView; offsetY: content.y; hovered: shellHover.hovered; ink: window.dark ? "#a0a0a0" : "#7e97a4" }
                    anchors.top: quoteHeader.bottom
                    anchors.bottom: quoteFooter.top
                    width: parent.width
                    contentWidth: width; contentHeight: quoteColumn.implicitHeight
                    clip: true
                    Column {
                        id: quoteColumn; width: parent.width; spacing: 0
                        Item {
                            width: parent.width; height: Math.round(55 * window.fontScale)
                            Text {
                                text: quotePage.quote.price || "—"
                                x: 4; y: 0
                                color: quotePage.tone
                                font.family: "Segoe UI"; font.pixelSize: window.fs(29); font.weight: Font.DemiBold
                            }
                            Text {
                                text: quotePage.quote.change || ""
                                x: 5; y: Math.round(34 * window.fontScale)
                                color: quotePage.tone
                                font.family: "Segoe UI"; font.pixelSize: window.fs(13); font.weight: Font.DemiBold
                            }
                            Rectangle {
                                visible: window.controlsVisible
                                width: 28; height: 28; radius: 8
                                anchors.right: parent.right; anchors.rightMargin: 4
                                y: 3
                                color: removeArea.containsMouse ? (window.dark ? "#303030" : "#dceaf4") : "transparent"
                                Rectangle {
                                    width: 15; height: 15; radius: 8
                                    anchors.centerIn: parent
                                    color: "transparent"
                                    border.color: window.dark ? "#ff6b78" : "#c52b3b"
                                    border.width: 1.5
                                    Rectangle { anchors.centerIn: parent; width: 7; height: 2; radius: 1; color: parent.border.color }
                                }
                                MouseArea {
                                    id: removeArea; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: { app.removeStock(window.selectedSymbol); window.showPage("stocks") }
                                    ToolTip.visible: containsMouse
                                    ToolTip.text: "移出自选"
                                }
                            }
                            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.divider }
                        }
                        Item {
                            width: parent.width; height: Math.round(78 * window.fontScale)
                            Repeater {
                                model: [ ["开盘", quotePage.quote.open, "最高", quotePage.quote.high],
                                         ["收盘", quotePage.quote.close, "最低", quotePage.quote.low],
                                         ["涨停", quotePage.quote.upper, "成交量", quotePage.quote.volume],
                                         ["跌停", quotePage.quote.lower, "成交额", quotePage.quote.turnover] ]
                                delegate: Item {
                                    required property var modelData
                                    required property int index
                                    y: index * Math.round(19 * window.fontScale)
                                    width: parent.width; height: Math.round(19 * window.fontScale)
                                    Text { text: modelData[0]; x: 4; color: window.secondary; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: modelData[1] || "—"; x: parent.width / 2 - Math.round(76 * window.fontScale); width: Math.round(65 * window.fontScale); horizontalAlignment: Text.AlignRight; color: window.foreground; font.family: "Segoe UI"; font.pixelSize: window.fs(12); font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: modelData[2]; x: parent.width / 2 + 5; color: window.secondary; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: modelData[3] || "—"; width: Math.round(77 * window.fontScale); anchors.right: parent.right; anchors.rightMargin: 4; horizontalAlignment: Text.AlignRight; color: window.foreground; font.family: "Segoe UI"; font.pixelSize: window.fs(12); font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                        }
                        Rectangle { width: parent.width; height: 1; color: window.divider }
                        Item {
                            width: parent.width; height: quotePage.bookRowHeight * 10
                            Repeater {
                                model: 10
                                delegate: Item {
                                    required property int index
                                    property bool sell: index < 5
                                    property int levelIndex: sell ? 4 - index : index - 5
                                    property var entry: sell ? (quotePage.quote.asks || [])[levelIndex]
                                                             : (quotePage.quote.bids || [])[levelIndex]
                                    property real levelVolume: entry ? Number(entry.volumeValue || 0) : 0
                                    property color ink: sell
                                        ? (window.dark ? "#ff9da5" : "#b54450")
                                        : (window.dark ? "#80dcb9" : "#228268")
                                    y: index * quotePage.bookRowHeight
                                    width: parent.width; height: quotePage.bookRowHeight
                                    Rectangle {
                                        anchors.fill: parent; radius: 6
                                        color: levelHover.containsMouse ? (window.dark ? "#2a2a2a" : "#dceaf4") : "transparent"
                                    }
                                    MouseArea { id: levelHover; anchors.fill: parent; acceptedButtons: Qt.NoButton; hoverEnabled: true }
                                    Text { text: (sell ? "卖" : "买") + (levelIndex + 1); x: 4; width: Math.round(38 * window.fontScale); color: parent.ink; font.pixelSize: window.fs(11); font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: entry ? (entry.price || "—") : "—"; x: Math.round(46 * window.fontScale); width: Math.round(65 * window.fontScale); color: window.foreground; font.family: "Segoe UI"; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                    Rectangle {
                                        id: volumeTrack
                                        x: Math.min(Math.round(132 * window.fontScale), parent.width * 0.34)
                                        width: Math.max(30, parent.width - x - 60)
                                        height: 8; radius: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        color: window.dark ? "#272727" : "#d3dee4"
                                        Rectangle {
                                            visible: levelVolume > 0
                                            width: Math.min(parent.width,
                                                Math.max(3, parent.width * levelVolume / quotePage.maxBookVolume))
                                            height: parent.height; radius: 3
                                            color: sell ? (window.dark ? "#d47c87" : "#c85e6b")
                                                        : (window.dark ? "#57ba99" : "#2f9b78")
                                        }
                                    }
                                    Text { text: entry ? (entry.volume || "—") : "—"; width: Math.round(56 * window.fontScale); anchors.right: parent.right; anchors.rightMargin: 4; horizontalAlignment: Text.AlignRight; color: parent.ink; font.family: "Segoe UI"; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                            Rectangle { y: quotePage.bookRowHeight * 5 - 1; width: parent.width; height: 1; color: window.divider }
                        }
                    }
                }
                Item {
                    id: quoteFooter
                    anchors.bottom: parent.bottom; anchors.bottomMargin: -10
                    width: parent.width; height: Math.round(36 * window.fontScale)
                    Rectangle { width: parent.width; height: 1; color: window.divider }
                    Text {
                        text: app.marketStatus + (app.marketTime.length > 0 ? " · " + app.marketTime : "")
                        color: window.secondary; font.pixelSize: window.fs(11)
                        anchors.right: parent.right; anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            } }
            }

            Loader {
                id: xiaohongshuLoader
                anchors.fill: parent
                active: window.page === "xiaohongshu"
                visible: active
                sourceComponent: Component { XiaohongshuWebsite {
                    appController: app
                    fontScale: window.fontScale
                    darkTheme: window.dark
                    pageBackground: window.canvasColor
                    foreground: window.foreground
                    secondary: window.secondary
                    surface: window.cardColor
                    unobscured: !window.opacityOpen && !window.frequencyOpen && !window.weiboAccountOpen && window.stockPopup.length === 0
                    onBack: window.showPage("home")
                } }
            }

            Loader {
                id: websiteLoader
                anchors.fill: parent
                active: window.weiboOpening || (window.page === "weiboDetail" && !window.weiboUseFallback)
                visible: window.weiboOpening || window.page === "weiboDetail"
                sourceComponent: Component { WeiboWebsite {
                    appController: app
                    topic: window.selectedTitle
                    darkTheme: window.dark
                    pageBackground: window.canvasColor
                    fontScale: window.fontScale
                    foreground: window.foreground; secondary: window.secondary
                    surface: window.cardColor
                    openingFromSource: window.weiboOpening
                    unobscured: !window.opacityOpen && !window.frequencyOpen && !window.weiboAccountOpen && window.stockPopup.length === 0
                    id: website
                    onPageReadyChanged: if (pageReady && window.weiboOpening) window.showPage("weiboDetail")
                    onPageFailedChanged: if (pageFailed && window.weiboOpening) window.showPage("weiboDetail")
                    onLoginBlockedBeforeContentChanged: if (loginBlockedBeforeContent && window.weiboOpening) {
                        Qt.callLater(() => {
                            if (websiteLoader.item === website && window.weiboOpening && website.loginBlockedBeforeContent) {
                                window.weiboLoginNotice = true
                                window.weiboOpening = false
                            }
                        })
                    }
                    onBack: window.goBack()
                    onFallback: {
                        window.weiboUseFallback = true
                        app.openWeibo(window.selectedTitle, window.selectedUrl)
                    }
                } }
            }
            Rectangle {
                z: 1
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom; anchors.bottomMargin: 36
                width: weiboNoticeText.implicitWidth + 24
                height: window.fs(30)
                radius: 8
                color: window.cardColor
                border.color: window.divider
                visible: !window.compactActive && window.weiboLoginNotice && !app.weiboLoggedIn
                Text {
                    id: weiboNoticeText
                    anchors.centerIn: parent
                    text: "登录后加载更多"
                    color: window.foreground
                    font.pixelSize: window.fs(12)
                    Accessible.role: Accessible.AlertMessage
                    Accessible.name: text
                }
            }
            Loader {
                anchors.fill: parent
                active: window.page === "weiboDetail" && window.weiboUseFallback
                sourceComponent: Component { Item {
                anchors.fill: parent
                DetailHeader { id: weiboDetailHeader; fontScale: window.fontScale; width: parent.width; title: "微博热搜"; externalUrl: window.selectedUrl; foreground: window.foreground; secondary: window.secondary; onBack: window.goBack(); onExternal: app.openExternal(externalUrl) }
                ToolButton {
                    text: "返回网页"
                    anchors.right: parent.right; anchors.rightMargin: 32
                    y: (weiboDetailHeader.height - height) / 2
                    onClicked: { app.cancelMedia(); window.weiboUseFallback = false }
                }
                Text {
                    id: topicHeading
                    text: window.selectedTitle
                    color: window.foreground; font.pixelSize: window.fs(16); font.weight: Font.DemiBold
                    anchors.top: weiboDetailHeader.bottom; anchors.topMargin: 14
                    anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 4; anchors.rightMargin: 4
                    wrapMode: Text.Wrap
                }
                Text {
                    id: postStatus
                    text: window.escapePostText(app.detailStatus) + (!app.postsLoading && app.postsHasMore ? ' · <a href="newsx:more">点击重试</a>' : "")
                    textFormat: Text.RichText
                    linkColor: window.accent
                    onLinkActivated: app.loadMoreWeibo()
                    visible: text.length > 0 && app.posts.length === 0
                    color: window.secondary; font.pixelSize: window.fs(12)
                    anchors.top: topicHeading.bottom; anchors.topMargin: 16
                    anchors.left: parent.left; anchors.leftMargin: 4
                }
                Flickable {
                    id: postList
                    //拖动不滚动页面，只允许滚轮与滚动条
                    interactive: false
                    WheelScroller { view: postList }
                    function maybeLoadMore() {
                        if (window.page === "weiboDetail" && app.posts.length > 0 && app.postsHasMore
                                && !app.postsLoading && app.detailStatus.length === 0
                                && contentY + height >= contentHeight - 200)
                            app.loadMoreWeibo()
                    }
                    anchors.top: postStatus.visible ? postStatus.bottom : topicHeading.bottom
                    anchors.topMargin: postStatus.visible ? 10 : 14
                    anchors.bottom: parent.bottom; width: parent.width
                    contentWidth: width
                    contentHeight: postsColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ScrollBarIndicator { id: postScrollbar; host: shell; view: postList; offsetY: content.y; hovered: shellHover.hovered; ink: window.dark ? "#a0a0a0" : "#7e97a4"; onDraggingChanged: if (!dragging) postList.maybeLoadMore() }
                    property real previousContentY: 0
                    onContentYChanged: {
                        const movedDown = contentY > previousContentY
                        previousContentY = contentY
                        if (movedDown) maybeLoadMore()
                    }
                    Column {
                        id: postsColumn
                        width: postList.width
                        Repeater {
                            model: app.posts
                            delegate: Item {
                        id: postRow
                        required property var modelData
                        required property int index
                        property var row: modelData
                        property bool expanded: false
                        property bool commentsOpen: false
                        property var commentThread: app.commentThreads[String(row.id)] || ({})
                        property string preview: ""
                        width: postsColumn.width
                        height: postColumn.implicitHeight + 23
                        Column {
                            id: postColumn
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.leftMargin: 4; anchors.rightMargin: 4; anchors.topMargin: 5
                            spacing: 8
                            Row {
                                width: parent.width; height: Math.round(40 * window.fontScale); spacing: 9
                                CircularAvatar {
                                    width: Math.round(36 * window.fontScale); height: Math.round(36 * window.fontScale)
                                    imageUrl: app.imageSources[row.avatar] || ""
                                    fallback: row.title || "微"
                                    background: window.dark ? "#373737" : "#dce9ef"
                                    foreground: window.foreground
                                }
                                Column {
                                    width: parent.width - Math.round(40 * window.fontScale) - 9; spacing: 3
                                    Text { text: row.title || "微博用户"; color: window.foreground; font.pixelSize: window.fs(13); font.weight: Font.Bold; width: parent.width; elide: Text.ElideRight }
                                    Text { text: window.postTime(row.published_at); color: window.secondary; font.pixelSize: window.fs(11) }
                                }

                            }
                            Text {
                                text: window.richPostText(row.text, row.links, postRow.expanded)
                                textFormat: Text.RichText; linkColor: window.accent
                                onLinkActivated: link => {
                                    if (link === "newsx:expand") postRow.expanded = true
                                    else if (link === "newsx:collapse") postRow.expanded = false
                                    else app.openExternal(link)
                                }
                                color: window.foreground; font.pixelSize: window.fs(13); lineHeight: 1.35
                                wrapMode: Text.Wrap; width: parent.width
                            }
                            Grid {
                                width: parent.width
                                columns: 3; spacing: 5
                                Repeater {
                                    model: row.images || []
                                    delegate: Image {
                                        required property string modelData
                                        width: (postColumn.width - 10) / 3; height: width * 0.72
                                        source: app.imageSources[modelData] || ""; sourceSize.width: width * window.imageScale; sourceSize.height: height * window.imageScale
                                        fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false
                                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: preview = preview === parent.modelData ? "" : parent.modelData }
                                    }
                                }
                            }
                            Image {
                                id: previewImage
                                visible: postRow.preview.length > 0
                                source: app.imageSources[postRow.preview] || ""
                                sourceSize.width: postColumn.width * window.imageScale
                                width: parent.width
                                height: visible && status === Image.Ready ? width * implicitHeight / Math.max(1, implicitWidth) : 0
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                readonly property var pictures: postRow.row.images || []
                                readonly property int pictureIndex: pictures.indexOf(postRow.preview)
                                readonly property real visibleTop: Math.max(0, postList.contentY - postRow.y - postColumn.y - y)
                                readonly property real visibleBottom: Math.min(height, postList.contentY + postList.height - postRow.y - postColumn.y - y)
                                MouseArea { anchors.fill: parent; onClicked: postRow.preview = "" }
                                Repeater {
                                    model: 2
                                    delegate: Item {
                                        id: imageNavigation
                                        required property int index
                                        readonly property int direction: index === 0 ? -1 : 1
                                        readonly property int targetIndex: previewImage.pictureIndex + direction
                                        width: Math.min(64, previewImage.width / 5)
                                        height: previewImage.height
                                        x: index === 0 ? 0 : previewImage.width - width
                                        visible: previewImage.pictureIndex >= 0 && targetIndex >= 0 && targetIndex < previewImage.pictures.length
                                        MouseArea {
                                            id: navigationArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: postRow.preview = previewImage.pictures[imageNavigation.targetIndex]
                                        }
                                        Item {
                                            width: parent.width
                                            height: Math.min(48, parent.height)
                                            y: Math.max(0, Math.min(parent.height - height, (previewImage.visibleTop + previewImage.visibleBottom - height) / 2))
                                            visible: navigationArea.containsMouse
                                            // Transparent controls; a shadow keeps the chevrons legible on photographs.
                                            Text {
                                                anchors.centerIn: parent
                                                anchors.horizontalCenterOffset: 1
                                                anchors.verticalCenterOffset: 1
                                                text: imageNavigation.index === 0 ? "❮" : "❯"
                                                color: "#99000000"
                                                font.pixelSize: window.fs(28)
                                            }
                                            Text {
                                                anchors.centerIn: parent
                                                text: imageNavigation.index === 0 ? "❮" : "❯"
                                                color: "white"
                                                font.pixelSize: window.fs(28)
                                            }
                                        }
                                    }
                                }
                            }
                            Item {
                                visible: (row.video_url || "").length > 0
                                width: parent.width; height: visible ? Math.max(160, width * 9 / 16) : 0
                                Image { anchors.fill: parent; source: app.imageSources[row.video_poster] || ""; sourceSize.width: width * window.imageScale; fillMode: Image.PreserveAspectCrop; cache: false; visible: window.playingPost !== index }
                                Rectangle { anchors.centerIn: parent; width: 42; height: 42; radius: 21; color: "#99000000"; visible: window.playingPost !== index
                                    Text { anchors.centerIn: parent; text: "▶"; color: "white"; font.pixelSize: window.fs(21) }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { window.playingPost = index; app.prepareVideo(row.video_url) } }
                                }
                                Loader {
                                    anchors.fill: parent
                                    active: window.playingPost === index && app.videoSource.length > 0
                                    source: "components/VideoPlayer.qml"
                                }
                                Text { anchors.centerIn: parent; visible: window.playingPost === index && app.videoSource.length === 0; text: app.videoStatus; color: "white"; font.pixelSize: window.fs(11) }
                            }
                            Row {
                                width: parent.width; height: Math.round(36 * window.fontScale)
                                Row {
                                    width: parent.width / 3; height: parent.height; spacing: 5
                                    Text { text: "\ue72d"; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(13); color: window.secondary; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: "转发 " + window.compactCount(row.reposts_count); color: window.secondary; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                }
                                Rectangle {
                                    width: parent.width / 3; height: parent.height; radius: 6
                                    color: commentArea.containsMouse ? (window.dark ? "#2a2a2a" : "#e5eff4") : "transparent"
                                    Row {
                                        anchors.centerIn: parent; spacing: 5
                                        Text { text: "\ue8f2"; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(13); color: postRow.commentsOpen ? window.accent : window.secondary }
                                        Text { text: "评论 " + window.compactCount(row.comments_count); color: postRow.commentsOpen ? window.accent : window.secondary; font.pixelSize: window.fs(11) }
                                    }
                                    MouseArea { id: commentArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { postRow.commentsOpen = !postRow.commentsOpen; if (postRow.commentsOpen) app.loadComments(String(row.id)) } }
                                }
                                Row {
                                    width: parent.width / 3; height: parent.height; spacing: 5
                                    layoutDirection: Qt.RightToLeft
                                    Text { text: "\ue8e1"; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(13); color: window.secondary; anchors.verticalCenter: parent.verticalCenter }
                                    Text { text: "赞 " + window.compactCount(row.attitudes_count); color: window.secondary; font.pixelSize: window.fs(11); anchors.verticalCenter: parent.verticalCenter }
                                }
                            }
                            Column {
                                visible: postRow.commentsOpen
                                width: parent.width; spacing: 8
                                Rectangle { width: parent.width; height: 1; color: window.divider }
                                Text { text: "评论"; color: window.foreground; font.pixelSize: window.fs(12); font.weight: Font.DemiBold }
                                Text { visible: postRow.commentThread.loading === true; text: "正在加载评论…"; color: window.secondary; font.pixelSize: window.fs(11) }
                                Text { visible: !!postRow.commentThread.error; text: postRow.commentThread.error || ""; color: window.secondary; font.pixelSize: window.fs(11) }
                                Repeater {
                                    model: postRow.commentThread.items || []
                                    delegate: Row {
                                        required property var modelData
                                        width: postColumn.width; height: Math.max(Math.round(28 * window.fontScale), commentBody.implicitHeight)
                                        spacing: 7
                                        CircularAvatar {
                                            width: Math.round(26 * window.fontScale); height: Math.round(26 * window.fontScale)
                                            imageUrl: app.imageSources[modelData.avatar] || ""
                                            fallback: modelData.author || "微"
                                            background: window.dark ? "#373737" : "#dce9ef"
                                            foreground: window.foreground
                                        }
                                        Column {
                                            id: commentBody; width: parent.width - Math.round(33 * window.fontScale); spacing: 3
                                            Text { text: modelData.author || "微博用户"; color: window.accent; font.pixelSize: window.fs(11); font.weight: Font.DemiBold }
                                            Text { text: modelData.text || ""; color: window.foreground; font.pixelSize: window.fs(11); wrapMode: Text.Wrap; width: parent.width }
                                            Text { text: window.postTime(modelData.published_at); color: window.secondary; font.pixelSize: window.fs(10) }
                                        }
                                    }
                                }
                                Text {
                                    visible: !postRow.commentThread.loading && !postRow.commentThread.error
                                        && (postRow.commentThread.items || []).length === 0
                                    text: "暂无可显示的评论"; color: window.secondary; font.pixelSize: window.fs(11)
                                }
                                Text {
                                    visible: postRow.commentThread.hasMore === true && !postRow.commentThread.loading
                                    text: "再展开10条评论  ↓"; color: window.linkInk; font.pixelSize: window.fs(11)
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: app.loadComments(String(row.id)) }
                                }

                            }
                        }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: window.divider }
                    }
                        }
                        Text {
                            visible: app.posts.length > 0 && (app.postsLoading || app.postsHasMore || app.detailStatus.length > 0)
                            text: app.postsLoading ? "正在加载更多微博…"
                                : window.escapePostText(app.detailStatus)
                                  + (app.postsHasMore
                                    ? '<a href="newsx:more">' + (app.detailStatus.length > 0 ? ' · 点击重试' : '加载更多微博') + '</a>'
                                    : ' · <a href="' + window.selectedUrl + '">官网查看更多 ↗</a>')
                            textFormat: Text.RichText
                            linkColor: window.accent
                            onLinkActivated: link => { if (link === "newsx:more") app.loadMoreWeibo(); else app.openExternal(link) }
                            color: window.secondary; font.pixelSize: window.fs(11)
                            width: parent.width; leftPadding: 4; topPadding: 10; bottomPadding: 12
                        }
                    }
                }

            } }
            }

            Loader {
                anchors.fill: parent
                active: window.page === "article"
                sourceComponent: Component { Item {
                anchors.fill: parent
                DetailHeader { id: articleHeader; fontScale: window.fontScale; width: parent.width; title: "财联社头条"; externalUrl: window.selectedUrl; foreground: window.foreground; secondary: window.secondary; onBack: window.goBack(); onExternal: app.openExternal(externalUrl) }
                Flickable {
                    id: articleView
                    //拖动不滚动页面，只允许滚轮与滚动条
                    interactive: false
                    WheelScroller { view: articleView }
                    ScrollBarIndicator { host: shell; view: articleView; offsetY: content.y; hovered: shellHover.hovered; ink: window.dark ? "#a0a0a0" : "#7e97a4" }
                    anchors.top: articleHeader.bottom; anchors.bottom: parent.bottom; width: parent.width
                    contentHeight: articleColumn.implicitHeight; clip: true
                    Column {
                        id: articleColumn; width: parent.width; spacing: 12
                        Text {
                            text: window.selectedTitle.length > 0 ? window.selectedTitle : app.articleTitle
                            color: window.foreground; font.pixelSize: window.fs(18); font.weight: Font.Bold
                            lineHeight: 1.3; wrapMode: Text.Wrap; width: parent.width
                        }
                        Text {
                            text: [app.articleTime, app.articleSource].filter(value => value && value.length > 0).join(" · ")
                            visible: text.length > 0
                            color: window.secondary; font.pixelSize: window.fs(11)
                            width: parent.width; wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: app.articleLeadImages
                            delegate: Image {
                                required property string modelData
                                property string localSource: app.imageSources[modelData] || ""
                                source: localSource
                                visible: localSource.length > 0
                                width: articleColumn.width
                                sourceSize.width: width * window.imageScale
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true; cache: false
                                height: visible ? Math.min(460, implicitHeight * width / Math.max(1, implicitWidth)) : 0
                            }
                        }
                        Rectangle {
                            visible: app.articleBrief.length > 0
                            width: parent.width; height: briefLabel.implicitHeight + 18
                            radius: 10
                            color: window.dark ? "#111111" : "#e8f1f7"
                            Rectangle {
                                width: 3; height: parent.height - 18; radius: 1.5
                                x: 10; anchors.verticalCenter: parent.verticalCenter
                                color: window.accent
                            }
                            Text {
                                id: briefLabel
                                text: app.articleBrief
                                x: 22; y: 9; width: parent.width - 34
                                color: window.secondary; font.pixelSize: window.fs(12); lineHeight: 1.45
                                wrapMode: Text.Wrap
                            }
                        }
                        Repeater {
                            model: app.articleBlocks
                            delegate: Item {
                                required property var modelData
                                width: articleColumn.width
                                height: modelData.type === "image" ? articleImage.height : paragraph.implicitHeight
                                Text {
                                    id: paragraph
                                    visible: modelData.type === "text"
                                    text: visible ? modelData.text : ""
                                    textFormat: Text.RichText
                                    color: window.foreground; font.pixelSize: window.fs(13); lineHeight: 1.55
                                    wrapMode: Text.Wrap; width: parent.width
                                }
                                Image {
                                    id: articleImage
                                    property string localSource: modelData.type === "image" ? (app.imageSources[modelData.url] || "") : ""
                                    source: localSource
                                    visible: localSource.length > 0
                                    width: parent.width
                                    sourceSize.width: width * window.imageScale
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true; cache: false
                                    height: visible ? Math.min(460, implicitHeight * width / Math.max(1, implicitWidth)) : 0
                                }
                            }
                        }

                        Text {
                            text: app.detailStatus
                            visible: text.length > 0 && app.articleBlocks.length === 0
                            color: window.secondary; font.pixelSize: window.fs(11)
                        }
                    }
                }
            } }
            }
        }

        Repeater {
            model: [ {edge: Qt.LeftEdge, x: 0, y: 8, w: 5, h: shell.height - 16},
                     {edge: Qt.RightEdge, x: shell.width - 5, y: 8, w: 5, h: shell.height - 16},
                     {edge: Qt.TopEdge, x: 8, y: 0, w: shell.width - 16, h: 5},
                     {edge: Qt.BottomEdge, x: 8, y: shell.height - 5, w: shell.width - 16, h: 5},
                     {edge: Qt.LeftEdge | Qt.TopEdge, x: 0, y: 0, w: 8, h: 8},
                     {edge: Qt.RightEdge | Qt.TopEdge, x: shell.width - 8, y: 0, w: 8, h: 8},
                     {edge: Qt.LeftEdge | Qt.BottomEdge, x: 0, y: shell.height - 8, w: 8, h: 8},
                     {edge: Qt.RightEdge | Qt.BottomEdge, x: shell.width - 8, y: shell.height - 8, w: 8, h: 8} ]
            delegate: MouseArea {
                required property var modelData
                x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
                cursorShape: (modelData.edge === Qt.LeftEdge || modelData.edge === Qt.RightEdge) ? Qt.SizeHorCursor :
                             (modelData.edge === Qt.TopEdge || modelData.edge === Qt.BottomEdge) ? Qt.SizeVerCursor : Qt.SizeFDiagCursor
                enabled: !edgeHide.hidden && !edgeHide.animating
                onPressed: window.startSystemResize(modelData.edge)
            }
        }
        Rectangle {
            anchors.fill: parent
            z: 20
            visible: window.stockPopup.length > 0 || window.frequencyOpen || window.weiboAccountOpen
            color: window.dark ? "#75000000" : "#66000000"
            MouseArea { anchors.fill: parent; onClicked: { window.stockPopup = ""; window.stockPopupError = ""; window.frequencyOpen = false; window.frequencyError = ""; window.weiboAccountOpen = false } }
        }
        Rectangle {
            id: stockPopupPanel
            z: 21
            visible: window.stockPopup.length > 0
            x: 14; y: shell.height - height - 52
            width: shell.width - 28; height: Math.round(133 * window.fontScale); radius: 17
            color: window.dark ? "#171717" : "#f9fbfc"
            border.color: window.divider
            Text {
                text: window.stockPopup === "add" ? "添加自选" : "行情站点"
                color: window.foreground; font.pixelSize: window.fs(14); font.weight: Font.DemiBold
                x: 12; y: 12
            }
            Rectangle {
                width: Math.round(28 * window.fontScale); height: Math.round(28 * window.fontScale); radius: 8
                x: parent.width - width - 6; y: 6
                color: popupCloseArea.containsMouse ? (window.dark ? "#303030" : "#dceaf4") : "transparent"
                Text { anchors.centerIn: parent; text: "\ue8bb"; color: window.secondary; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(17) }
                MouseArea { id: popupCloseArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.stockPopup = "" }
            }
            Row {
                visible: window.stockPopup === "add"
                x: 12; y: 50; spacing: 2
                Repeater {
                    model: [ {label: "沪", code: "SH"}, {label: "深", code: "SZ"}, {label: "北", code: "BJ"} ]
                    delegate: Rectangle {
                        required property var modelData
                        width: Math.round(34 * window.fontScale); height: Math.round(34 * window.fontScale); radius: 9
                        color: window.selectedExchange === modelData.code
                            ? (window.dark ? "#303030" : "#ffffff")
                            : (exchangeArea.containsMouse ? (window.dark ? "#2a2a2a" : "#dceaf4") : "transparent")
                        Text { anchors.centerIn: parent; text: modelData.label; color: window.foreground; font.pixelSize: window.fs(13); font.weight: window.selectedExchange === modelData.code ? Font.DemiBold : Font.Normal }
                        MouseArea { id: exchangeArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.selectedExchange = modelData.code }
                    }
                }
            }
            TextField {
                id: stockCode
                visible: window.stockPopup === "add"
                x: 128; y: 50; width: parent.width - 140; height: Math.round(34 * window.fontScale)
                placeholderText: "六位股票代码"; maximumLength: 6
                inputMethodHints: Qt.ImhDigitsOnly
                color: window.foreground; placeholderTextColor: window.secondary
                font.pixelSize: window.fs(13)
                background: Rectangle { radius: 8; color: window.dark ? "#111111" : "#ffffff"; border.color: window.divider }
                onAccepted: window.saveStockPopup()
            }
            TextField {
                id: siteAddress
                visible: window.stockPopup === "site"
                x: 12; y: 50; width: parent.width - 24; height: Math.round(34 * window.fontScale)
                placeholderText: "tcp://主机:端口"
                color: window.foreground; placeholderTextColor: window.secondary
                font.pixelSize: window.fs(13)
                background: Rectangle { radius: 8; color: window.dark ? "#111111" : "#ffffff"; border.color: window.divider }
                onAccepted: window.saveStockPopup()
            }
            Text {
                text: window.stockPopupError.length > 0 ? window.stockPopupError
                    : window.stockPopup === "add" ? "选择交易所，输入 6 位代码" : "多个地址可用英文逗号分隔"
                color: window.stockPopupError.length > 0 ? (window.dark ? "#ff9da5" : "#b54450") : window.secondary
                font.pixelSize: window.fs(11)
                x: 12; y: parent.height - 34
                width: parent.width - 94; elide: Text.ElideRight
            }
            Rectangle {
                x: parent.width - width - 12; y: parent.height - height - 10
                width: Math.round(66 * window.fontScale); height: Math.round(27 * window.fontScale); radius: 10
                color: saveArea.containsMouse ? (window.dark ? "#b9e1f6" : "#2c70a9") : window.accent
                Text { anchors.centerIn: parent; text: window.stockPopup === "add" ? "添加" : "保存"; color: window.dark ? "#000000" : "#ffffff"; font.pixelSize: window.fs(12); font.weight: Font.DemiBold }
                MouseArea { id: saveArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.saveStockPopup() }
            }
        }
        Rectangle {
            id: weiboAccountPanel
            z: 21
            visible: window.weiboAccountOpen
            x: 14
            y: shell.height - height - 52
            width: shell.width - 28
            height: app.weiboLoggedIn ? window.fs(133) : Math.min(window.fs(520), shell.height - 120)
            radius: 17
            color: window.dark && !app.weiboLoggedIn ? "#141414" : window.cardColor
            border.color: window.divider
            MouseArea { anchors.fill: parent }
            Text {
                x: 12; y: 12
                text: app.weiboLoggedIn ? "微博账号" : "登录微博"
                color: window.foreground
                font.pixelSize: window.fs(14)
                font.weight: Font.DemiBold
            }
            ToolButton {
                visible: !app.weiboLoggedIn
                anchors.top: parent.top; anchors.topMargin: 6
                anchors.right: parent.right; anchors.rightMargin: 8
                width: window.fs(30); height: window.fs(30)
                text: "×"
                font.pixelSize: window.fs(17)
                palette.buttonText: window.secondary
                focusPolicy: Qt.StrongFocus
                Accessible.name: "关闭微博登录"
                onClicked: window.weiboAccountOpen = false
                background: Rectangle {
                    radius: 8
                    color: parent.hovered ? (window.dark ? "#171717" : "#e5eff4") : "transparent"
                    border.color: window.accent
                    border.width: parent.activeFocus ? 1 : 0
                }
            }
            Text {
                visible: app.weiboLoggedIn
                x: 12; y: window.fs(47)
                width: parent.width - 24
                text: app.weiboUserName || "微博账号"
                elide: Text.ElideRight
                color: window.foreground
                font.pixelSize: window.fs(14)
            }
            Rectangle {
                visible: app.weiboLoggedIn
                width: window.fs(66); height: window.fs(30)
                anchors.right: parent.right; anchors.rightMargin: 12
                anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                radius: 8
                color: logoutArea.containsMouse ? (window.dark ? "#343434" : "#b7d2e5") : (window.dark ? "#262626" : "#dceaf4")
                Text { anchors.centerIn: parent; text: "登出"; color: window.foreground; font.pixelSize: window.fs(12) }
                MouseArea {
                    id: logoutArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { window.weiboAccountOpen = false; app.logoutWeibo() }
                }
                Accessible.role: Accessible.Button
                Accessible.name: "登出微博"
                Accessible.onPressAction: { window.weiboAccountOpen = false; app.logoutWeibo() }
            }
            Loader {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: window.fs(40)
                active: window.weiboAccountOpen && !app.weiboLoggedIn
                sourceComponent: Component { Item {
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 20
                        text: accountBrowser.errorString || (accountBrowser.loading ? "正在加载登录页…" : "正在确认登录状态…")
                        color: window.secondary
                        font.pixelSize: window.fs(12)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    WeiboWebView {
                        id: accountBrowser
                        anchors.fill: parent
                        url: "https://passport.weibo.com/sso/signin?entry=miniblog&source=miniblog&url=https%3A%2F%2Fm.weibo.cn%2F"
                        session: app.weiboSession
                        loginOnly: true
                        darkTheme: window.dark
                        backgroundColor: weiboAccountPanel.color
                        onCloseRequested: window.weiboAccountOpen = false
                    }
                } }
            }
        }
        Rectangle {
            id: frequencyPopupPanel
            z: 21
            visible: window.frequencyOpen
            x: 14; y: shell.height - height - 52
            width: shell.width - 28; height: Math.round(133 * window.fontScale); radius: 17
            color: window.dark ? "#171717" : "#f9fbfc"
            border.color: window.divider
            Text {
                text: window.page === "weibo" ? "微博刷新频率" : "财联社刷新频率"
                color: window.foreground; font.pixelSize: window.fs(14); font.weight: Font.DemiBold
                x: 12; y: 12
            }
            Rectangle {
                width: Math.round(28 * window.fontScale); height: Math.round(28 * window.fontScale); radius: 8
                x: parent.width - width - 6; y: 6
                color: frequencyCloseArea.containsMouse ? (window.dark ? "#303030" : "#dceaf4") : "transparent"
                Text { anchors.centerIn: parent; text: "\ue8bb"; color: window.secondary; font.family: "Segoe MDL2 Assets"; font.pixelSize: window.fs(17) }
                MouseArea { id: frequencyCloseArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.frequencyOpen = false }
            }
            TextField {
                id: frequencyMinutesInput
                x: 12; y: 50; width: parent.width - 76; height: Math.round(34 * window.fontScale)
                placeholderText: "1–1440"; maximumLength: 4
                inputMethodHints: Qt.ImhDigitsOnly
                color: window.foreground; placeholderTextColor: window.secondary
                font.pixelSize: window.fs(13)
                background: Rectangle { radius: 8; color: window.dark ? "#111111" : "#ffffff"; border.color: window.divider }
                onAccepted: window.saveFrequency()
            }
            Text {
                x: parent.width - 54; y: 50; height: Math.round(34 * window.fontScale)
                text: "分钟"; color: window.secondary
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: window.fs(12)
            }
            Text {
                text: window.frequencyError.length > 0 ? window.frequencyError : "1–1440 分钟，保存后重新计时"
                color: window.frequencyError.length > 0 ? (window.dark ? "#ff9da5" : "#b54450") : window.secondary
                font.pixelSize: window.fs(11)
                x: 12; y: parent.height - 34
                width: parent.width - 94; elide: Text.ElideRight
            }
            Rectangle {
                x: parent.width - width - 12; y: parent.height - height - 10
                width: Math.round(66 * window.fontScale); height: Math.round(27 * window.fontScale); radius: 10
                color: frequencySaveArea.containsMouse ? (window.dark ? "#b9e1f6" : "#2c70a9") : window.accent
                Text { anchors.centerIn: parent; text: "保存"; color: window.dark ? "#000000" : "#ffffff"; font.pixelSize: window.fs(12); font.weight: Font.DemiBold }
                MouseArea { id: frequencySaveArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: window.saveFrequency() }
            }
        }
    }
}
