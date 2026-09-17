import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

Dialog {
    id: editDialog
    property var manager: typeof peerManager === "undefined" ? null : peerManager
    function edit(peer) {
        if (!peer) return
        fingerprint = peer.fingerprint
        deviceName.text = peer.name
        deviceAddress.text = peer.address
        hostPort.value = peer.hostPort || 48989
        bindingPort.value = peer.bindingPort || 48991
        advancedPorts.checked = false
        editError.text = ""
        open()
    }
    property string fingerprint: ""
    title: qsTranslate("BindView", "Edit device")
    anchors.centerIn: parent
    width: Math.max(280, Math.min(parent ? parent.width - 32 : 460, 460))
    modal: true
    contentItem: ColumnLayout {
        spacing: 10
        Label { text: qsTranslate("BindView", "Device name") }
        TextField { id: deviceName; objectName: "editPeerName"; Layout.fillWidth: true; maximumLength: 64 }
        Label { text: qsTranslate("BindView", "Domain name or IP address") }
        TextField { id: deviceAddress; objectName: "editPeerAddress"; Layout.fillWidth: true; placeholderText: qsTranslate("BindView", "Computer name or IP, without port") }
        Label { text: qsTranslate("BindView", "A domain name is saved as entered and resolved again when connecting."); wrapMode: Text.WordWrap; Layout.fillWidth: true }
        CheckBox { id: advancedPorts; text: qsTranslate("BindView", "Advanced port overrides"); checked: false }
        Label { text: qsTranslate("BindView", "Host port"); visible: advancedPorts.checked }
        SpinBox { id: hostPort; visible: advancedPorts.checked; from: 1024; to: 65514; editable: true; Layout.fillWidth: true }
        Label { text: qsTranslate("BindView", "Binding port"); visible: advancedPorts.checked }
        SpinBox { id: bindingPort; visible: advancedPorts.checked; from: 1; to: 65535; editable: true; Layout.fillWidth: true }
        Label { id: editError; textFormat: Text.PlainText; color: ui.warning; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            UiButton { text: qsTranslate("BindView", "Cancel"); onClicked: editDialog.close() }
            Item { Layout.fillWidth: true }
            UiButton {
                objectName: "savePeerAddress"; text: qsTranslate("BindView", "Save"); highlighted: true; enabled: manager && !manager.busy
                onClicked: {
                    if (manager.editPeer(editDialog.fingerprint, deviceName.text, deviceAddress.text, hostPort.value, bindingPort.value)) editDialog.close()
                    else editError.text = manager.status
                }
            }
        }
    }
}
