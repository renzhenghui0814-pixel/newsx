import QtQuick

Item {
    id: root
    property string imageUrl: ""
    property string fallback: "微"
    property color background: "#373737"
    property color foreground: "#f2f2f2"
    readonly property bool hasImage: imageUrl.length > 0
    readonly property bool imageReady: image.status === Image.Ready
    width: 36
    height: 36

    // Avatars are cropped to a circle before they reach QML (see MediaService), so
    // the image needs no mask layer: the letter placeholder simply stays visible
    // until the picture is ready and takes over.
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.background
        visible: !root.imageReady
        Text {
            anchors.centerIn: parent
            text: root.fallback.slice(0, 1)
            color: root.foreground
            font.pixelSize: root.width * 0.45
        }
    }

    Image {
        id: image
        anchors.fill: parent
        visible: root.hasImage
        source: root.imageUrl
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }
}
