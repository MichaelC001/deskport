import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0

UiPage {
    objectName: qsTr("Advanced streaming settings")
    heading: deviceSettings ? qsTr("Settings for %1").arg(deviceName) : qsTr("Make DeskPort your own.")
    description: deviceSettings ? qsTr("These settings apply only to this device. Reconnect to apply changes.") : qsTr("Default settings for new devices. Choose Device settings in a device menu to customize a connection.")
    id: page
    property var preferences: null
    property string deviceName: ""
    readonly property bool deviceSettings: true
    property bool changed: false
    readonly property int sectionIndex: deviceSettings && sections.currentIndex === 3 ? 4 : sections.currentIndex
    function save() { preferences.save(); changed = true }
    Label { visible: page.changed; text: qsTr("Saved · reconnect to apply changes."); color: ui.accent; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    Label { text: qsTr("Connecting to remote computers"); color: ui.text; font.pixelSize: ui.title; font.bold: true; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    ComboBox {
        id: sections
        objectName: "settingsSections"
        Layout.fillWidth: true
        textRole: "label"
        model: ListModel {
            id: sectionModel
            ListElement { label: qsTr("Picture") }
            ListElement { label: qsTr("Input") }
            ListElement { label: qsTr("Sound") }
            ListElement { label: qsTr("Advanced") }
        }
    }

    UiCard {
        visible: page.sectionIndex === 0

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Picture"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            UiButton { id: pictureDetails; objectName: "pictureDetails"; text: qsTr("Picture adjustments"); checkable: true; highlighted: checked; onClicked: {} }
            ColumnLayout {
            visible: pictureDetails.checked; Layout.fillWidth: true; spacing: ui.gap
            Label { text: preferences.adaptiveResolution ? qsTr("Fallback resolution") : qsTr("Resolution"); color: ui.muted }
            ComboBox {
                id: resolution; objectName: "resolutionChoice"
                model: ["1920 × 1080", "2560 × 1440", "2880 × 1800", "3840 × 2160"]
                property var widths: [1920,2560,2880,3840]
                property var heights: [1080,1440,1800,2160]
                currentIndex: { for (var i=0; i<widths.length; i++) if (preferences.width===widths[i] && preferences.height===heights[i]) return i; return -1 }
                displayText: currentIndex < 0 ? preferences.width + " × " + preferences.height : currentText
                onActivated: function(index) { preferences.width=widths[index]; preferences.height=heights[index]; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Connection window"); color: ui.muted }
            ComboBox {
                objectName: "windowModeChoice"
                textRole: "label"
                model: ListModel {
                    ListElement { label: qsTr("Full screen") }
                    ListElement { label: qsTr("Borderless full screen") }
                    ListElement { label: qsTr("Window") }
                }
                currentIndex: preferences.windowMode
                onActivated: function(index) { preferences.windowMode=index; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Frame rate"); color: ui.muted }
            ComboBox {
                model: ["30 fps", "60 fps", "90 fps", "120 fps"]
                property var rates: [30,60,90,120]
                currentIndex: rates.indexOf(preferences.fps)
                displayText: currentIndex < 0 ? preferences.fps + " fps" : currentText
                onActivated: function(index) { preferences.smartStreaming=false; preferences.fps=rates[index]; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Bandwidth · %1 Mbps").arg(Math.round(preferences.bitrateKbps/1000)); color: ui.muted }
            Slider { objectName: "bitrateSlider"; from: 5; to: 100; stepSize: 1; value: preferences.bitrateKbps/1000; Layout.fillWidth: true; onMoved: { preferences.smartStreaming=false; preferences.bitrateKbps=Math.round(value)*1000; save() } }
            Label { text: qsTr("Higher values improve detail and use more network capacity. Keep your existing advanced values unless you move this slider."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { objectName: "framePacingSwitch"; text: qsTr("Smooth frame pacing"); checked: preferences.smartStreaming || preferences.framePacing; enabled: !preferences.smartStreaming; onClicked: { preferences.framePacing=checked; save() } }
            Switch { text: qsTr("Show streaming statistics"); checked: preferences.showPerformanceOverlay; onClicked: { preferences.showPerformanceOverlay=checked; save() } }
            Switch { text: qsTr("Synchronize frames to this display"); checked: preferences.enableVsync; onClicked: { preferences.enableVsync=checked; save() } }
            }
        }
    }
    UiCard {
        visible: page.sectionIndex === 1

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Keyboard & pointer"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Use a desktop-style pointer"); checked: preferences.absoluteMouseMode; onClicked: { preferences.absoluteMouseMode=checked; save() } }
            Switch { objectName: "localCursorSwitch"; text: qsTr("Always show a local pointer in desktop mode"); checked: preferences.showLocalCursor; enabled: preferences.absoluteMouseMode; onClicked: { preferences.showLocalCursor=checked; save() } }
            Label { text: qsTr("Keeps the pointer visible if the host hides its cursor. Turn this off if you see two pointers."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Reverse scrolling direction"); checked: preferences.reverseScrollDirection; onClicked: { preferences.reverseScrollDirection=checked; save() } }
            Label { text: qsTr("Send system shortcuts to the remote computer"); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ComboBox {
                objectName: "systemKeysChoice"
                textRole: "label"
                model: ListModel {
                    ListElement { label: qsTr("Never") }
                    ListElement { label: qsTr("Only in full screen") }
                    ListElement { label: qsTr("Always") }
                }
                currentIndex: preferences.captureSysKeysMode
                onActivated: function(index) { preferences.captureSysKeysMode=index; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("On a Mac host, Super / Windows sends Command and Alt sends Option. Choose Always to forward Super + Space in a window. Changes apply on the next connection."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Keyboard follows the pointer inside the focused video. Leaving releases held keys and buttons. Click to focus; system-reserved shortcuts may stay local."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Release remote input with Ctrl + Alt + Shift + Z."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        visible: page.sectionIndex === 2

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Sound from the remote computer"); color: ui.text; font.pixelSize: ui.title; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { text: qsTr("Mute when DeskPort loses focus"); checked: preferences.muteOnFocusLoss; onClicked: { preferences.muteOnFocusLoss=checked; save() } }
            Switch { text: qsTr("Also play audio on the host"); checked: preferences.playAudioOnHost; onClicked: { preferences.playAudioOnHost=checked; save() } }
        }
    }
    UiCard {
        visible: page.sectionIndex === 4

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Advanced & support"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: qsTr("Custom resolutions, codecs, HDR, surround sound and controller options remain available in advanced settings."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ColumnLayout {
                UiButton { text: qsTr("Advanced settings"); onClicked: stackView.push(Qt.resolvedUrl("SettingsView.qml"), {"preferences": page.preferences}) }
                UiButton { text: qsTr("Permission guide"); onClicked: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }
            }
            UiButton { text: qsTr("Report a problem"); visible: SystemProperties.hasBrowser; onClicked: Qt.openUrlExternally("https://github.com/keithxc/deskport/issues") }
        }
    }
}
