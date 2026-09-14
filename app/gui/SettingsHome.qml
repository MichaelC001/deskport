import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0

UiPage {
    objectName: qsTr("Settings")
    heading: deviceSettings ? qsTr("Settings for %1").arg(deviceName) : qsTr("Make DeskPort your own.")
    description: deviceSettings ? qsTr("These settings apply only to this device. Reconnect to apply changes.") : qsTr("Default settings for new devices. Choose Device settings in a device menu to customize a connection.")
    id: page
    property var preferences: StreamingPreferences
    property string deviceName: ""
    readonly property bool deviceSettings: preferences.deviceId.length > 0
    property bool changed: false
    readonly property int sectionIndex: deviceSettings && sections.currentIndex === 3 ? 4 : sections.currentIndex
    function save() { preferences.save(); changed = true }
    function applyPreset(index) {
        var rates = [30, 60, 60]
        var bitrates = [10000, 40000, 15000]
        preferences.fps = rates[index]
        preferences.bitrateKbps = bitrates[index]
        save()
    }
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
            ListElement { label: qsTr("Connections") }
            ListElement { label: qsTr("Advanced") }
            ListElement { label: qsTr("Appearance") }
            Component.onCompleted: if (page.deviceSettings) { remove(5); remove(3) }
        }
    }

    UiCard {
        visible: !page.deviceSettings && page.sectionIndex === 5
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: qsTr("Appearance"); font.pixelSize: ui.title; font.bold: true; color: ui.text }
            ComboBox {
                objectName: "themeChoice"; Layout.fillWidth: true
                model: [qsTr("Follow system"), qsTr("Light"), qsTr("Dark")]
                currentIndex: preferences.uiTheme
                onActivated: function(index) { preferences.uiTheme = index; preferences.save() }
            }
            Label { text: qsTr("Appearance changes apply immediately."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        visible: !page.deviceSettings && page.sectionIndex === 5
        ColumnLayout {
            anchors.fill: parent; spacing: 10
            Label { text: qsTr("Language"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            ComboBox {
                id: languageChoice
                objectName: "languageChoice"
                Layout.fillWidth: true
                textRole: "label"
                popup: Popup {
                    y: languageChoice.height
                    width: languageChoice.width
                    height: Math.min(340, contentItem.implicitHeight + topPadding + bottomPadding)
                    margins: 8
                    padding: 4
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: languageChoice.popup.visible ? languageChoice.delegateModel : null
                        currentIndex: languageChoice.highlightedIndex
                        ScrollIndicator.vertical: ScrollIndicator {}
                    }
                }
                model: [
                    { label: qsTr("Follow system"), value: preferences.LANG_AUTO },
                    { label: "English", value: preferences.LANG_EN },
                    { label: "简体中文", value: preferences.LANG_ZH_CN },
                    { label: "繁體中文", value: preferences.LANG_ZH_TW },
                    { label: "日本語", value: preferences.LANG_JA },
                    { label: "한국어", value: preferences.LANG_KO },
                    { label: "Deutsch", value: preferences.LANG_DE },
                    { label: "Français", value: preferences.LANG_FR },
                    { label: "Español", value: preferences.LANG_ES },
                    { label: "Italiano", value: preferences.LANG_IT },
                    { label: "Português", value: preferences.LANG_PT },
                    { label: "Русский", value: preferences.LANG_RU },
                    { label: "Nederlands", value: preferences.LANG_NL },
                    { label: "Polski", value: preferences.LANG_PL },
                    { label: "Čeština", value: preferences.LANG_CS },
                    { label: "Svenska", value: preferences.LANG_SV },
                    { label: "Norsk bokmål", value: preferences.LANG_NB_NO },
                    { label: "Türkçe", value: preferences.LANG_TR },
                    { label: "Magyar", value: preferences.LANG_HU },
                    { label: "Ελληνικά", value: preferences.LANG_EL },
                    { label: "Tiếng Việt", value: preferences.LANG_VI },
                    { label: "ภาษาไทย", value: preferences.LANG_TH }
                ]
                currentIndex: {
                    for (var i = 0; i < model.length; ++i)
                        if (model[i].value === preferences.language) return i
                    return -1
                }
                onActivated: function(index) {
                    var value = model[index].value
                    if (preferences.language === value) return
                    preferences.language = value
                    save()
                    if (!preferences.retranslate())
                        ToolTip.show(qsTr("Restart DeskPort to apply this language."), 5000)
                    else if (typeof window !== "undefined")
                        window.clearOnBack = true
                }
            }
            Label { text: qsTr("Saved on this computer. Missing translations appear in English."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        visible: page.sectionIndex === 0

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Picture"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Match the client window resolution"); checked: preferences.adaptiveResolution; onClicked: { preferences.adaptiveResolution=checked; save() } }
            Label { text: qsTr("Uses the built-in virtual display on a bound Mac. Resizing briefly reconnects the picture and keeps your apps open. Other hosts use the resolution below."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { objectName: "smartStreamingSwitch"; text: qsTr("Smart streaming"); checked: preferences.smartStreaming; onClicked: { preferences.smartStreaming=checked; save() } }
            Label { text: qsTr("Uses a resolution-aware bandwidth ceiling and smooth frame pacing. Turn off to use manual bandwidth and pacing."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Quality preset"); color: ui.text }
            ComboBox {
                objectName: "qualityPreset"; Layout.fillWidth: true
                model: [qsTr("Choose a preset…"), qsTr("Office · 30 fps / 10 Mbps"), qsTr("Clear · 60 fps / 40 Mbps"), qsTr("Smooth · 60 fps / 15 Mbps")]
                currentIndex: 0
                onActivated: function(index) { if (index > 0) page.applyPreset(index - 1); currentIndex = 0 }
            }
            Label { text: qsTr("Presets change frame rate and bandwidth only. Tune them for your network below."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                onActivated: function(index) { preferences.fps=rates[index]; save() }
                Layout.preferredWidth: 250
            }
            Label { text: qsTr("Bandwidth · %1 Mbps").arg(Math.round(preferences.bitrateKbps/1000)); color: ui.muted }
            Slider { objectName: "bitrateSlider"; from: 5; to: 100; stepSize: 1; value: preferences.bitrateKbps/1000; Layout.fillWidth: true; onMoved: { preferences.bitrateKbps=Math.round(value)*1000; save() } }
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
            Switch { objectName: "remoteInputSwitch"; text: qsTr("Allow keyboard, pointer and controller input"); checked: preferences.remoteInput; onClicked: { preferences.remoteInput=checked; save() } }
            Label { text: qsTr("Turn off for a view-only connection. Local DeskPort shortcuts remain available."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
            Switch { objectName: "sharedClipboardSwitch"; text: qsTr("Share plain text clipboard during a session"); enabled: preferences.remoteInput; checked: preferences.sharedClipboard; onClicked: { preferences.sharedClipboard=checked; save() } }
            Label { text: qsTr("Reconnect to share new copies with this bound device. Up to 128 MiB of plain text; images and files are not shared."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Keyboard follows the pointer inside the focused video. Leaving releases held keys and buttons. Click to focus; system-reserved shortcuts may stay local."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Label { text: qsTr("Release remote input with Ctrl + Alt + Shift + Z."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    UiCard {
        visible: page.sectionIndex === 2

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Sound from the remote computer"); color: ui.text; font.pixelSize: ui.title; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch { objectName: "remoteAudioSwitch"; text: qsTr("Receive sound from this device"); checked: preferences.remoteAudio; onClicked: { preferences.remoteAudio=checked; save() } }
            Switch { text: qsTr("Mute when DeskPort loses focus"); checked: preferences.muteOnFocusLoss; onClicked: { preferences.muteOnFocusLoss=checked; save() } }
            Switch { text: qsTr("Also play audio on the host"); checked: preferences.playAudioOnHost; onClicked: { preferences.playAudioOnHost=checked; save() } }
        }
    }
    UiCard {
        visible: !page.deviceSettings && page.sectionIndex === 3

        ColumnLayout {
            anchors.fill: parent; spacing: 12
            Label { text: qsTr("Connections"); color: ui.text; font.pixelSize: 20; font.weight: Font.DemiBold }
            Switch { text: qsTr("Discover nearby devices"); checked: preferences.enableMdns; onClicked: { preferences.enableMdns=checked; save() } }
            Switch { text: qsTr("Keep this computer awake while connected"); checked: preferences.keepAwake; onClicked: { preferences.keepAwake=checked; save() } }
            Label { text: qsTr("Device connection port"); color: ui.text }
            Label { text: qsTr("Usually leave this at 48991 on both computers. Video and audio ports are managed automatically. Previous entry ports stay available for saved devices."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                SpinBox { id: connectionPort; objectName: "connectionPort"; from: 1024; to: 65535; value: peerManager.port; editable: true; Layout.fillWidth: true }
                UiButton { text: qsTr("Apply"); onClicked: peerManager.setConnectionPort(connectionPort.value) }
            }
            Label { text: peerManager.status; textFormat: Text.PlainText; color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            UiButton { text: qsTr("Manage saved access"); onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
        }
    }
    UiCard {
        visible: !page.deviceSettings
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: qsTr("Sharing this computer"); color: ui.text; font.pixelSize: ui.title; font.bold: true }
            Label { text: qsTr("Manage incoming access, shared audio and login startup on the Sharing page."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            UiButton { text: qsTr("Open sharing settings"); onClicked: navigateTo("qrc:/gui/HostView.qml", "HostView") }
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
