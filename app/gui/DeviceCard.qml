import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Controls.impl 2.12
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
    property string sessionState: ""
    readonly property string sessionStatus: sessionState === "starting" ? qsTranslate("main", "Connecting…")
        : sessionState === "stopping" ? qsTranslate("main", "Disconnecting…")
        : sessionState === "error" ? qsTranslate("main", "Failed") : ""
    property bool anotherSession: false
    // The system icon opens the device panel; the button does the card's action.
    signal detailsRequested()
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
    // Each card reflects only its own backend session.
    readonly property string actionText: group ? qsTr("Open") : sessionState === "starting" || sessionState === "stopping" ? sessionStatus : activeSession ? qsTr("Return to desktop") : unknown ? qsTr("Checking…") : !online ? qsTr("Troubleshoot") : paired ? qsTr("Connect") : qsTr("Set up access")
    readonly property bool primaryAction: activeSession || (!group && !unknown && online && paired)
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
    readonly property string statusText: sessionStatus.length > 0 ? sessionStatus : activeSession ? qsTr("Connected") : unknown ? qsTr("Checking…") : online ? qsTr("Online") : qsTr("Offline")
    readonly property color statusColor: online || activeSession ? ui.online : unknown ? ui.checking : ui.offline
    // Online and checking devices are washed with their status colour; offline
    // devices, groups and the add card stay plain.
    readonly property bool washed: !group && !addCard && (online || activeSession || unknown)
    radius: 20
    color: addCard ? "transparent" : washed ? ui.mix(ui.surface, statusColor, selected ? (ui.dark ? 0.20 : 0.15) : (ui.dark ? 0.13 : 0.09)) : selected ? ui.raised : ui.surface
    border.color: selected || dropTarget ? ui.accent : addCard ? "transparent" : washed ? ui.mix(ui.surface, statusColor, ui.dark ? 0.32 : 0.26) : ui.line
    border.width: selected || dropTarget ? 2 : 1

    // Dashed outline, a large plus, then the two ways to add.
    Canvas {
        id: dashes
        visible: card.addCard
        anchors.fill: parent
        readonly property color stroke: card.selected ? ui.accent : ui.dark ? Qt.lighter(ui.line, 1.35) : Qt.darker(ui.line, 1.12)
        onStrokeChanged: requestPaint()
        onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d"), r = card.radius, w = width - 2, h = height - 2
            ctx.reset()
            ctx.strokeStyle = stroke; ctx.lineWidth = 1.5
            ctx.setLineDash([6, 5])
            ctx.beginPath()
            ctx.moveTo(1 + r, 1); ctx.lineTo(1 + w - r, 1); ctx.arcTo(1 + w, 1, 1 + w, 1 + r, r)
            ctx.lineTo(1 + w, 1 + h - r); ctx.arcTo(1 + w, 1 + h, 1 + w - r, 1 + h, r)
            ctx.lineTo(1 + r, 1 + h); ctx.arcTo(1, 1 + h, 1, 1 + h - r, r)
            ctx.lineTo(1, 1 + r); ctx.arcTo(1, 1, 1 + r, 1, r)
            ctx.closePath(); ctx.stroke()
        }
    }
    ColumnLayout {
        visible: card.addCard
        anchors.fill: parent; anchors.margins: 16; spacing: 8
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            AbstractButton {
                objectName: "addPlus"
                anchors.centerIn: parent
                width: 64; height: 64
                hoverEnabled: true
                Accessible.name: qsTr("Add a device")
                onClicked: card.addDeviceRequested()
                background: Rectangle { radius: 32; color: parent.hovered ? Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, 0.22) : ui.tint }
                contentItem: IconImage {
                    source: "qrc:/res/ui/plus.svg"; color: ui.accent
                    sourceSize.width: 34; sourceSize.height: 34
                }
            }
        }
        UiButton { objectName: "addDevice"; text: qsTr("Add a device"); tinted: true; Layout.fillWidth: true; onClicked: card.addDeviceRequested() }
        UiButton { objectName: "addGroup"; text: qsTr("New group"); tinted: true; Layout.fillWidth: true; onClicked: card.addGroupRequested() }
    }

    ColumnLayout {
        visible: !card.addCard
        anchors.fill: parent; anchors.margins: 16; spacing: 4
        // A small static light; the status itself is in the accessible name.
        Item {
            objectName: "cardStatus"
            Layout.preferredWidth: 20; Layout.preferredHeight: 20
            Layout.topMargin: -4; Layout.leftMargin: -4
            opacity: card.group ? 0 : 1
            Accessible.role: Accessible.StaticText
            Accessible.name: card.statusText + " · " + card.deviceName
            Rectangle { anchors.centerIn: parent; width: 20; height: 20; radius: 10; color: card.statusColor; opacity: card.washed ? 0.14 : 0 }
            Rectangle { anchors.centerIn: parent; width: 14; height: 14; radius: 7; color: card.statusColor; opacity: card.washed ? 0.24 : 0 }
            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: card.statusColor }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 56
            AbstractButton {
                objectName: "cardDetails"
                visible: !card.group
                anchors.centerIn: parent
                width: 64; height: 64
                hoverEnabled: true
                Accessible.name: qsTr("Device details") + " · " + card.deviceName
                onClicked: card.detailsRequested()
                background: Rectangle { radius: 16; color: parent.hovered && !card.arranging ? Qt.rgba(ui.text.r, ui.text.g, ui.text.b, 0.06) : "transparent" }
                contentItem: Image {
                    source: "qrc:/res/os/" + card.osKey + ".svg"
                    sourceSize.width: 144; sourceSize.height: 144
                    fillMode: Image.PreserveAspectFit
                    Accessible.name: card.operatingSystem
                }
                padding: 8
            }
            Grid {
                visible: card.group
                anchors.centerIn: parent; columns: 2; spacing: 6
                Repeater {
                    model: card.group ? card.memberSystems.slice(0, 4) : []
                    Image { width: 26; height: 26; sourceSize.width: 78; sourceSize.height: 78; source: "qrc:/res/os/" + card.osKeyFor(modelData) + ".svg"; fillMode: Image.PreserveAspectFit }
                }
            }
        }
        Label { text: card.deviceName; textFormat: Text.PlainText; font.pixelSize: 16; font.weight: Font.DemiBold; color: ui.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; maximumLineCount: 1; Layout.fillWidth: true }
        Label { text: card.group ? (card.memberCount === 1 ? qsTr("1 device") : qsTr("%1 devices").arg(card.memberCount)) : card.operatingSystem || qsTr("Computer"); textFormat: Text.PlainText; color: ui.muted; font.pixelSize: ui.small; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; maximumLineCount: 1; Layout.fillWidth: true }
        UiButton {
            objectName: "cardAction"
            text: card.actionText
            highlighted: card.primaryAction
            tinted: !card.primaryAction
            enabled: card.sessionState !== "stopping" && (!card.unknown || card.activeSession || card.sessionState === "starting")
            Layout.fillWidth: true; Layout.topMargin: 10
            onClicked: card.activateRequested()
            Accessible.name: text + " · " + card.deviceName
        }
    }
}
