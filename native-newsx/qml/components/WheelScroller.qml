import QtQuick

// The pages disable Flickable dragging (interactive: false) so a click-drag never
// scrolls them. A non-interactive Flickable also drops wheel events, so this handler
// does the wheel scrolling itself: same feel as before, no dragging.
WheelHandler {
    id: root
    property Item view
    target: null
    blocking: true
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    enabled: view !== null && view !== undefined
    onWheel: event => {
        const step = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 120 * 86
        const maxY = Math.max(0, view.contentHeight - view.height)
        view.contentY = Math.max(0, Math.min(maxY, view.contentY - step))
        event.accepted = true
    }
}
