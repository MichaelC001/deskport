import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
UiPage {
    id: page
    objectName: qsTr("Device settings")
    property var preferences: null
    property string deviceName: ""
    property bool changed: false
    property var peer: {
        if (!preferences) return null
        var entries = peerManager.peers
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].hostId && entries[i].hostId.toLowerCase() === preferences.deviceId.toLowerCase()) return entries[i]
        return null
    }
    heading: deviceName
    description: qsTr("Saved only for this device. Changes apply on the next connection.")
    function save() { preferences.save(); changed = true }
    UiCard {
        visible: page.peer !== null
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: qsTr("Domain name or IP address"); color: ui.text }
            TextField {
                id: address; objectName: "deviceAddress"; Layout.fillWidth: true
                text: page.peer ? page.peer.address : ""
                placeholderText: qsTr("Computer name or IP, without port")
            }
            Label { text: qsTr("Your existing binding is kept. Reconnect to use the saved address."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            UiButton {
                text: qsTr("Save"); enabled: page.peer !== null && !peerManager.busy
                onClicked: {
                    if (peerManager.editPeer(page.peer.fingerprint, page.peer.name, address.text, page.peer.hostPort, page.peer.bindingPort)) {
                        addressError.text = ""; page.changed = true
                    } else addressError.text = peerManager.status
                }
            }
            Label { id: addressError; visible: text.length > 0; textFormat: Text.PlainText; color: ui.warning; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: qsTr("Picture mode"); color: ui.text; font.pixelSize: ui.title }
            ComboBox {
                objectName: "devicePictureMode"; Layout.fillWidth: true
                model: [qsTr("Automatic (recommended)"), qsTr("Clear"), qsTr("Smooth"), qsTr("Save data"), qsTr("Custom")]
                currentIndex: preferences.smartStreaming ? 0 : preferences.fps === 60 && preferences.bitrateKbps === 40000 ? 1 : preferences.fps === 60 && preferences.bitrateKbps === 15000 ? 2 : preferences.fps === 30 && preferences.bitrateKbps === 5000 ? 3 : 4
                onActivated: function(index) {
                    if (index === 4) { stackView.push(Qt.resolvedUrl("DeviceAdvanced.qml"), {preferences: page.preferences, deviceName: page.deviceName}); return }
                    preferences.smartStreaming = index === 0
                    if (index > 0) { preferences.fps = index === 3 ? 30 : 60; preferences.bitrateKbps = index === 1 ? 40000 : index === 2 ? 15000 : 5000 }
                    save()
                }
            }
            Label { text: qsTr("Automatic uses a resolution-aware bandwidth limit. Save data uses 30 fps with a 5 Mbps video limit."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Virtual screen"); color: ui.text }
            ComboBox {
                objectName: "deviceDisplayPolicy"; Layout.fillWidth: true
                model: [qsTr("Primary screen and mirror others (default)"), qsTr("Primary screen and turn off others"), qsTr("Use client as an extended screen")]
                currentIndex: preferences.displayPolicy
                onActivated: { preferences.displayPolicy = currentIndex; save() }
            }
            Label { text: qsTr("The previous screen layout is restored automatically when the session ends."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Match the client window resolution"); checked: preferences.adaptiveResolution; onClicked: { preferences.adaptiveResolution = checked; save() } }
            Switch { text: qsTr("Receive sound from this device"); checked: preferences.remoteAudio; onClicked: { preferences.remoteAudio = checked; save() } }
            Switch { text: qsTr("Allow keyboard, pointer and controller input"); checked: preferences.remoteInput; onClicked: { preferences.remoteInput = checked; save() } }
            Switch { text: qsTr("Share text, images and files during a session"); checked: preferences.sharedClipboard; enabled: preferences.remoteInput; onClicked: { preferences.sharedClipboard = checked; save() } }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            UiButton { objectName: "deviceAdvancedButton"; text: qsTr("Advanced streaming settings"); Layout.fillWidth: true; onClicked: stackView.push(Qt.resolvedUrl("DeviceAdvanced.qml"), {preferences: page.preferences, deviceName: page.deviceName}) }
        }
    }
    Label { visible: page.changed; text: qsTr("Saved · reconnect to apply changes."); color: ui.accent; wrapMode: Text.WordWrap; Layout.fillWidth: true }
}
