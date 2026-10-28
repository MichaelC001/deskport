import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0
UiPage {
    id: page
    objectName: qsTr("Settings")
    heading: qsTr("Make DeskPort your own.")
    readonly property var preferences: StreamingPreferences
    function save() { preferences.save() }

    UiGroup {
        title: qsTr("Appearance")
        footnote: qsTr("Appearance changes apply immediately.")
        Item {
            objectName: "themeRow"
            width: parent.width
            implicitHeight: 56
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 14
                spacing: 12
                UiIconTile { source: "qrc:/res/ui/theme.svg" }
                Label { text: qsTr("Theme"); color: ui.text; Layout.fillWidth: true; elide: Text.ElideRight }
                UiSegmented {
                    objectName: "themeChoice"
                    Layout.preferredWidth: Math.min(270, page.width * 0.5)
                    model: [qsTr("System"), qsTr("Light"), qsTr("Dark")]
                    currentIndex: preferences.uiTheme
                    onActivated: function(index) { preferences.uiTheme = index; save() }
                }
            }
            Rectangle { anchors.bottom: parent.bottom; x: 64; width: parent.width - 80; height: 1; color: ui.line }
        }
        UiChoiceRow {
            objectName: "accentChoice"
            iconSource: "qrc:/res/ui/accent.svg"
            title: qsTr("Accent color")
            options: [qsTr("Follow system"), qsTr("Blue"), qsTr("Green"), qsTr("Purple"), qsTr("Orange")]
            currentIndex: preferences.uiAccent
            onActivated: function(index) { preferences.uiAccent = index; save() }
        }
        UiChoiceRow {
            id: languageChoice
            objectName: "languageChoice"
            iconSource: "qrc:/res/ui/language.svg"
            title: qsTr("Language")
            note: qsTr("Saved on this computer. Missing translations appear in English.")
            readonly property var languages: [
                { label: qsTr("Follow system"), value: preferences.LANG_AUTO }
            ].concat(preferences.sharedLanguages).concat([
                { label: "Čeština", value: preferences.LANG_CS },
                { label: "Svenska", value: preferences.LANG_SV },
                { label: "Norsk bokmål", value: preferences.LANG_NB_NO },
                { label: "Magyar", value: preferences.LANG_HU },
                { label: "Ελληνικά", value: preferences.LANG_EL }
            ])
            options: languages
            currentIndex: {
                for (var i = 0; i < languages.length; ++i)
                    if (languages[i].value === preferences.language) return i
                return -1
            }
            onActivated: function(index) {
                var value = languages[index].value
                if (preferences.language === value) return
                preferences.language = value
                save()
                if (!preferences.retranslate())
                    restartNotice.open()
                else if (typeof window !== "undefined")
                    window.clearOnBack = true
            }
        }
        UiRow {
            objectName: "showTrafficSwitch"
            iconSource: "qrc:/res/ui/traffic.svg"
            title: qsTr("Show data usage in the top bar")
            switchable: true; on: preferences.showTraffic
            onSwitched: function(value) { preferences.showTraffic = value; save() }
        }
        UiRow {
            objectName: "hideStreamTitleBarSwitch"
            iconSource: "qrc:/res/ui/window.svg"
            title: qsTr("Hide title bar while viewing a remote desktop")
            infoAccessibleName: qsTr("About hiding the title bar")
            switchable: true; on: preferences.hideStreamTitleBar; divider: false
            onInfoClicked: titleBarPanel.open()
            onSwitched: function(value) { preferences.hideStreamTitleBar = value; save() }
        }
    }

    UiGroup {
        title: qsTr("Keyboard shortcuts")
        footnote: typeof globalShortcut === "undefined" || !preferences.globalShortcutEnabled ? "" :
                  globalShortcut.error.length > 0 ? qsTr("Shortcut unavailable: %1").arg(globalShortcut.error) :
                  globalShortcut.desktopAssigned ? qsTr("Desktop shortcut: %1. Manage its binding in your desktop settings.").arg(globalShortcut.effectiveShortcut) : ""
        UiRow {
            objectName: "globalShortcutSwitch"
            iconSource: "qrc:/res/ui/input.svg"
            title: qsTr("Global window shortcut")
            value: preferences.shortcutDisplay(preferences.globalShortcutSequence)
            valueEditable: true
            valueAccessibleName: qsTr("Change shortcut")
            switchable: true; on: preferences.globalShortcutEnabled; divider: false
            Accessible.description: qsTr("%1 shows or hides DeskPort. Enabled by default; changes apply immediately.").arg(value)
            ToolTip.visible: hovered
            ToolTip.delay: 800
            ToolTip.text: Accessible.description
            onValueClicked: shortcutDialog.open()
            onSwitched: function(value) { preferences.globalShortcutEnabled = value; save() }
        }
    }

    ShortcutDialog { id: shortcutDialog }

    UiGroup {
        title: qsTr("Diagnostics and feedback")
        footnote: diagnostics.status.length > 0 ? diagnostics.status : qsTr("Off by default. Logs stay on this computer until you share them. See Privacy for what is recorded.")
        UiRow {
            objectName: "diagnosticsSwitch"
            iconSource: "qrc:/res/ui/diagnostics.svg"
            title: qsTr("Enable diagnostic logs")
            switchable: true; on: diagnostics.enabled
            onSwitched: function(value) { diagnostics.enabled = value }
        }
        // One way to report: it packs the logs into a ZIP and opens a draft GitHub issue.
        UiRow {
            objectName: "feedbackButton"
            iconSource: "qrc:/res/ui/report.svg"
            title: qsTr("Report a problem")
            onClicked: feedbackNotice.open()
        }
        UiRow {
            objectName: "showDiagnosticsBundle"
            visible: diagnostics.bundlePath.length > 0
            iconSource: "qrc:/res/ui/clipboard.svg"
            title: qsTr("Show ZIP in folder")
            detail: diagnostics.bundlePath
            onClicked: diagnostics.showBundle()
        }
        UiRow {
            objectName: "clearDiagnostics"
            iconSource: "qrc:/res/ui/trash.svg"
            title: qsTr("Clear saved diagnostics")
            destructive: true; divider: false
            onClicked: clearNotice.open()
        }
    }

    UiGroup {
        title: qsTr("About")
        UiRow {
            iconSource: "qrc:/res/ui/info.svg"
            readOnly: true
            title: qsTr("Version")
            value: SystemProperties.versionString
        }
        UiRow {
            objectName: "privacyRow"
            iconSource: "qrc:/res/ui/privacy.svg"
            title: qsTr("Privacy")
            onClicked: privacyPanel.open()
        }
        UiRow {
            objectName: "licensesRow"
            iconSource: "qrc:/res/ui/licenses.svg"
            title: qsTr("Licenses")
            divider: false
            onClicked: licensesPanel.open()
        }
    }

    NavigableMessageDialog {
        id: restartNotice
        objectName: "restartNotice"
        text: qsTr("Restart DeskPort to apply this language.")
        standardButtons: Dialog.Ok
    }
    NavigableMessageDialog {
        id: feedbackNotice
        objectName: "feedbackNotice"
        title: qsTr("Report a problem")
        text: qsTr("GitHub issues and attachments are public. This button creates a ZIP and opens a draft issue. Nothing is uploaded or submitted automatically. Review the ZIP, drag it into the issue, then submit it yourself.")
        standardButtons: Dialog.Ok | Dialog.Cancel
        acceptText: qsTr("Create ZIP and open issue")
        onAccepted: diagnostics.feedback()
    }
    NavigableMessageDialog {
        id: clearNotice
        objectName: "clearDiagnosticsNotice"
        title: qsTr("Clear saved diagnostics")
        text: qsTr("Turning logs off stops new recording. Clear saved diagnostics to delete existing logs and the generated ZIP. Older versions' raw logs are never included.")
        standardButtons: Dialog.Ok | Dialog.Cancel
        destructive: true
        acceptText: qsTr("Clear saved diagnostics")
        onAccepted: diagnostics.clear()
    }

    // Reading panels: a fixed title row, and a body that scrolls.
    component ReadingPanel: NavigableDialog {
        id: reading
        property var paragraphs: []
        height: Math.min(implicitHeight, maximumHeight)
        padding: 0; topPadding: 0; bottomPadding: 16
        contentItem: ScrollView {
            id: readingScroll
            clip: true
            implicitHeight: readingText.implicitHeight
            contentWidth: availableWidth
            ColumnLayout {
                id: readingText
                x: 20; width: readingScroll.availableWidth - 40
                spacing: 12
                Repeater {
                    model: reading.paragraphs
                    Label { text: modelData; textFormat: Text.PlainText; color: ui.text; wrapMode: Text.WordWrap; Layout.fillWidth: true; lineHeight: 1.15 }
                }
            }
        }
    }
    ReadingPanel {
        id: titleBarPanel
        objectName: "titleBarPanel"
        title: qsTr("Hide title bar while viewing a remote desktop")
        paragraphs: [
            qsTr("Off by default. When on, the remote desktop window has no title bar or window buttons, and the remote picture uses that space. The Devices page keeps its title bar. Use the tray icon or the global window shortcut to show, hide or disconnect. Changes apply immediately."),
            Qt.platform.os === "osx" ? qsTr("macOS: the close, minimize and zoom buttons are hidden and the picture reaches the top edge. Resize the window from its edges; use Fn+Control+arrow keys to tile it, or use full screen.")
                : Qt.platform.os === "windows" ? qsTr("Windows: the window cannot be moved or resized with the mouse. Turn this off or use full screen to change its size.")
                : qsTr("Linux: move or resize the window with your desktop's shortcuts, such as Meta+drag on KDE Plasma. Tiled windows are unaffected. On some older Wayland desktops, the title bar returns only after the window is reopened.")
        ]
    }
    ReadingPanel {
        id: privacyPanel
        objectName: "privacyPanel"
        title: qsTr("Privacy")
        paragraphs: [
            qsTr("DeskPort has no account and no DeskPort server. Your devices connect to each other directly. Settings, saved devices and approvals stay on this computer."),
            qsTr("Off by default. Collects connection, interaction setup and runtime event types, timing and resize dimensions from this app, its host and display helper. Changes apply immediately. Logs are limited to 9 MiB and kept for up to 7 days while DeskPort runs."),
            qsTr("IP addresses, domains, device names and arbitrary message text are omitted automatically. No key text, clipboard or screen content is collected. Each app run has a random anonymous ID. Filtering reduces detail and cannot diagnose every problem; review the archive before sharing."),
            qsTr("To check for updates, DeskPort asks GitHub for the latest release. Like Moonlight, it downloads public host compatibility data and controller mappings from moonlight-stream.org. Reporting a problem only opens a draft issue in your browser; you decide what to submit.")
        ]
    }
    ReadingPanel {
        id: licensesPanel
        objectName: "licensesPanel"
        title: qsTr("Licenses")
        paragraphs: [
            qsTr("DeskPort is free software under the GNU General Public License, version 3 or later. It is derived from Moonlight Qt and includes a modified Sunshine host."),
            qsTr("The shared DeskPort catalog is available under the MIT License. Operating system icons are dedicated to the public domain (CC0 1.0)."),
            qsTr("Qt, FFmpeg, SDL and the other bundled libraries keep their own licenses. Their notices ship with each package."),
            qsTr("Source code and modifications: %1").arg("https://github.com/keithxc/deskport")
        ]
    }
}
