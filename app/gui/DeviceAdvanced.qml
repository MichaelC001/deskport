import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0

// Advanced streaming settings for one device, expanded in place below the
// basic device settings.
ColumnLayout {
    id: page
    property var preferences: null
    signal saved()
    function save() { preferences.save(); saved() }
    Layout.fillWidth: true
    spacing: 16

    UiGroup {
        title: qsTr("Picture")
        footnote: qsTr("Automatic uses a resolution-aware bandwidth limit. Save data uses 30 fps with a 5 Mbps video limit.")
        UiChoiceRow {
            objectName: "devicePictureMode"
            iconSource: "qrc:/res/ui/picture.svg"
            title: qsTr("Picture mode")
            options: [qsTr("Automatic (recommended)"), qsTr("Clear"), qsTr("Smooth"), qsTr("Save data"), qsTr("Custom")]
            currentIndex: preferences.smartStreaming ? 0 : preferences.fps === 60 && preferences.bitrateKbps === 40000 ? 1 : preferences.fps === 60 && preferences.bitrateKbps === 15000 ? 2 : preferences.fps === 30 && preferences.bitrateKbps === 5000 ? 3 : 4
            onActivated: function(index) {
                if (index === 4) { preferences.smartStreaming = false; save(); return }
                preferences.smartStreaming = index === 0
                if (index > 0) { preferences.fps = index === 3 ? 30 : 60; preferences.bitrateKbps = index === 1 ? 40000 : index === 2 ? 15000 : 5000 }
                save()
            }
        }
        UiChoiceRow {
            id: resolution; objectName: "resolutionChoice"
            iconSource: "qrc:/res/ui/screen.svg"
            title: preferences.adaptiveResolution ? qsTr("Fallback resolution") : qsTr("Resolution")
            options: ["1920 × 1080", "2560 × 1440", "2880 × 1800", "3840 × 2160"]
            property var widths: [1920,2560,2880,3840]
            property var heights: [1080,1440,1800,2160]
            currentIndex: { for (var i=0; i<widths.length; i++) if (preferences.width===widths[i] && preferences.height===heights[i]) return i; return -1 }
            displayText: currentIndex < 0 ? preferences.width + " × " + preferences.height : ""
            onActivated: function(index) { preferences.width=widths[index]; preferences.height=heights[index]; save() }
        }
        UiChoiceRow {
            objectName: "windowModeChoice"
            iconSource: "qrc:/res/ui/window.svg"
            title: qsTr("Connection window")
            note: qsTr("Ctrl+Alt+Shift+X also toggles full screen during a connection.")
            options: [qsTr("Full screen"), qsTr("Borderless full screen"), qsTr("Window")]
            currentIndex: preferences.windowMode
            onActivated: function(index) { preferences.windowMode=index; save() }
        }
        UiChoiceRow {
            objectName: "frameRateChoice"
            iconSource: "qrc:/res/ui/rate.svg"
            title: qsTr("Frame rate")
            options: ["30 fps", "60 fps", "90 fps", "120 fps"]
            property var rates: [30,60,90,120]
            currentIndex: rates.indexOf(preferences.fps)
            displayText: currentIndex < 0 ? preferences.fps + " fps" : ""
            onActivated: function(index) { preferences.smartStreaming=false; preferences.fps=rates[index]; save() }
        }
        Item {
            width: parent.width
            implicitHeight: bandwidth.implicitHeight + 16
            ColumnLayout {
                id: bandwidth
                x: 16; y: 8; width: parent.width - 32; spacing: 2
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    UiIconTile { source: "qrc:/res/ui/bandwidth.svg" }
                    Label { text: qsTr("Bandwidth"); color: ui.text; Layout.fillWidth: true }
                    Label { text: qsTr("%1 Mbps").arg(Math.round(preferences.bitrateKbps/1000)); color: ui.muted }
                }
                Slider {
                    objectName: "bitrateSlider"; from: 5; to: 100; stepSize: 1; value: preferences.bitrateKbps/1000
                    Layout.fillWidth: true; Layout.leftMargin: 38
                    onMoved: { preferences.smartStreaming=false; preferences.bitrateKbps=Math.round(value)*1000; save() }
                }
                Label { text: qsTr("Higher values improve detail and use more network capacity. Keep your existing advanced values unless you move this slider."); color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 44 }
            }
            Rectangle { anchors.bottom: parent.bottom; x: 64; width: parent.width - 80; height: 1; color: ui.line }
        }
        UiRow {
            objectName: "framePacingSwitch"
            iconSource: "qrc:/res/ui/stats.svg"
            title: qsTr("Smooth frame pacing")
            switchable: true; on: preferences.smartStreaming || preferences.framePacing; enabled: !preferences.smartStreaming
            onSwitched: function(value) { preferences.framePacing=value; save() }
        }
        UiRow {
            objectName: "performanceOverlaySwitch"
            iconSource: "qrc:/res/ui/diagnostics.svg"
            title: qsTr("Show streaming statistics")
            switchable: true; on: preferences.showPerformanceOverlay
            onSwitched: function(value) { preferences.showPerformanceOverlay=value; save() }
        }
        UiRow {
            objectName: "vsyncSwitch"
            iconSource: "qrc:/res/ui/fullscreen.svg"
            title: qsTr("Synchronize frames to this display")
            switchable: true; on: preferences.enableVsync; divider: false
            onSwitched: function(value) { preferences.enableVsync=value; save() }
        }
    }
    UiGroup {
        title: qsTr("Keyboard & pointer")
        footnote: qsTr("Keyboard follows the pointer inside the focused video. Leaving releases held keys and buttons. Click to focus; system-reserved shortcuts may stay local.") + " " + qsTr("Release remote input with Ctrl + Alt + Shift + Z.")
        UiRow {
            iconSource: "qrc:/res/ui/pointer.svg"
            title: qsTr("Use a desktop-style pointer")
            switchable: true; on: preferences.absoluteMouseMode
            onSwitched: function(value) { preferences.absoluteMouseMode=value; save() }
        }
        UiRow {
            objectName: "localCursorSwitch"
            iconSource: "qrc:/res/ui/pointer.svg"
            title: qsTr("Always show a local pointer in desktop mode")
            detail: qsTr("Keeps the pointer visible if the host hides its cursor. Turn this off if you see two pointers.")
            switchable: true; on: preferences.showLocalCursor; enabled: preferences.absoluteMouseMode
            onSwitched: function(value) { preferences.showLocalCursor=value; save() }
        }
        UiRow {
            iconSource: "qrc:/res/ui/scroll.svg"
            title: qsTr("Reverse scrolling direction")
            switchable: true; on: preferences.reverseScrollDirection
            onSwitched: function(value) { preferences.reverseScrollDirection=value; save() }
        }
        UiChoiceRow {
            objectName: "systemKeysChoice"
            iconSource: "qrc:/res/ui/shortcuts.svg"
            title: qsTr("Send system shortcuts to the remote computer")
            note: qsTr("On a Mac host, Super / Windows sends Command and Alt sends Option. Choose Always to forward Super + Space in a window. Changes apply on the next connection.")
            options: [qsTr("Never"), qsTr("Only in full screen"), qsTr("Always")]
            currentIndex: preferences.captureSysKeysMode
            onActivated: function(index) { preferences.captureSysKeysMode=index; save() }
        }
        UiRow {
            objectName: "sharedClipboardSwitch"
            iconSource: "qrc:/res/ui/clipboard.svg"
            title: qsTr("Share text, images and files during a session")
            switchable: true; on: preferences.sharedClipboard; enabled: preferences.remoteInput; divider: false
            onSwitched: function(value) { preferences.sharedClipboard=value; save() }
        }
    }
    UiGroup {
        title: qsTr("Sound from the remote computer")
        UiRow {
            iconSource: "qrc:/res/ui/sound.svg"
            title: qsTr("Mute when DeskPort loses focus")
            switchable: true; on: preferences.muteOnFocusLoss
            onSwitched: function(value) { preferences.muteOnFocusLoss=value; save() }
        }
        UiRow {
            iconSource: "qrc:/res/ui/sound.svg"
            title: qsTr("Also play audio on the host")
            switchable: true; on: preferences.playAudioOnHost; divider: false
            onSwitched: function(value) { preferences.playAudioOnHost=value; save() }
        }
    }
    UiGroup {
        footnote: qsTr("Custom resolutions, codecs, HDR, surround sound and controller options remain available in advanced settings.")
        UiRow {
            objectName: "allStreamingOptions"
            iconSource: "qrc:/res/ui/legacy.svg"
            title: qsTr("All streaming options")
            divider: false
            onClicked: legacyPanel.open()
        }
    }

    // The complete upstream option list, in the same floating panel style.
    NavigableDialog {
        id: legacyPanel
        objectName: "allStreamingOptionsPanel"
        title: qsTr("All streaming options")
        height: maximumHeight
        padding: 0; topPadding: 0; bottomPadding: 8
        // Loaded by URL on demand, with this device's preferences from the start.
        onAboutToShow: legacyLoader.setSource(Qt.resolvedUrl("SettingsView.qml"), {"preferences": page.preferences})
        onClosed: { legacyLoader.source = ""; page.save() }
        contentItem: Loader { id: legacyLoader }
    }
}
