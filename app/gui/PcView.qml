import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

GridView {
    property ComputerModel computerModel : createModel()
    property bool controlCenterForActiveSession: false
    // Edit mode drags cards to reorder them, or drops a device on a device or
    // group to group them, like folders on a phone home screen. The layout is
    // saved locally.
    property bool arranging: false
    // Native macOS resizing delivers a width change for nearly every pixel.
    // Recomputing GridView columns and running displaced transitions for each
    // event makes cards flash between partially completed layouts.
    property bool resizing: false
    onWidthChanged: { resizing = true; resizeSettle.restart() }
    Timer {
        id: resizeSettle
        interval: 70
        onTriggered: { pcGrid.resizing = false }
    }
    // Index of the card a held device would join, or -1.
    property int combineIndex: -1
    // Bumped after group changes so group names and the edit button refresh.
    property int layoutRevision: 0
    readonly property bool inGroup: computerModel.currentGroup !== ""
    readonly property bool canEdit: (layoutRevision, count, inGroup, computerModel.canEdit())
    onCanEditChanged: if (!canEdit) arranging = false
    function refreshLayout() { layoutRevision++ }
    // Layout changes reset the model, which destroys the card that asked for them.
    // Run them after the card's input handler has returned, never inside it.
    function afterInput(change) { Qt.callLater(function() { change(); pcGrid.refreshLayout() }) }
    function openGroup(groupId) {
        folderDialog.openGroup(groupId)
    }
    Keys.onEscapePressed: function(event) {
        if (inGroup) { openGroup(""); event.accepted = true } else event.accepted = false
    }

    function savedPeer(hostId) {
        if (typeof peerManager === "undefined") return null
        var peers = peerManager.peers
        for (var i = 0; i < peers.length; ++i)
            if (peers[i].hostId === hostId && peers[i].role !== "client") return peers[i]
        return null
    }
    property Dialog addressEditor: PeerEditor { id: peerEditor }

    property ComputerModel folderModel: {
        var model = createModel()
        model.objectName = "groupFolderModel"
        return model
    }

    // One device's actions, from the grid or from inside a group. Each keeps the
    // model it came from, so indices always refer to the right list.
    function openAlias(model, index, originalName, alias) {
        renamePcDialog.targetModel = model
        renamePcDialog.pcIndex = index
        renamePcDialog.originalName = originalName
        renamePcDialog.currentAlias = alias || ""
        renamePcDialog.open()
    }
    function confirmRemove(model, index, name) {
        deletePcDialog.targetModel = model
        deletePcDialog.hostId = model.hostIdAt(index)
        deletePcDialog.pcName = name
        deletePcDialog.open()
    }
    function pairWithPin(model, index) {
        var pin = model.generatePinString()
        model.pairComputer(index, pin)
        pairDialog.pin = pin; pairDialog.open()
    }
    function explainActiveSession() {
        showPcDetailsDialog.pcDetails = qsTr("A session with %1 is open. Disconnect it before connecting to another computer.").arg(pcGrid.sessionHostName)
        showPcDetailsDialog.open()
    }
    // What a card's button or a click on it does.
    function activate(row) {
        if (pcGrid.controlCenterForActiveSession && row.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0) {
            recallRemoteSession()
            return true
        }
        // Offline or still checking: troubleshooting lives in the device panel.
        if (!row.online) return false
        if (pcGrid.controlCenterForActiveSession) explainActiveSession()
        else if (!row.serverSupported) {
            errorDialog.text = qsTr("The host on %1 uses an unsupported protocol version. Update the host and DeskPort before connecting.").arg(row.name)
            errorDialog.helpText = ""
            errorDialog.open()
        } else if (row.paired) {
            stackView.push(Qt.resolvedUrl("DesktopSegue.qml"), {"computerIndex": row.sourceIndex, "objectName": row.name})
        } else {
            showAddDevice(row.hostAddress)
        }
        return true
    }

    NavigableDialog {
        id: folderDialog
        objectName: "groupFolderDialog"
        title: (pcGrid.layoutRevision, computerModel.groupName(groupId))
        height: Math.min(maximumHeight, 440)
        padding: 0; topPadding: 0; bottomPadding: 0
        property bool editing: false
        property string groupId: ""
        function openGroup(id) {
            groupId = id
            folderModel.currentGroup = id
            editing = pcGrid.arranging
            open()
        }
        onClosed: {
            editing = false
            folderModel.currentGroup = ""
            computerModel.refreshFavorites()
            pcGrid.refreshLayout()
        }
        header: Item {
            implicitHeight: 58
            Label {
                anchors.left: parent.left; anchors.leftMargin: 20
                anchors.right: folderEdit.left; anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: folderDialog.title; textFormat: Text.PlainText
                color: ui.text; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight
            }
            UiIconButton {
                id: folderEdit
                objectName: "editGroupFolder"
                anchors.right: folderClose.left; anchors.verticalCenter: parent.verticalCenter
                source: folderDialog.editing ? "qrc:/res/done.svg" : "qrc:/res/edit-square.svg"
                iconColor: folderDialog.editing ? ui.accent : ui.text
                Accessible.name: folderDialog.editing ? qsTr("Finish editing") : qsTr("Edit")
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
                onClicked: folderDialog.editing = !folderDialog.editing
            }
            UiIconButton {
                id: folderClose
                objectName: "panelClose"
                anchors.right: parent.right; anchors.rightMargin: 10; anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/res/ui/close.svg"
                Accessible.name: qsTranslate("NavigableDialog", "Close")
                onClicked: folderDialog.close()
            }
        }
        contentItem: Item {
            id: folderPanel
            implicitHeight: 360
            Label {
                visible: folderDialog.editing
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                leftPadding: 20; rightPadding: 20
                text: qsTr("Drag devices to change their order, or out of this panel to leave the group.")
                color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap
            }
            GridView {
                id: folderGrid
                objectName: "groupFolderGrid"
                anchors.fill: parent; anchors.margins: 16; anchors.topMargin: folderDialog.editing ? 40 : 4
                clip: true
                model: folderModel
                cellWidth: width / Math.max(1, Math.floor(width / 130)); cellHeight: 146
                move: Transition { NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutQuad } }
                displaced: Transition { NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutQuad } }
                delegate: ItemDelegate {
                    id: folderItem
                    objectName: "folderDevice-" + model.hostId
                    width: folderGrid.cellWidth - 10; height: folderGrid.cellHeight - 10
                    padding: 8
                    readonly property color statusColor: model.online ? ui.online : model.statusUnknown ? ui.checking : ui.offline
                    readonly property bool washed: model.online || model.statusUnknown
                    background: Rectangle {
                        radius: 16
                        color: folderItem.washed ? ui.mix(ui.surface, folderItem.statusColor, folderItem.hovered || folderDrag.pressed ? 0.16 : 0.09) : folderItem.hovered || folderDrag.pressed ? ui.raised : ui.surface
                        border.color: folderDrag.pressed ? ui.accent : folderItem.washed ? ui.mix(ui.surface, folderItem.statusColor, 0.26) : ui.line
                        border.width: folderDrag.pressed ? 2 : 1
                        Rectangle { x: 10; y: 10; width: 8; height: 8; radius: 4; color: folderItem.statusColor }
                    }
                    Accessible.name: model.name + " · " + (model.online ? qsTr("Online") : model.statusUnknown ? qsTr("Checking…") : qsTr("Offline"))
                    contentItem: Column {
                        spacing: 7
                        Item { width: 1; height: 4 }
                        Image { anchors.horizontalCenter: parent.horizontalCenter; width: 52; height: 52; sourceSize.width: 144; sourceSize.height: 144; source: "qrc:/res/os/" + folderDevicePanel.osKeyFor(model.operatingSystem) + ".svg"; fillMode: Image.PreserveAspectFit }
                        Label { width: parent.width; text: model.name; textFormat: Text.PlainText; color: ui.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Label { width: parent.width; text: model.operatingSystem || qsTr("Computer"); textFormat: Text.PlainText; color: ui.muted; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.pixelSize: 11 }
                    }
                    SequentialAnimation on rotation {
                        running: folderDialog.editing && !folderDrag.pressed
                        loops: Animation.Infinite
                        NumberAnimation { to: -0.7; duration: 130 }
                        NumberAnimation { to: 0.7; duration: 130 }
                    }
                    DevicePanel {
                        id: folderDevicePanel
                        objectName: "folderDevicePanel-" + model.hostId
                        hostId: model.hostId; deviceName: model.name
                        alias: model.alias || ""; reportedName: model.reportedName || ""
                        operatingSystem: model.operatingSystem; address: model.address; details: model.details
                        online: model.online; paired: model.paired; unknown: model.statusUnknown
                        activeSession: pcGrid.sessionHostId.length > 0 && model.hostId === pcGrid.sessionHostId
                        bound: pcGrid.savedPeer(model.hostId) !== null
                        canChangeAddress: !pcGrid.controlCenterForActiveSession && (typeof peerManager !== "undefined" && !peerManager.busy)
                        inGroup: true
                        onSettingsRequested: deviceSettingsDialog.openFor(model.hostId, model.name)
                        onChangeAddressRequested: peerEditor.edit(pcGrid.savedPeer(model.hostId))
                        onAliasRequested: pcGrid.openAlias(folderModel, index, model.reportedName || model.name, model.alias)
                        onPairRequested: pcGrid.pairWithPin(folderModel, index)
                        onRemoveRequested: pcGrid.confirmRemove(folderModel, index, model.name)
                        onMoveOutRequested: { folderModel.moveOutOfGroup(index); computerModel.refreshFavorites(); pcGrid.refreshLayout() }
                        onReconnectRequested: hostManager.reconnectViewer()
                        onFullscreenRequested: hostManager.toggleViewerFullscreen()
                        onDisconnectRequested: hostManager.disconnectViewer()
                    }
                    MouseArea {
                        id: folderDrag
                        anchors.fill: parent
                        preventStealing: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        cursorShape: folderDialog.editing ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
                        onPositionChanged: function(mouse) {
                            if (!pressed || !folderDialog.editing) return
                            var point = mapToItem(folderGrid.contentItem, mouse.x, mouse.y)
                            var target = folderGrid.indexAt(point.x, point.y)
                            if (target >= 0 && target !== index) folderModel.moveComputer(index, target)
                        }
                        onReleased: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                if (!folderDialog.editing) folderDevicePanel.open()
                                return
                            }
                            if (!folderDialog.editing) {
                                if (model.isGroup || model.isAdd) return
                                var row = {hostId: model.hostId, name: model.name, online: model.online, paired: model.paired,
                                           serverSupported: model.serverSupported, sourceIndex: model.sourceIndex, hostAddress: model.hostAddress}
                                if (!model.online) { folderDevicePanel.open(); return }
                                folderDialog.close()
                                pcGrid.activate(row)
                                return
                            }
                            var point = mapToItem(folderPanel, mouse.x, mouse.y)
                            if (point.x < 0 || point.y < 0 || point.x > folderPanel.width || point.y > folderPanel.height) {
                                folderModel.moveOutOfGroup(index)
                                computerModel.refreshFavorites()
                                pcGrid.refreshLayout()
                            }
                        }
                    }
                }
                Label { anchors.centerIn: parent; visible: folderGrid.count === 0; text: qsTr("This group is empty."); color: ui.muted }
            }
        }
    }

    // Device settings float over the device list; advanced streaming settings
    // expand in place inside the same panel.
    NavigableDialog {
        id: deviceSettingsDialog
        objectName: "deviceSettingsDialog"
        title: qsTr("Device settings")
        height: maximumHeight
        padding: 0; topPadding: 0; bottomPadding: 0
        property var devicePreferences: null
        property string deviceName: ""
        property string deviceId: ""
        function openFor(hostId, name) {
            deviceId = hostId
            deviceName = name
            devicePreferences = StreamingPreferences.forDevice(hostId)
            open()
        }
        onClosed: if (devicePreferences) devicePreferences.save()
        contentItem: Loader {
            active: deviceSettingsDialog.devicePreferences !== null && deviceSettingsDialog.visible
            sourceComponent: Component {
                DeviceSettings {
                    preferences: deviceSettingsDialog.devicePreferences
                    deviceName: deviceSettingsDialog.deviceName
                    deviceId: deviceSettingsDialog.deviceId
                }
            }
        }
    }

    id: pcGrid
    focus: true
    activeFocusOnTab: true
    readonly property bool compact: false
    readonly property string sessionHostId: typeof window !== "undefined" ? window.activeHostId : ""
    readonly property string sessionHostName: typeof window !== "undefined" ? window.activeHostName : ""
    boundsBehavior: Flickable.OvershootBounds
    topMargin: 16
    bottomMargin: 5
    // Integer cells avoid rounding a full row into one fewer column.
    readonly property int columns: Math.max(1, Math.floor(width / 235))
    cellWidth: Math.max(1, Math.floor(width / columns))
    cellHeight: 268
    objectName: qsTr("Devices")

    Component.onCompleted: {
        // Don't show any highlighted item until interacting with them.
        // We do this here instead of onActivated to avoid losing the user's
        // selection when backing out of a different page of the app.
        currentIndex = -1
    }

    // Note: Any initialization done here that is critical for streaming must
    // also be done in CliStartStreamSegue.qml, since this code does not run
    // for command-line initiated streams.
    StackView.onActivated: {
        computerModel.refreshFavorites()
        // Setup signals on CM
        ComputerManager.computerAddCompleted.connect(addComplete)

        // This is a bit of a hack to do this here as opposed to main.qml, but
        // we need it enabled before calling getConnectedGamepads() and PcView
        // is never destroyed, so it should be okay.
        SdlGamepadKeyNavigation.enable()

        // Highlight the first item if a gamepad is connected
        if (currentIndex == -1 && SdlGamepadKeyNavigation.getConnectedGamepads() > 0) {
            currentIndex = 0
        }
    }

    StackView.onDeactivating: {
        ComputerManager.computerAddCompleted.disconnect(addComplete)
    }

    function pairingComplete(error)
    {
        // Close the PIN dialog
        pairDialog.close()

        // Display a failed dialog if we got an error
        if (error !== undefined) {
            errorDialog.text = error
            errorDialog.helpText = ""
            errorDialog.open()
        }
    }

    function addComplete(success, detectedPortBlocking)
    {
        if (!success) {
            errorDialog.text = qsTr("Unable to connect to the specified PC.")

            if (detectedPortBlocking) {
                errorDialog.text += "\n\n" + qsTr("This PC's Internet connection is blocking Moonlight. Streaming over the Internet may not work while connected to this network.")
            }
            else {
                errorDialog.helpText = qsTr("Click the Help button for possible solutions.")
            }

            errorDialog.open()
        }
    }

    function createModel()
    {
        var model = Qt.createQmlObject('import ComputerModel 1.0; ComputerModel {}', pcGrid, '')
        model.initialize(ComputerManager)
        model.pairingCompleted.connect(pairingComplete)
        return model
    }

    Label {
        objectName: "emptyGroup"
        anchors.centerIn: parent; width: Math.min(parent.width - 64, 420)
        visible: pcGrid.count === 0 && pcGrid.inGroup
        text: qsTr("This group is empty. In edit mode, drag devices onto the group to add them.")
        color: ui.muted; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
    }

    Column {
        anchors.centerIn: parent; width: Math.min(parent.width - 64, 460); spacing: 16
        visible: pcGrid.count === 0 && !pcGrid.inGroup
        Label { width: parent.width; text: qsTr("Connect your first device."); color: ui.text; font.pixelSize: 28; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
        Label { width: parent.width; text: qsTr("Add a device by IP address or name. Confirm once on the other computer, then connect in either direction."); color: ui.muted; font.pixelSize: 14; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap }
        UiButton { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Add a device"); highlighted: true; onClicked: navigateTo("qrc:/gui/BindView.qml", "BindView") }
        Label { width: parent.width; text: StreamingPreferences.enableMdns ? qsTr("Nearby devices appear here automatically") : qsTr("Nearby discovery is off"); color: ui.muted; font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter }
    }

    header: Item {
        objectName: "deviceHeader"
        width: pcGrid.width - 12
        height: pcGrid.arranging ? arrangeHint.implicitHeight + 16 : 0
        Label {
            id: arrangeHint
            visible: pcGrid.arranging
            width: parent.width; leftPadding: 4
            text: qsTr("Drag cards to change their order, or onto another device to make a group.")
            color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap
        }
    }

    model: computerModel
    move: Transition { enabled: pcGrid.arranging && !pcGrid.resizing; NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad } }
    displaced: Transition { enabled: pcGrid.arranging && !pcGrid.resizing; NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad } }

    function promptNewGroup() { groupNameDialog.ask("", computerModel.defaultGroupName()) }

    delegate: NavigableItemDelegate {
        id: cardDelegate
        objectName: model.isAdd ? "addCard" : model.isGroup ? "group-" + model.groupId : "device-" + model.hostId
        // The add card hides while editing; it never moves.
        opacity: model.isAdd && pcGrid.arranging ? 0 : 1
        enabled: !(model.isAdd && pcGrid.arranging)
        width: pcGrid.cellWidth - 12; height: pcGrid.cellHeight - 12;
        padding: 0
        background: Item {}
        grid: pcGrid
        Accessible.name: model.isAdd ? qsTr("Add a device") : model.name
        // The device panel, or the group panel for a group card.
        function openPanel() {
            if (pcGrid.arranging || model.isAdd) return
            if (model.isGroup) groupPanel.openFor(model.groupId)
            else devicePanel.open()
        }

        contentItem: DeviceCard {
            deviceName: model.name; address: model.address
            arranging: pcGrid.arranging
            held: arrangeArea.pressed
            group: model.isGroup
            addCard: model.isAdd
            onAddDeviceRequested: navigateTo("qrc:/gui/BindView.qml", "BindView")
            onAddGroupRequested: pcGrid.promptNewGroup()
            memberCount: model.memberCount
            memberSystems: model.memberSystems
            dropTarget: pcGrid.combineIndex === index
            operatingSystem: model.operatingSystem
            onDetailsRequested: devicePanel.open()
            activeSession: pcGrid.sessionHostId.length > 0 && model.hostId === pcGrid.sessionHostId
            anotherSession: pcGrid.controlCenterForActiveSession && !activeSession
            onActivateRequested: parent.clicked()
            online: model.online; paired: model.paired; unknown: model.statusUnknown
            selected: parent.hovered || parent.highlighted
        }

        DevicePanel {
            id: devicePanel
            objectName: "devicePanel-" + model.hostId
            hostId: model.hostId; deviceName: model.name
            alias: model.alias || ""; reportedName: model.reportedName || ""
            operatingSystem: model.operatingSystem; address: model.address; details: model.details
            online: model.online; paired: model.paired; unknown: model.statusUnknown
            activeSession: pcGrid.sessionHostId.length > 0 && model.hostId === pcGrid.sessionHostId
            bound: !model.isGroup && !model.isAdd && pcGrid.savedPeer(model.hostId) !== null
            canChangeAddress: !pcGrid.controlCenterForActiveSession && (typeof peerManager !== "undefined" && !peerManager.busy)
            inGroup: false
            onSettingsRequested: deviceSettingsDialog.openFor(model.hostId, model.name)
            onChangeAddressRequested: peerEditor.edit(pcGrid.savedPeer(model.hostId))
            onAliasRequested: pcGrid.openAlias(computerModel, index, model.reportedName || model.name, model.alias)
            onPairRequested: pcGrid.pairWithPin(computerModel, index)
            onRemoveRequested: pcGrid.confirmRemove(computerModel, index, model.name)
            onReconnectRequested: hostManager.reconnectViewer()
            onFullscreenRequested: hostManager.toggleViewerFullscreen()
            onDisconnectRequested: hostManager.disconnectViewer()
        }

        onClicked: {
            if (model.isAdd) return
            if (model.isGroup) {
                pcGrid.openGroup(model.groupId)
                return
            }
            var row = {hostId: model.hostId, name: model.name, online: model.online, paired: model.paired,
                       serverSupported: model.serverSupported, sourceIndex: model.sourceIndex, hostAddress: model.hostAddress}
            // Using open() here because it may be activated by keyboard
            if (!pcGrid.activate(row)) devicePanel.open()
        }

        // While arranging, the whole card is a drag handle: it swaps places with the
        // card under the pointer and never connects or opens its panel.
        MouseArea {
            id: arrangeArea
            objectName: "arrange-" + model.hostId
            anchors.fill: parent
            z: 10
            enabled: pcGrid.arranging && !model.isAdd
            visible: enabled
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            // Opening a group still works while editing.
            onClicked: if (model.isGroup) pcGrid.openGroup(model.groupId)
            onPositionChanged: function(mouse) {
                if (!pressed) return
                var point = mapToItem(pcGrid.contentItem, mouse.x, mouse.y)
                var target = pcGrid.indexAt(point.x, point.y)
                var card = target >= 0 && target !== index ? pcGrid.itemAtIndex(target) : null
                pcGrid.combineIndex = -1
                if (!card) return
                var local = mapToItem(card, mouse.x, mouse.y)
                var fx = local.x / card.width, fy = local.y / card.height
                // Over the middle of another card a device joins it in a group.
                // The cards swap places once the pointer reaches the far side, so
                // passing over a card's near edge never moves it away.
                if (!pcGrid.inGroup && !model.isGroup && fx > 0.22 && fx < 0.78 && fy > 0.22 && fy < 0.78)
                    pcGrid.combineIndex = target
                else if (target < index ? (fx < 0.22 || fy < 0.22) : (fx > 0.78 || fy > 0.78))
                    computerModel.moveComputer(index, target)
            }
            onReleased: {
                var target = pcGrid.combineIndex
                pcGrid.combineIndex = -1
                var from = index, model = pcGrid.computerModel
                if (target >= 0) pcGrid.afterInput(function() { model.combine(from, target) })
            }
            onCanceled: pcGrid.combineIndex = -1
        }

        // Right click, long press and the menu key open the device panel.
        onPressAndHold: openPanel()

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: cardDelegate.openPanel()
        }

        Keys.onMenuPressed: openPanel()

        Keys.onDeletePressed: {
            if (model.isGroup || model.isAdd) return
            pcGrid.confirmRemove(computerModel, index, model.name)
        }
    }

    ErrorMessageDialog {
        id: errorDialog

        // Using Setup-Guide here instead of Troubleshooting because it's likely that users
        // will arrive here by forgetting to enable GameStream or not forwarding ports.
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide"
    }

    NavigableMessageDialog {
        id: pairDialog
        objectName: "pairDialog"
        title: qsTr("Pair with a legacy PIN")

        // Pairing dialog must be modal to prevent double-clicks from triggering
        // pairing twice
        modal: true
        closePolicy: Popup.CloseOnEscape

        // don't allow edits to the rest of the window while open
        property string pin : "0000"
        showSpinner: true
        text:qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin)+"\n\n"+
             qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.")
        standardButtons: Dialog.Cancel
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        objectName: "removeDeviceDialog"
        property var targetModel: null
        property string hostId: ""
        property string pcName : ""
        title: qsTr("Remove device?")
        text: qsTr("Delete '%1', its saved binding and device settings from this computer?").arg(pcName)
        standardButtons: Dialog.Ok | Dialog.Cancel
        destructive: true
        acceptText: qsTr("Remove")

        onAccepted: {
            peerManager.removeDevice(hostId)
        }
    }

    // A group's actions: rename, or delete the group and keep its devices.
    NavigableDialog {
        id: groupPanel
        objectName: "groupPanel"
        property string groupId: ""
        title: (pcGrid.layoutRevision, computerModel.groupName(groupId))
        function openFor(id) { groupId = id; open() }
        padding: 0; topPadding: 0; bottomPadding: 16
        contentItem: ColumnLayout {
            UiGroup {
                Layout.leftMargin: 16; Layout.rightMargin: 16
                footnote: qsTr("Deleting a group keeps its devices; they return to the device list.")
                UiRow {
                    objectName: "openGroup"
                    iconSource: "qrc:/res/ui/group.svg"; title: qsTr("Open group")
                    onClicked: { var id = groupPanel.groupId; groupPanel.close(); pcGrid.openGroup(id) }
                }
                UiRow {
                    objectName: "renameGroup"
                    iconSource: "qrc:/res/ui/edit.svg"; title: qsTr("Rename group")
                    onClicked: { var id = groupPanel.groupId; groupPanel.close(); groupNameDialog.ask(id, computerModel.groupName(id)) }
                }
                UiRow {
                    objectName: "deleteGroup"
                    destructive: true; divider: false
                    iconSource: "qrc:/res/ui/trash.svg"; title: qsTr("Delete group")
                    // Devices are kept: they return to the top level where the group was.
                    onClicked: { var id = groupPanel.groupId; groupPanel.close(); pcGrid.afterInput(function() { computerModel.deleteGroup(id) }) }
                }
            }
        }
    }

    NavigableDialog {
        id: renamePcDialog
        objectName: "renameDeviceDialog"
        property var targetModel: null
        property int pcIndex: -1
        property string originalName: ""
        property string currentAlias: ""
        title: qsTr("Set alias")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { aliasField.text = currentAlias; aliasField.selectAll(); aliasField.forceActiveFocus() }
        onAccepted: targetModel.setAlias(pcIndex, aliasField.text.trim())
        contentItem: ColumnLayout {
            spacing: 8
            TextField {
                id: aliasField
                objectName: "deviceAliasField"
                placeholderText: renamePcDialog.originalName
                maximumLength: 64
                Layout.fillWidth: true
                Keys.onReturnPressed: renamePcDialog.accept()
                Keys.onEnterPressed: renamePcDialog.accept()
            }
            Label {
                text: qsTr("Shown only on this computer. Leave empty to use the original name.")
                color: ui.muted; font.pixelSize: ui.small; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }
    }

    NavigableDialog {
        id: groupNameDialog
        objectName: "groupNameDialog"
        // Empty for a new group.
        property string groupId: ""
        function ask(id, name) { groupId = id; groupNameField.text = name; open() }
        title: groupId ? qsTr("Rename group") : qsTr("New group")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { groupNameField.selectAll(); groupNameField.forceActiveFocus() }
        onAccepted: {
            if (groupId) computerModel.renameGroup(groupId, groupNameField.text)
            else computerModel.addGroup(groupNameField.text)
            pcGrid.refreshLayout()
        }
        contentItem: TextField {
            id: groupNameField
            objectName: "groupNameField"
            maximumLength: 64
            Keys.onReturnPressed: groupNameDialog.accept()
            Keys.onEnterPressed: groupNameDialog.accept()
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        objectName: "deviceDetails"
        property string pcDetails : "";
        text: showPcDetailsDialog.pcDetails
        standardButtons: Dialog.Ok
    }

    ScrollBar.vertical: ScrollBar {}
}
