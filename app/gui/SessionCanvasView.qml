import QtQuick
import QtQuick.Controls
import DeskPort.Canvas 1.0

Rectangle {
    color: "#101216"
    SessionCanvas {
        id: sessionCanvas
        objectName: "sessionCanvas"
        anchors.fill: parent
        presentation: sessionManager.presentation
        textInputEnabled: sessionManager.canvas.textInput === true
        cursorHidden: sessionManager.canvas.cursorHidden === true
        captureRequested: sessionManager.canvas.grab === true
        onViewportChanged: function(state) { sessionManager.updateCanvas(state) }
        onInput: function(event) { sessionManager.canvasInput(event) }
    }
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: !sessionCanvas.frameReady || sessionManager.canvas.waiting === true
        BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: sessionManager.canvas.label || qsTranslate("DesktopSegue", "Connecting to desktop…")
            color: "#e6e9ee"
        }
        UiButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTranslate("StreamSegue", "Cancel connection")
            onClicked: sessionManager.disconnectSession(sessionManager.selectedId)
        }
    }
}
