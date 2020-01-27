import QtQuick 2.10
import QtQuick.Window 2.10
import QtQuick.Layouts 1.10
import QtQuick.Controls 2.0
import QtMultimedia 5.10
import v4l2source 1.0

Window {
    visible: true
    width: 640
    height: 480
    title: qsTr("qml zero copy rendering")
    color: "black"

    CameraSource {
        id: camera
        device: "/dev/video0"
        onFrameReady: videoOutput.update()
    }

    VideoOutput {
        id: videoOutput
        source: camera
        anchors.fill: parent
    }

    onClosing: camera.stop()
//    onVisibleChanged: visible ? camera.start() : camera.stop()
}
