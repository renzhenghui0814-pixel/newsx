import QtQuick

Item {
    id: root
    property var row: ({})
    property color foreground: "#f2f2f2"
    property color secondary: "#bdbdbd"
    property bool darkMode: true
    property bool detailed: false
    property color divider: darkMode ? "#373737" : "#d8e2e8"
    signal opened(string url, string title)
    width: parent ? parent.width : 300
    height: Math.round((detailed ? 49 : 34) * fontScale)
    // Font multiplier: Main.qml passes window.fontScale in.
    property real fontScale: 1.15
    function fs(size) { return Math.round(size * fontScale) }
    function publishedTime(value) {
        const seconds = Number(value)
        return Number.isFinite(seconds) && seconds > 0
            ? Qt.formatTime(new Date(seconds * 1000), "HH:mm") : "--:--"
    }
    Rectangle { anchors.fill: parent; radius: 9; color: area.containsMouse ? Qt.rgba(0.55, 0.75, 0.9, 0.10) : "transparent" }
    Rectangle {
        visible: root.detailed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 1
        color: root.divider
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.opened(root.row.url || "", root.row.title || "") }
    Text {
        id: timeText
        text: root.publishedTime(root.row.published_at)
        color: root.darkMode ? "#8cc8ef" : "#397fbd"; font.family: "Segoe UI"; font.pixelSize: fs(root.detailed ? 12 : 11)
        width: root.detailed ? 52 : 44
        anchors.left: parent.left; anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        text: root.row.title || ""
        color: root.foreground; font.pixelSize: fs(13); elide: Text.ElideRight
        anchors.left: timeText.right
        anchors.right: parent.right; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }
}
