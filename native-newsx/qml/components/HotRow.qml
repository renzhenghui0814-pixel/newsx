import QtQuick

Item {
    id: root
    property var row: ({})
    property color foreground: "#f4f7f9"
    property color secondary: "#a5b9c4"
    property bool darkMode: true
    property bool detailed: false
    property color divider: darkMode ? "#4c616d" : "#d8e2e8"
    property int rank: Number(row.rank || 0)
    property bool featured: rank >= 1 && rank <= 3
    property color tone: rank === 1 ? (darkMode ? "#ff6b78" : "#c52b3b")
                         : featured ? (darkMode ? "#ffb987" : "#a64d2c") : secondary
    signal opened(string title, string url, int rank)
    width: parent ? parent.width : 300
    height: Math.round((detailed ? 49 : 34) * fontScale)

    // Font multiplier: Main.qml passes window.fontScale in.
    property real fontScale: 1.15
    function fs(size) { return Math.round(size * fontScale) }
    function hotValue(value) {
        const raw = String(value || "").trim()
        if (!raw) return ""
        if (!/^\d+(\.\d+)?$/.test(raw)) return raw
        const number = Number(raw)
        if (number >= 100000000) return (number / 100000000).toFixed(1) + "亿"
        if (number >= 10000) return (number / 10000).toFixed(1) + "万"
        return String(number)
    }

    Rectangle { anchors.fill: parent; radius: 9; color: area.containsMouse ? Qt.rgba(0.55, 0.75, 0.9, 0.10) : "transparent" }
    Rectangle {
        visible: root.detailed
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 1; color: root.divider
    }
    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.opened(root.row.title || "", root.row.url || "", root.rank) }
    Text {
        id: rankText
        text: root.rank > 0 ? (root.rank < 10 ? "0" : "") + root.rank : ""
        color: root.tone
        font.family: root.featured ? "Bahnschrift SemiBold" : "Segoe UI"; font.pixelSize: fs(root.featured ? 17 : 13)
        font.weight: root.featured ? Font.Bold : Font.Normal
        width: root.detailed ? 36 : 30
        anchors.left: parent.left; anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        text: root.row.title || ""
        color: root.foreground; font.pixelSize: fs(13); elide: Text.ElideRight
        anchors.left: rankText.right
        anchors.right: hotText.left; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        id: hotText
        text: root.hotValue(root.row.hot)
        color: root.tone
        font.family: root.featured ? "Bahnschrift SemiBold" : "Segoe UI"; font.pixelSize: fs(root.featured ? 13 : 12)
        font.weight: root.featured ? Font.Bold : Font.Normal
        width: 68; horizontalAlignment: Text.AlignRight
        anchors.right: tagBox.left; anchors.rightMargin: 7
        anchors.verticalCenter: parent.verticalCenter
    }
    Rectangle {
        id: tagBox
        width: 42; height: Math.round(19 * fontScale); radius: 5
        anchors.right: parent.right; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        visible: String(root.row.tag || "").trim().length > 0
        color: root.rank === 1 ? (root.darkMode ? "#59373f" : "#fbe5e8")
                : root.featured ? (root.darkMode ? "#59483f" : "#f7e3d7")
                : (root.darkMode ? "#3b5360" : "#e7edf1")
        Text {
            anchors.fill: parent
            text: String(root.row.tag || "").trim()
            color: root.tone; font.pixelSize: fs(11); font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
