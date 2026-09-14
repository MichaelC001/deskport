import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import SystemProperties 1.0
UiPage {
    id: page
    objectName: qsTr("Settings")
    heading: qsTr("Make DeskPort your own.")
    description: qsTr("Appearance and preferences for this computer only.")
    readonly property var preferences: StreamingPreferences
    function save() { preferences.save() }
    UiCard {
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: qsTr("Appearance"); color: ui.text; font.pixelSize: ui.title }
            Label { text: qsTr("Theme"); color: ui.muted }
            ComboBox {
                objectName: "themeChoice"; Layout.fillWidth: true
                model: [qsTr("Follow system"), qsTr("Light"), qsTr("Dark")]
                currentIndex: preferences.uiTheme
                onActivated: function(index) { preferences.uiTheme = index; save() }
            }
            Label { text: qsTr("Accent color"); color: ui.muted }
            ComboBox {
                objectName: "accentChoice"; Layout.fillWidth: true
                model: [qsTr("Follow system"), qsTr("Blue"), qsTr("Green"), qsTr("Purple"), qsTr("Orange")]
                currentIndex: preferences.uiAccent
                onActivated: function(index) { preferences.uiAccent = index; save() }
            }
            Label { text: qsTr("Appearance changes apply immediately."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Switch {
                objectName: "showTrafficSwitch"; text: qsTr("Show data usage in the sidebar")
                checked: preferences.showTraffic
                onClicked: { preferences.showTraffic = checked; save() }
            }
        }
    }
    UiCard {
        visible: true
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
        ColumnLayout {
            anchors.fill: parent; spacing: ui.gap
            Label { text: "DeskPort " + SystemProperties.versionString; color: ui.text; font.pixelSize: ui.title }
            UiButton { text: qsTr("Report a problem"); visible: SystemProperties.hasBrowser; onClicked: Qt.openUrlExternally("https://github.com/keithxc/deskport/issues") }
        }
    }
}
