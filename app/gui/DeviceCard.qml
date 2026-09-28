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
    readonly property string statusText: activeSession ? qsTr("Connected") : unknown ? qsTr("Checking…") : online ? qsTr("Online") : qsTr("Offline")
    readonly property color statusColor: online || activeSession ? (ui.dark ? "#70d6a1" : "#23875a") : unknown ? (ui.dark ? "#ffc66d" : "#d08a12") : (ui.dark ? "#abb3c2" : "#8a93a3")
    readonly property bool washed: !group && !addCard && (online || activeSession || unknown)
    readonly property color baseColor: ui.dark ? "#1f2430" : ui.surface
    radius: 20
    color: washed ? ui.mix(baseColor, statusColor, selected ? 0.20 : ui.dark ? 0.14 : 0.10) : selected ? ui.raised : baseColor
    border.color: selected || dropTarget ? ui.accent : washed ? ui.mix(baseColor, statusColor, 0.30) : ui.line
    border.width: selected || dropTarget ? 2 : 1
    // Split diagonally from the bottom-left to the top-right corner: adding a
    // device on the upper-left half, a new group on the lower-right half.
    Item {
        id: addHalves
        visible: card.addCard
        anchors.fill: parent
        Canvas {
            anchors.fill: parent
            onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d"); ctx.reset()
                ctx.strokeStyle = ui.line; ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(width * 0.12, height * 0.88); ctx.lineTo(width * 0.88, height * 0.12); ctx.stroke()
            }
        }
        // Either half of the card responds, not only its label.
        MouseArea {
            anchors.fill: parent
            onClicked: function(mouse) {
                if (mouse.x / width + mouse.y / height < 1) card.addDeviceRequested()
                else card.addGroupRequested()
            }
        }
        Repeater {
            model: [{ key: "device", icon: "qrc:/res/add-device.svg", label: qsTr("Add a device") }, { key: "group", icon: "qrc:/res/add-group.svg", label: qsTr("New group") }]
            ToolButton {
                objectName: modelData.key === "device" ? "addDevice" : "addGroup"
                // Near the middle of its triangle, clear of the dividing line.
                x: (index === 0 ? addHalves.width * 0.3 : addHalves.width * 0.7) - width / 2
                y: (index === 0 ? addHalves.height * 0.3 : addHalves.height * 0.7) - height / 2
                Accessible.name: modelData.label
                onClicked: modelData.key === "device" ? card.addDeviceRequested() : card.addGroupRequested()
                display: AbstractButton.TextUnderIcon
                text: modelData.label
                icon.source: modelData.icon
                icon.color: ui.accent
                icon.width: 28; icon.height: 28
                palette.buttonText: ui.accent
                font.pixelSize: 13
            }
        }
    }
    ColumnLayout {
        visible: !card.addCard
        anchors.fill: parent; anchors.margins: 16; spacing: 6
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            Item {
                objectName: "cardStatus"
                visible: !card.group
                width: 22; height: 22
                Accessible.role: Accessible.StaticText
                Accessible.name: card.statusText + " · " + card.deviceName
                Rectangle { anchors.centerIn: parent; width: 22; height: 22; radius: 11; color: card.statusColor; opacity: card.washed ? 0.10 : 0 }
                Rectangle { anchors.centerIn: parent; width: 16; height: 16; radius: 8; color: card.statusColor; opacity: card.washed ? 0.18 : 0 }
                Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 5; color: card.statusColor }
            }
            ToolButton {
                visible: card.group; anchors.right: parent.right; width: 32; height: 24
                objectName: "cardActions"; text: "⋯"; Accessible.name: qsTr("Group actions")
                onClicked: card.moreRequested()
            }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 55
            Image { visible: !card.group; anchors.centerIn: parent; width: 48; height: 48; sourceSize.width: 144; sourceSize.height: 144; source: "qrc:/res/os/" + card.osKey + ".svg"; fillMode: Image.PreserveAspectFit; Accessible.name: card.operatingSystem }
            ToolButton {
                objectName: "cardDetails"
                visible: !card.group; anchors.fill: parent
                background: Item {}
                Accessible.name: qsTr("Device details") + " · " + card.deviceName
                onClicked: card.detailsRequested()
            }
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
            text: card.actionText; highlighted: card.activeSession || (card.online && card.paired && !card.anotherSession)
            enabled: card.group || !card.unknown || card.activeSession || card.anotherSession
            Layout.fillWidth: true; Layout.topMargin: 8
            onClicked: card.activateRequested()
            Accessible.name: text + " · " + card.deviceName
        }
    }
}
