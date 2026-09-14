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
    property bool favorite: false
    property bool activeSession: false
    property bool anotherSession: false
    signal moreRequested()
    signal settingsRequested()
    signal activateRequested()
    readonly property string osKey: {
        var os = operatingSystem.toLowerCase()
        if (/mac|darwin|osx/.test(os)) return "apple"
        if (/windows/.test(os)) return "windows"
        for (var i = 0, names = ["nixos", "ubuntu", "debian", "fedora", "arch"]; i < names.length; ++i)
            if (os.indexOf(names[i]) >= 0) return names[i]
        return /linux/.test(os) ? "linux" : "computer"
    }
    readonly property string actionText: activeSession ? qsTr("Return to desktop") : anotherSession ? qsTr("View details") : unknown ? qsTr("Checking…") : !online ? qsTr("Troubleshoot") : paired ? qsTr("Connect") : qsTr("Set up access")
    radius: ui.radius
    color: selected ? ui.raised : ui.surface
    border.color: selected || activeSession ? ui.accent : ui.line
    border.width: selected ? 2 : 1
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 6
        RowLayout {
            Layout.fillWidth: true
            Label { text: card.activeSession ? qsTr("Connected") : card.unknown ? qsTr("Checking…") : card.online ? qsTr("Online") : qsTr("Offline"); color: card.online || card.activeSession ? ui.accent : ui.muted; font.pixelSize: ui.small; Layout.fillWidth: true; elide: Text.ElideRight }
            ToolButton { objectName: "cardSettings"; text: "⚙"; implicitWidth: 30; implicitHeight: 30; Accessible.name: qsTr("Device settings") + " · " + card.deviceName; onClicked: card.settingsRequested() }
            ToolButton { text: "⋯"; implicitWidth: 26; implicitHeight: 30; Accessible.name: qsTr("Device actions") + " · " + card.deviceName; onClicked: card.moreRequested() }
        }
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 55
            Image { anchors.centerIn: parent; width: 48; height: 48; sourceSize.width: 144; sourceSize.height: 144; source: "qrc:/res/os/" + card.osKey + ".svg"; fillMode: Image.PreserveAspectFit; Accessible.name: card.operatingSystem }
        }
        Label { text: (card.favorite ? "★  " : "") + card.deviceName; textFormat: Text.PlainText; font.pixelSize: 16; font.weight: Font.DemiBold; color: ui.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; Layout.fillWidth: true }
        Label { text: card.operatingSystem || qsTr("Computer"); textFormat: Text.PlainText; color: ui.muted; font.pixelSize: ui.small; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; Layout.fillWidth: true }
        UiButton {
            text: card.actionText; highlighted: card.activeSession
            enabled: !card.unknown || card.activeSession || card.anotherSession
            Layout.fillWidth: true; Layout.topMargin: 8
            onClicked: card.activateRequested()
            Accessible.name: text + " · " + card.deviceName
        }
    }
}
