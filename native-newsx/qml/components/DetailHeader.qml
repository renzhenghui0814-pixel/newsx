import QtQuick
import QtQuick.Controls

Item {
    id: root
    property string title: ""
    property string externalUrl: ""
    property string rightLabel: ""
    property string rightActionIcon: ""
    property string rightActionHint: ""
    property bool showDivider: true
    property color foreground: "#f2f2f2"
    property color secondary: "#bdbdbd"
    // Font multiplier: Main.qml passes window.fontScale in.
    property real fontScale: 1.15
    function fs(size) { return Math.round(size * fontScale) }
    signal back()
    signal external()
    signal rightAction()
    height: Math.round(36 * fontScale)
    Rectangle {
        id: backButton
        width: 30; height: 30; radius: 8
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        color: backArea.containsMouse ? Qt.rgba(0.55, 0.75, 0.9, 0.18) : "transparent"
        Text {
            anchors.centerIn: parent
            text: "\ue72b"
            color: backArea.containsMouse ? root.foreground : root.secondary
            font.family: "Segoe MDL2 Assets"; font.pixelSize: fs(18)
        }
        MouseArea {
            id: backArea
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.back()
            ToolTip.visible: containsMouse
            ToolTip.text: "返回卡片"
        }
    }
    Text {
        text: root.title
        color: root.foreground; font.pixelSize: fs(16); font.weight: Font.Bold
        elide: Text.ElideRight
        anchors.left: backButton.right; anchors.leftMargin: 8
        anchors.right: rightActionButton.visible ? rightActionButton.left
            : rightLabelText.visible ? rightLabelText.left : externalButton.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
    }
    Text {
        id: externalButton
        text: "↗"; visible: root.externalUrl.length > 0
        color: root.secondary; font.pixelSize: fs(20)
        anchors.right: parent.right; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        MouseArea {
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.external()
            ToolTip.visible: containsMouse
            ToolTip.text: "在官网打开"
        }
    }
    Text {
        id: rightLabelText
        text: root.rightLabel
        visible: text.length > 0
        color: root.secondary; font.family: "Segoe UI"; font.pixelSize: fs(11)
        anchors.right: parent.right; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
    }
    Rectangle {
        id: rightActionButton
        visible: root.rightActionIcon.length > 0
        activeFocusOnTab: visible
        Accessible.role: Accessible.Button
        Accessible.name: root.rightActionHint
        Accessible.onPressAction: root.rightAction()
        Keys.onReturnPressed: root.rightAction()
        Keys.onSpacePressed: root.rightAction()
        width: 30; height: 30; radius: 8
        anchors.right: externalButton.visible ? externalButton.left : parent.right
        anchors.rightMargin: externalButton.visible ? 8 : 0
        anchors.verticalCenter: parent.verticalCenter
        color: rightActionArea.containsMouse ? Qt.rgba(0.55, 0.75, 0.9, 0.18) : "transparent"
        Text {
            anchors.centerIn: parent
            text: root.rightActionIcon
            color: rightActionArea.containsMouse ? root.foreground : root.secondary
            font.family: "Segoe MDL2 Assets"; font.pixelSize: fs(18)
        }
        MouseArea {
            id: rightActionArea
            anchors.fill: parent; hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.rightAction()
            ToolTip.visible: containsMouse
            ToolTip.text: root.rightActionHint
        }
    }
    Rectangle {
        visible: root.showDivider
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 1
        color: root.secondary; opacity: 0.4
    }
}
