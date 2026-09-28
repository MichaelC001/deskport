import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
// The body of the device settings panel: the common choices first, then the
// advanced streaming settings, which expand in place.
UiPage {
    id: page
    objectName: qsTr("Device settings")
    property var preferences: null
    property string deviceName: ""
    property string deviceId: ""
    property bool changed: false
    property bool advancedOpen: false
    heading: deviceName
    function save() { preferences.save(); changed = true }
    function openAdvanced() {
        advancedOpen = true
        Qt.callLater(function() {
            var flick = page.contentItem
            flick.contentY = Math.max(0, Math.min(advancedRow.mapToItem(flick.contentItem, 0, 0).y - 12, flick.contentHeight - flick.height))
        })
    }
    UiGroup {
        footnote: page.changed ? qsTr("Saved · reconnect to apply changes.") : qsTr("Saved only for this device. Changes apply on the next connection.")
        UiChoiceRow {
            objectName: "deviceDesktopAdjustment"
            iconSource: "qrc:/res/ui/tuning.svg"
            title: qsTr("Desktop fine tuning")
            note: qsTr("0.5 makes controls larger; 1.5 fits more content. Applies after the automatic desktop calculation and takes effect immediately during a connection.")
            readonly property var factors: preferences.desktopAdjustmentChoices
            options: preferences.desktopAdjustmentLabels
            currentIndex: Math.max(0, factors.indexOf(preferences.desktopAdjustment))
            onActivated: function(index) {
                preferences.desktopAdjustment = factors[index]; save()
                if (typeof window !== "undefined" && window.activeHostId === page.deviceId && window.activeStreamPage)
                    window.activeStreamPage.session.setDesktopAdjustment(factors[index])
            }
        }
        UiChoiceRow {
            objectName: "deviceDisplayPolicy"
            iconSource: "qrc:/res/ui/screen.svg"
            title: qsTr("Virtual screen")
            note: qsTr("The previous screen layout is restored automatically when the session ends.")
            options: [
                { label: qsTr("Main + mirror"), detail: qsTr("Primary screen and mirror others (default)") },
                { label: qsTr("Main only"), detail: qsTr("Primary screen and turn off others") },
                { label: qsTr("Extended"), detail: qsTr("Use client as an extended screen") }
            ]
            currentIndex: preferences.displayPolicy
            onActivated: function(index) { preferences.displayPolicy = index; save() }
        }
        UiRow {
            objectName: "deviceFitWindow"
            iconSource: "qrc:/res/ui/fit.svg"
            title: qsTr("Fit to window")
            detail: qsTr("Match the client window resolution")
            switchable: true; on: preferences.adaptiveResolution
            onSwitched: function(value) { preferences.adaptiveResolution = value; save() }
        }
        UiRow {
            objectName: "deviceSound"
            iconSource: "qrc:/res/ui/sound.svg"
            title: qsTr("Sound")
            detail: qsTr("Receive sound from this device")
            switchable: true; on: preferences.remoteAudio
            onSwitched: function(value) { preferences.remoteAudio = value; save() }
        }
        UiRow {
            objectName: "deviceInput"
            iconSource: "qrc:/res/ui/input.svg"
            title: qsTr("Input")
            detail: qsTr("Allow keyboard, pointer and controller input")
            switchable: true; on: preferences.remoteInput; divider: false
            onSwitched: function(value) { preferences.remoteInput = value; save() }
        }
    }
    UiGroup {
        UiRow {
            id: advancedRow
            objectName: "deviceAdvancedButton"
            iconSource: "qrc:/res/ui/picture.svg"
            title: qsTr("Advanced streaming settings")
            expanded: page.advancedOpen; divider: false
            onClicked: page.advancedOpen ? page.advancedOpen = false : page.openAdvanced()
        }
    }
    DeviceAdvanced {
        objectName: "deviceAdvanced"
        visible: page.advancedOpen
        preferences: page.preferences
        onSaved: page.changed = true
    }
}
