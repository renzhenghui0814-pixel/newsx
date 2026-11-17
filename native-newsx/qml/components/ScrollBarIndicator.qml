import QtQuick

// Shared scrollbar for every page: a thin rounded thumb pinned to the window edge.
// The thumb is computed straight from the view's contentY/contentHeight, so it sits
// exactly at the top when the view is at the top and exactly at the bottom when the
// view is fully scrolled. Qt's own ScrollBar normalises against an estimated
// contentHeight instead, which made the thumb drift while a long list measured itself.
Item {
    id: root
    property Item host
    property Item view
    property real offsetY: 0
    property bool hovered: false
    property color ink: "#7e97a4"
    readonly property bool dragging: dragHandler.active
    readonly property real scrollable: view ? Math.max(0, view.contentHeight - view.height) : 0
    readonly property real track: Math.max(0, height - thumb.height)
    property real dragStartY: 0
    parent: host
    x: host ? host.width - 10 : 0
    y: offsetY + (view ? view.y : 0)
    width: 7
    height: view ? view.height : 0
    visible: view !== null && view !== undefined && view.visible && scrollable > 0 && hovered

    Rectangle {
        id: thumb
        width: parent.width
        radius: 3
        color: root.ink
        height: root.view && root.view.contentHeight > 0
            ? Math.max(20, root.height * (root.view.height / root.view.contentHeight))
            : root.height
        y: root.scrollable > 0
            ? Math.max(0, Math.min(root.track, root.view.contentY / root.scrollable * root.track))
            : 0
    }

    WheelScroller { view: root.view }

    DragHandler {
        id: dragHandler
        target: null
        xAxis.enabled: false
        onActiveChanged: if (active) root.dragStartY = root.view ? root.view.contentY : 0
        onTranslationChanged: {
            if (!active || !root.view || root.scrollable <= 0) return
            const ratio = root.scrollable / Math.max(1, root.track)
            root.view.contentY = Math.max(0, Math.min(root.scrollable, root.dragStartY + translation.y * ratio))
        }
    }
}
