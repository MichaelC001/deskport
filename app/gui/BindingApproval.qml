import QtQuick 2.9
import QtQuick.Controls 2.2

NavigableDialog {
    id: bindingApproval
    objectName: "bindingApproval"
    property var manager
    property var appWindow
    property string transaction: ""
    property string peerText: ""
    property bool clientOnly: false
    title: qsTr("Bind with this device?")
    // Closing the panel any other way answers "Not now".
    property bool answered: false
    footer: DialogButtonBox {
        alignment: Qt.AlignRight
        spacing: 8
        padding: 20; topPadding: 4
        background: Item {}
        UiButton { text: qsTr("Not now"); DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
        UiButton { text: qsTr("Allow & bind"); filled: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
    }
    onAboutToShow: answered = false
    onAccepted: { answered = true; manager.approve(transaction) }
    onRejected: { answered = true; manager.reject(transaction) }
    onClosed: if (!answered) { answered = true; manager.reject(transaction) }
    contentItem: Label {
        color: ui.text
        textFormat: Text.PlainText
        text: bindingApproval.peerText + "\n\n" + (clientOnly ? qsTr("Allow this device to view and control this computer? Binding saves permission without starting a connection or interrupting existing sessions. This does not grant access to the requesting device. Accept only a request you are expecting.") : qsTr("Allow this device and this computer to view and control each other? Binding saves permission without starting a connection or interrupting existing sessions. Choose Connect when you are ready. Accept only a request you are expecting."))
        wrapMode: Text.WordWrap
    }
    Connections {
        target: bindingApproval.manager
        function onIncomingRequest() {
            bindingApproval.transaction = bindingApproval.manager.requestId
            bindingApproval.clientOnly = bindingApproval.manager.pendingClientOnly
            bindingApproval.peerText = bindingApproval.manager.pendingName
            bindingApproval.open()
            console.info("Binding: approval dialog opened:", bindingApproval.visible)
            bindingApproval.appWindow.show()
            bindingApproval.appWindow.raise()
            bindingApproval.appWindow.requestActivate()
        }
        function onChanged() {
            if (bindingApproval.visible && bindingApproval.manager.requestId !== bindingApproval.transaction) {
                // The request ended elsewhere; closing must not answer it.
                bindingApproval.answered = true
                bindingApproval.close()
            }
        }
    }
}
