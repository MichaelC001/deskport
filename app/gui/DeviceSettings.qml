import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
UiPage {
    id: page
    objectName: qsTr("Device settings")
    property var preferences: null
    property string deviceName: ""
    property string deviceId: ""
    property bool popupMode: false
    signal advancedRequested()
    property bool changed: false
    heading: deviceName
    description: qsTr("Saved only for this device. Changes apply on the next connection.")
    function save() { preferences.save(); changed = true }
    function openAdvanced() {
        if (popupMode) advancedRequested()
        else stackView.push(Qt.resolvedUrl("DeviceAdvanced.qml"), {preferences: page.preferences, deviceName: page.deviceName})
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
                    if (index === 4) { preferences.smartStreaming = false; save(); openAdvanced(); return }
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
            Label { text: qsTr("Desktop adjustment"); color: ui.text }
            ComboBox {
                objectName: "deviceDesktopAdjustment"; Layout.fillWidth: true
                readonly property var factors: preferences.desktopAdjustmentChoices
                model: preferences.desktopAdjustmentLabels
                currentIndex: Math.max(0, factors.indexOf(preferences.desktopAdjustment))
                onActivated: function(index) {
                    preferences.desktopAdjustment = factors[index]; save()
                    if (typeof window !== "undefined" && window.activeHostId === page.deviceId && window.activeStreamPage)
                        window.activeStreamPage.session.setDesktopAdjustment(factors[index])
                }
            }
            Label { text: qsTr("0.5 makes controls larger; 1.5 fits more content. Applies after the automatic desktop calculation and takes effect immediately during a connection."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch {
                objectName: "deviceFullScreen"
                text: qsTr("Full screen")
                // Window modes as in Advanced: 2 is a window; full screen uses the platform's recommended mode.
                checked: preferences.windowMode !== 2
                onClicked: { preferences.windowMode = checked ? preferences.recommendedFullScreenMode : 2; save() }
            }
            Label { text: qsTr("Ctrl+Alt+Shift+X also toggles full screen during a connection."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Match the client window resolution"); checked: preferences.adaptiveResolution; onClicked: { preferences.adaptiveResolution = checked; save() } }
            Switch { text: qsTr("Receive sound from this device"); checked: preferences.remoteAudio; onClicked: { preferences.remoteAudio = checked; save() } }
            Switch { text: qsTr("Allow keyboard, pointer and controller input"); checked: preferences.remoteInput; onClicked: { preferences.remoteInput = checked; save() } }
            Switch { text: qsTr("Share text, images and files during a session"); checked: preferences.sharedClipboard; enabled: preferences.remoteInput; onClicked: { preferences.sharedClipboard = checked; save() } }
        }
    }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            UiButton { objectName: "deviceAdvancedButton"; text: qsTr("Advanced streaming settings"); Layout.fillWidth: true; onClicked: openAdvanced() }
        }
    }
    Label { visible: page.changed; text: qsTr("Saved · reconnect to apply changes."); color: ui.accent; wrapMode: Text.WordWrap; Layout.fillWidth: true }
}
