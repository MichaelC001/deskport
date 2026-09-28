import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// Everything about one device in a floating panel: system icon, name and
// status in the header with device settings behind the gear, then the facts
// and the actions. Each action appears here only.
NavigableDialog {
    id: panel
    property string hostId
    property string deviceName
    property string alias
    property string reportedName
    property string operatingSystem
    property string address
    property string details
    property bool online: false
    property bool paired: false
    property bool unknown: false
    property bool activeSession: false
    property string sessionState: ""
    readonly property string sessionStatus: sessionState === "starting" ? qsTranslate("main", "Connecting…")
        : sessionState === "stopping" ? qsTranslate("main", "Disconnecting…")
        : sessionState === "error" ? qsTranslate("main", "Failed") : ""
    // A DeskPort binding rather than legacy pairing; its address can change.
    property bool bound: false
    property bool canChangeAddress: false
    property bool inGroup: false
    signal settingsRequested()
    signal changeAddressRequested()
    signal aliasRequested()
    signal moveOutRequested()
    signal pairRequested()
    signal removeRequested()
    signal reconnectRequested()
    signal fullscreenRequested()
    signal disconnectRequested()
    // Model changes can destroy the delegate that owns this panel: close first,
    // then act once input handling has returned.
    function run(action) { close(); Qt.callLater(action) }
    function osKeyFor(system) {
        var os = (system || "").toLowerCase()
        if (/mac|darwin|osx/.test(os)) return "apple"
        if (/windows/.test(os)) return "windows"
        for (var i = 0, names = ["nixos", "ubuntu", "debian", "fedora", "arch"]; i < names.length; ++i)
            if (os.indexOf(names[i]) >= 0) return names[i]
        return /linux/.test(os) ? "linux" : "computer"
    }
    readonly property string statusText: sessionStatus.length > 0 ? sessionStatus : activeSession ? qsTr("Connected") : unknown ? qsTr("Checking…")
        : online ? (paired ? qsTr("Online") : qsTr("Online · access not set up")) : qsTr("Offline")
    readonly property color statusColor: online || activeSession ? ui.online : unknown ? ui.checking : ui.muted
    property bool showTechnical: false
    onAboutToShow: showTechnical = false
    padding: 0; topPadding: 0; bottomPadding: 16
    header: Item {
        implicitHeight: 80
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 10; anchors.topMargin: 8
            spacing: 12
            Image {
                source: "qrc:/res/os/" + panel.osKeyFor(panel.operatingSystem) + ".svg"
                sourceSize.width: 120; sourceSize.height: 120
                Layout.preferredWidth: 40; Layout.preferredHeight: 40
                fillMode: Image.PreserveAspectFit
                Accessible.name: panel.operatingSystem
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Label { text: panel.deviceName; textFormat: Text.PlainText; color: ui.text; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { objectName: "panelStatus"; text: panel.statusText; color: panel.statusColor; font.pixelSize: ui.small; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            UiIconButton {
                objectName: "panelDeviceSettings"
                source: "qrc:/res/settings.svg"; iconColor: ui.text
                Accessible.name: qsTr("Device settings")
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
                onClicked: panel.run(function() { panel.settingsRequested() })
            }
            UiIconButton {
                objectName: "panelClose"
                source: "qrc:/res/ui/close.svg"
                Accessible.name: qsTranslate("NavigableDialog", "Close")
                onClicked: panel.close()
            }
        }
    }
    contentItem: ScrollView {
        id: scroller
        clip: true
        implicitHeight: body.implicitHeight
        contentWidth: availableWidth
        ColumnLayout {
            id: body
            width: scroller.availableWidth - 32
            x: 16
            spacing: 16
            UiGroup {
                footnote: !panel.online && !panel.unknown && !panel.activeSession
                          ? qsTr("Make sure DeskPort or the host is running on %1 and that both devices can reach each other. Refresh in the top bar checks again.").arg(panel.deviceName) : ""
                UiRow { visible: panel.alias.length > 0 && panel.reportedName.length > 0; readOnly: true; title: qsTr("Original name"); value: panel.reportedName }
                UiRow { readOnly: true; title: qsTr("System"); value: panel.operatingSystem || qsTr("Unknown") }
                UiRow { readOnly: true; title: qsTr("Connection"); value: panel.bound ? qsTr("DeskPort binding") : panel.paired ? qsTr("Legacy pairing") : qsTr("Not set up") }
                UiRow { readOnly: true; title: qsTr("Address"); value: panel.address || qsTr("Unknown") }
                UiRow {
                    objectName: "panelTechnical"
                    title: qsTr("Technical details"); expanded: panel.showTechnical
                    divider: panel.showTechnical
                    onClicked: panel.showTechnical = !panel.showTechnical
                }
                Label {
                    visible: panel.showTechnical
                    width: parent.width
                    leftPadding: 16; rightPadding: 16; topPadding: 8; bottomPadding: 12
                    text: panel.details; textFormat: Text.PlainText
                    color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WrapAnywhere
                }
            }
            UiGroup {
                visible: panel.activeSession || panel.sessionState === "starting"
                UiRow { objectName: "panel-reconnect-" + panel.hostId; iconSource: "qrc:/res/ui/reconnect.svg"; title: qsTr("Reconnect"); onClicked: panel.run(function() { panel.reconnectRequested() }) }
                UiRow { objectName: "panel-fullscreen-" + panel.hostId; iconSource: "qrc:/res/ui/fullscreen.svg"; title: qsTr("Toggle full screen"); onClicked: panel.run(function() { panel.fullscreenRequested() }) }
                UiRow { objectName: "panel-disconnect-" + panel.hostId; iconSource: "qrc:/res/ui/disconnect.svg"; title: qsTr("Disconnect"); divider: false; onClicked: panel.run(function() { panel.disconnectRequested() }) }
            }
            UiGroup {
                UiRow {
                    objectName: "panel-changeAddress-" + panel.hostId
                    visible: panel.bound
                    enabled: panel.canChangeAddress
                    iconSource: "qrc:/res/ui/address.svg"; title: qsTr("Change address")
                    onClicked: panel.run(function() { panel.changeAddressRequested() })
                }
                UiRow {
                    objectName: "panel-setAlias-" + panel.hostId
                    iconSource: "qrc:/res/ui/edit.svg"; title: qsTr("Set alias")
                    value: panel.alias
                    onClicked: panel.run(function() { panel.aliasRequested() })
                }
                UiRow {
                    objectName: "panel-pair-" + panel.hostId
                    visible: panel.online && !panel.paired
                    iconSource: "qrc:/res/ui/pin.svg"; title: qsTr("Pair with a legacy PIN")
                    detail: qsTr("For Sunshine or other hosts without DeskPort binding.")
                    onClicked: panel.run(function() { panel.pairRequested() })
                }
                UiRow {
                    objectName: "panel-moveOut-" + panel.hostId
                    visible: panel.inGroup
                    iconSource: "qrc:/res/ui/move-out.svg"; title: qsTr("Move out of group")
                    onClicked: panel.run(function() { panel.moveOutRequested() })
                }
                UiRow {
                    objectName: "panel-remove-" + panel.hostId
                    destructive: true; divider: false
                    enabled: !panel.activeSession && !peerManager.busy
                    iconSource: "qrc:/res/ui/trash.svg"; title: qsTr("Remove device")
                    onClicked: panel.run(function() { panel.removeRequested() })
                }
            }
        }
    }
}
