import QtQuick
import QtQuick.Controls

MenuItem {
    id: modeItem
    required property string modeValue
    property string currentMode: "normal"
    property bool pointerInMenu: false
    property real fontScale: 1.15
    property color foreground: "#f2f2f2"
    property color accent: "#8cc8ef"
    readonly property bool pointerHighlight: hovered || (highlighted && !modeItem.pointerInMenu)
    height: 42
    padding: 10
    spacing: 8
    hoverEnabled: true
    font.family: "Segoe UI"
    font.pixelSize: Math.round(12 * modeItem.fontScale)
    icon.source: "qrc:/resources/mode-" + modeValue + ".svg"
    icon.color: modeItem.foreground
    icon.width: 22; icon.height: 22
    palette.windowText: modeItem.foreground
    palette.text: modeItem.foreground
    palette.highlightedText: modeItem.foreground
    checkable: true
    checked: modeItem.currentMode === modeValue
    indicator: Text {
        width: 16
        height: 20
        x: modeItem.leftPadding
        y: (modeItem.height - height) / 2
        visible: modeItem.checked
        text: "\ue73e"
        color: modeItem.accent
        font.family: "Segoe MDL2 Assets"
        font.pixelSize: 15
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 8
        // Pointer feedback is immediate; keyboard highlight remains available.
        color: modeItem.down ? Qt.rgba(modeItem.accent.r, modeItem.accent.g, modeItem.accent.b, 0.24)
            : modeItem.pointerHighlight ? Qt.rgba(modeItem.accent.r, modeItem.accent.g, modeItem.accent.b, 0.14)
            : "transparent"
        border.width: modeItem.activeFocus && !modeItem.pointerInMenu ? 1 : 0
        border.color: modeItem.accent
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
