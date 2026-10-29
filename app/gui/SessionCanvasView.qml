import QtQuick
import QtQuick.Controls
import DeskPort.Canvas 1.0

Rectangle {
    id: canvasPage
    readonly property var question: sessionManager.canvas.question || ({})
    readonly property bool asking: question.id !== undefined
    readonly property bool showBackdrop: !asking && (!sessionCanvas.frameReady || sessionManager.canvas.waiting === true)
    color: "#101216"
    Image {
        objectName: "canvasTransitionBackdrop"
        anchors.fill: parent
        source: sessionManager.presentation && sessionManager.presentation.backdrop !== undefined
                ? sessionManager.presentation.backdrop : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: false
        visible: canvasPage.showBackdrop
    }
    Rectangle {
        anchors.fill: parent
        color: "#5a101216"
        visible: canvasPage.showBackdrop
    }
    SessionCanvas {
        id: sessionCanvas
        objectName: "sessionCanvas"
        transitionWaiting: sessionManager.canvas.waiting === true
        property string transitionLabel: sessionManager.canvas.label || qsTranslate("DesktopSegue", "Connecting to desktop…")
        property string transitionCancelLabel: qsTranslate("StreamSegue", "Cancel connection")
        anchors.fill: parent
        visible: !canvasPage.asking
        presentation: sessionManager.presentation
        textInputEnabled: sessionManager.canvas.textInput === true
        cursorHidden: sessionManager.canvas.cursorHidden === true
        captureRequested: sessionManager.canvas.grab === true
        relativePointer: sessionManager.canvas.relative === true
        pointerRegion: sessionManager.canvas.pointerRegion || ({})
        onViewportChanged: function(state) { sessionManager.updateCanvas(state) }
        onInput: function(event) { sessionManager.canvasInput(event) }
        onTransitionCancelRequested: sessionManager.disconnectSession(sessionManager.selectedId)
    }
    Column {
        anchors.centerIn: parent
        spacing: 16
        visible: canvasPage.showBackdrop
        BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: parent.visible }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: sessionManager.canvas.label || qsTranslate("DesktopSegue", "Connecting to desktop…")
            color: "#e6e9ee"
        }
        UiButton {
            anchors.horizontalCenter: parent.horizontalCenter
            objectName: "canvasCancelConnection"
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
            UiButton { objectName: "canvasConfirmConnection"; text: canvasPage.question.accept || ""; onClicked: sessionManager.answerCanvasQuestion(1) }
        }
    }
}
