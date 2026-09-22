import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
Rectangle {
    id: card
    property string deviceName: ""
    property string address: ""
    property string operatingSystem: ""
    property bool online: false
    property bool paired: false
    property bool unknown: false
    property bool selected: false
    property bool arranging: false
    property bool held: false
    // A group card: member system icons, name and count; it opens the group.
    property bool group: false
    property int memberCount: 0
    property var memberSystems: []
    // A device held over this card would join it in a group.
    property bool dropTarget: false
    // The last card: add a device, or create a group, each with one click.
    property bool addCard: false
    signal addDeviceRequested()
    signal addGroupRequested()
    property bool activeSession: false
    property bool anotherSession: false
    signal detailsRequested()
    signal moreRequested()
    signal settingsRequested()
    signal activateRequested()
    function osKeyFor(system) {
        var os = (system || "").toLowerCase()
        if (/mac|darwin|osx/.test(os)) return "apple"
        if (/windows/.test(os)) return "windows"
        for (var i = 0, names = ["nixos", "ubuntu", "debian", "fedora", "arch"]; i < names.length; ++i)
            if (os.indexOf(names[i]) >= 0) return names[i]
        return /linux/.test(os) ? "linux" : "computer"
    }
    readonly property string osKey: osKeyFor(operatingSystem)
    readonly property string actionText: group ? qsTr("Open") : activeSession ? qsTr("Return to desktop") : anotherSession ? qsTr("View details") : unknown ? qsTr("Checking…") : !online ? qsTr("Troubleshoot") : paired ? qsTr("Connect") : qsTr("Set up access")
    // Arrange mode: a gentle wiggle marks cards as movable; the held card lifts.
    scale: held ? 1.04 : dropTarget ? 1.06 : 1
    Behavior on scale { NumberAnimation { duration: 120 } }
    onArrangingChanged: if (!arranging) rotation = 0
    onHeldChanged: if (held) rotation = 0
    SequentialAnimation on rotation {
        running: card.arranging && !card.held
        loops: Animation.Infinite
        NumberAnimation { to: -0.6; duration: 130 }
        NumberAnimation { to: 0.6; duration: 130 }
    }
    radius: ui.radius
    color: selected ? ui.raised : ui.surface
    border.color: selected || activeSession || dropTarget ? ui.accent : ui.line
    border.width: selected || dropTarget ? 2 : 1
    ColumnLayout {
        visible: card.addCard
        anchors.fill: parent; anchors.margins: 8; spacing: 0
        Repeater {
            model: [{ key: "device", symbol: "+", label: qsTr("Add a device") }, { key: "group", symbol: "▣", label: qsTr("New group") }]
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                Rectangle { visible: index === 1; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; height: 1; color: ui.line }
                ToolButton {
                    objectName: modelData.key === "device" ? "addDevice" : "addGroup"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Accessible.name: modelData.label
                    onClicked: modelData.key === "device" ? card.addDeviceRequested() : card.addGroupRequested()
                    contentItem: Column {
                        spacing: 4
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.symbol; color: ui.accent; font.pixelSize: 24 }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: ui.accent; font.pixelSize: 14 }
                    }
                }
            }
        }
    }
    ColumnLayout {
        visible: !card.addCard
        anchors.fill: parent; anchors.margins: 16; spacing: 6
        Row {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Item {
                objectName: "cardStatus"
                opacity: card.group ? 0 : 1
                width: header.width / 4; height: 44
                readonly property string statusText: card.activeSession ? qsTr("Connected") : card.unknown ? qsTr("Checking…") : card.online ? qsTr("Online") : qsTr("Offline")
                readonly property color statusColor: card.online || card.activeSession ? "#2FA66A" : card.unknown ? "#D29922" : ui.muted
                Accessible.role: Accessible.StaticText
                Accessible.name: statusText + " · " + card.deviceName
                Column {
                    anchors.centerIn: parent; width: parent.width; spacing: 2
                    Rectangle { width: 6; height: 6; radius: 3; color: parent.parent.statusColor; anchors.horizontalCenter: parent.horizontalCenter }
                    Label { width: parent.width; text: parent.parent.statusText; color: parent.parent.statusColor; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                }
            }
            ToolButton { objectName: "cardDetails"; text: "ⓘ"; opacity: card.group ? 0 : 1; enabled: !card.group; width: header.width / 4; height: 44; Accessible.name: qsTr("Device details") + " · " + card.deviceName; onClicked: card.detailsRequested() }
            ToolButton { objectName: "cardActions"; text: "⋯"; width: header.width / 4; height: 44; Accessible.name: (card.group ? qsTr("Group actions") : qsTr("Device actions")) + " · " + card.deviceName; onClicked: card.moreRequested() }
            ToolButton { objectName: "cardSettings"; text: "⚙"; opacity: card.group ? 0 : 1; enabled: !card.group; width: header.width / 4; height: 44; Accessible.name: qsTr("Device settings") + " · " + card.deviceName; onClicked: card.settingsRequested() }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 55
            Image { visible: !card.group; anchors.centerIn: parent; width: 48; height: 48; sourceSize.width: 144; sourceSize.height: 144; source: "qrc:/res/os/" + card.osKey + ".svg"; fillMode: Image.PreserveAspectFit; Accessible.name: card.operatingSystem }
            Rectangle {
                visible: card.group
                anchors.centerIn: parent; width: 58; height: 58; radius: 14
                color: ui.raised; border.color: ui.line
                Grid {
                    anchors.centerIn: parent; columns: 2; spacing: 4
                    Repeater {
                        model: card.group ? card.memberSystems.slice(0, 4) : []
                        Image { width: 22; height: 22; sourceSize.width: 66; sourceSize.height: 66; source: "qrc:/res/os/" + card.osKeyFor(modelData) + ".svg"; fillMode: Image.PreserveAspectFit }
                    }
                }
            }
        }
        Label { text: card.deviceName; textFormat: Text.PlainText; font.pixelSize: 16; font.weight: Font.DemiBold; color: ui.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; Layout.fillWidth: true }
        Label { text: card.group ? (card.memberCount === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(card.memberCount)) : card.operatingSystem || qsTr("Computer"); textFormat: Text.PlainText; color: ui.muted; font.pixelSize: ui.small; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; Layout.fillWidth: true }
        UiButton {
            text: card.actionText; highlighted: card.activeSession
            enabled: card.group || !card.unknown || card.activeSession || card.anotherSession
            Layout.fillWidth: true; Layout.topMargin: 8
            onClicked: card.activateRequested()
            Accessible.name: text + " · " + card.deviceName
        }
    }
}
