import QtQuick
import QtMultimedia

Item {
    MediaPlayer {
        id: player
        source: app.videoSource
        videoOutput: output
        audioOutput: AudioOutput {}
        Component.onCompleted: play()
    }
    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }
    MouseArea {
        anchors.fill: parent
        onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
    }
}
