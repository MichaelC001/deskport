import QtQuick
import QtQuick.Controls
import DeskPort.Canvas 1.0

Rectangle {
    id: canvasPage
    readonly property var question: sessionManager.canvas.question || ({})
    readonly property bool asking: question.id !== undefined
    color: "#101216"
    SessionCanvas {
        id: sessionCanvas
        objectName: "sessionCanvas"
        anchors.fill: parent
        visible: !canvasPage.asking
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
        visible: !canvasPage.asking && (!sessionCanvas.frameReady || sessionManager.canvas.waiting === true)
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
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 460)
        spacing: 16
        visible: canvasPage.asking
        Label { width: parent.width; text: canvasPage.question.title || ""; font.bold: true; wrapMode: Text.Wrap; color: "#e6e9ee" }
        Label { width: parent.width; text: canvasPage.question.text || ""; wrapMode: Text.Wrap; color: "#e6e9ee" }
        Row {
            spacing: 12
            UiButton { text: canvasPage.question.cancel || ""; onClicked: sessionManager.answerCanvasQuestion(0) }
            UiButton { text: canvasPage.question.accept || ""; onClicked: sessionManager.answerCanvasQuestion(1) }
        }
    }
}
