import QtQuick

Item {
    id: root
    property var row: ({})
    property bool showFollow: false
    property bool showTurnover: false
    property bool detailed: false
    property bool darkMode: true
    property color foreground: "#f2f2f2"
    property color secondary: "#bdbdbd"
    property color divider: secondary
    property color tone: row.change && row.change.length > 0
        ? (row.changeValue < 0 ? (darkMode ? "#80dcb9" : "#228268")
                               : (darkMode ? "#ff9da5" : "#b54450")) : foreground
    signal opened(string symbol)
    signal followClicked(string symbol)
    // Font multiplier: Main.qml passes window.fontScale in.
    property real fontScale: 1.15
    function fs(size) { return Math.round(size * fontScale) }
    width: parent ? parent.width : 300
    height: Math.round((detailed ? 49 : showTurnover ? 42 : 40) * fontScale)

    Rectangle {
        visible: root.showTurnover
        width: parent.width - 8; height: 1
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        color: root.divider; opacity: root.detailed ? 0.75 : 0.18
    }

    Rectangle {
        anchors.fill: parent
        radius: 9
        color: mouse.containsMouse ? Qt.rgba(0.55, 0.75, 0.9, 0.10) : "transparent"
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.opened(root.row.symbol || "")
    }
    Text {
        id: star
        visible: root.showFollow
        text: root.row.followed ? "★" : "☆"
        color: root.darkMode ? "#ffb45e" : "#e89122"
        font.pixelSize: fs(20)
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.followClicked(root.row.symbol || "") }
    }
    Column {
        anchors.left: root.showFollow ? star.right : parent.left
        anchors.leftMargin: root.showFollow ? 8 : 4
        anchors.right: priceColumn.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text { text: root.row.name || root.row.symbol || ""; color: root.foreground; font.pixelSize: fs(14); font.weight: Font.Bold; elide: Text.ElideRight; width: parent.width }
        Text { text: root.row.symbol || ""; color: root.secondary; font.pixelSize: fs(11) }
    }
    Column {
        id: priceColumn
        width: Math.round((root.showTurnover ? 100 : 104) * fontScale)
        anchors.right: root.showTurnover ? amountColumn.left : parent.right
        anchors.rightMargin: root.showTurnover ? 8 : 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1
        Text { text: root.row.price || "—"; color: root.tone; font.family: "Segoe UI"; font.pixelSize: fs(15); font.weight: Font.Bold; horizontalAlignment: Text.AlignRight; width: parent.width }
        Text { text: root.row.change || ""; color: root.tone; font.family: "Segoe UI"; font.pixelSize: fs(11); horizontalAlignment: Text.AlignRight; width: parent.width }
    }
    Text {
        id: amountColumn
        visible: root.showTurnover
        width: Math.round(78 * fontScale)
        anchors.right: parent.right; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: root.row.turnover || "—"
        color: root.tone
        font.family: "Segoe UI"; font.pixelSize: fs(13); font.weight: Font.Bold
        horizontalAlignment: Text.AlignRight
    }
}
