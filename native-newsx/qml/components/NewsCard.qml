import QtQuick
import QtQuick.Controls

Rectangle {
    id: card
    property string title: ""
    property string timestamp: ""
    property string origin: ""
    property color surface: "#23323e"
    property color foreground: "#f2f6f8"
    property color secondary: "#9eb2be"
    property var rows: []
    property Component rowDelegate
    property Component columnHeader
    property bool pageDotsEnabled: false
    property bool showPageDots: false
    property bool headerDragEnabled: false
    property int selectedDot: 0
    property var pageDotLabels: []
    readonly property real headerZoneHeight: content.y + titleRow.height + content.spacing
    readonly property bool bodyHovered: cardHover.hovered && cardHover.point.position.y >= headerZoneHeight
    readonly property bool headerPressed: headerDragArea.pressed
    signal opened()
    signal dragRequested()
    signal dotSelected(int index)
    radius: 17
    color: surface
    border.color: Qt.rgba(1, 1, 1, 0.08)
    // Font multiplier: Main.qml passes window.fontScale in.
    property real fontScale: 1.15
    function fs(size) { return Math.round(size * fontScale) }
    implicitHeight: content.implicitHeight + 28
    HoverHandler { id: cardHover }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 4

        Item {
            id: titleRow
            width: parent.width
            height: Math.round(30 * fontScale)
            Text {
                id: titleText
                text: card.title
                color: card.foreground
                font.family: "Microsoft YaHei UI"
                font.pixelSize: fs(16)
                font.weight: Font.Bold
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: card.origin
                visible: text.length > 0
                color: card.secondary
                font.pixelSize: fs(11)
                anchors.left: titleText.right
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: card.timestamp
                color: card.secondary
                font.family: "Segoe UI"
                font.pixelSize: fs(11)
                anchors.right: chevron.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                id: chevron
                text: "›"
                color: card.secondary
                font.pixelSize: fs(19)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: card.opened() }
        }

        Rectangle { id: divider; width: parent.width; height: 1; color: card.secondary; opacity: 0.3 }
        Loader { width: content.width; sourceComponent: card.columnHeader; visible: card.columnHeader !== null }

        Repeater {
            model: Math.min(card.rows.length, 5)
            delegate: Loader {
                id: cardRowLoader
                required property int index
                width: content.width
                sourceComponent: card.rowDelegate
                Binding {
                    target: cardRowLoader.item
                    property: "row"
                    value: card.rows[cardRowLoader.index]
                    when: cardRowLoader.item !== null && cardRowLoader.index < card.rows.length
                }
            }
        }
        Text {
            visible: card.rows.length === 0
            text: "暂无内容"
            color: card.secondary
            font.pixelSize: fs(13)
            topPadding: 14
            bottomPadding: 12
        }
    }
    MouseArea {
        id: headerDragArea
        visible: card.headerDragEnabled
        z: 1
        width: parent.width
        height: card.headerZoneHeight
        cursorShape: Qt.SizeAllCursor
        property real pressX: 0
        property real pressY: 0
        property bool moving: false
        onPressed: mouse => {
            pressX = mouse.x
            pressY = mouse.y
            moving = false
        }
        onPositionChanged: mouse => {
            if (pressed && !moving && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) >= 4) {
                moving = true
                card.dragRequested()
            }
        }
        onClicked: {} // The whole header is a drag area; only the page dots have a click action.
    }
    Row {
        visible: card.pageDotsEnabled && card.showPageDots
        z: 2
        x: (card.width - width) / 2
        y: content.y + (titleRow.height - height) / 2
        spacing: 2
        Repeater {
            model: card.pageDotLabels.length
            delegate: Item {
                required property int index
                width: 24; height: 30
                Rectangle {
                    anchors.centerIn: parent
                    width: card.selectedDot === index ? 9 : 6
                    height: width; radius: width / 2
                    color: card.selectedDot === index ? card.foreground : card.secondary
                    Behavior on width { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.dotSelected(index)
                    ToolTip.visible: containsMouse
                    ToolTip.text: card.pageDotLabels[index]
                }
            }
        }
    }
}
