import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3

import ComputerModel 1.0

import ComputerManager 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

CenteredGridView {
    property ComputerModel computerModel : createModel()
    property bool controlCenterForActiveSession: false
    // Edit mode drags cards to reorder them, or drops a device on a device or
    // group to group them, like folders on a phone home screen. The layout is
    // saved locally.
    property bool arranging: false
    // Native macOS resizing delivers a width change for nearly every pixel.
    // Recomputing GridView columns and running displaced transitions for each
    // event makes cards flash between partially completed layouts.
    property real settledWidth: width
    property bool resizing: false
    onWidthChanged: { resizing = true; resizeSettle.restart() }
    Timer {
        id: resizeSettle
        interval: 70
        onTriggered: { pcGrid.settledWidth = pcGrid.width; pcGrid.resizing = false }
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

    Dialog {
        id: folderDialog
        objectName: "groupFolderDialog"
        modal: true
        closePolicy: Popup.CloseOnEscape
        width: Math.min(pcGrid.width - 48, 460)
        height: Math.min(pcGrid.height - 64, 380)
        x: Math.max(24, (pcGrid.width - width) / 2)
        y: Math.max(32, (pcGrid.height - height) / 2)
        padding: 0
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
        header: Rectangle {
            height: 58; color: ui.surface
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: ui.line }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 12
                Label { text: computerModel.groupName(folderDialog.groupId); textFormat: Text.PlainText; color: ui.text; font.pixelSize: ui.title; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                UiButton { objectName: "editGroupFolder"; text: folderDialog.editing ? qsTr("Done") : qsTr("Edit"); highlighted: folderDialog.editing; onClicked: folderDialog.editing = !folderDialog.editing }
                ToolButton { text: "×"; Accessible.name: qsTr("Close"); onClicked: folderDialog.close() }
            }
        }
        contentItem: Rectangle {
            id: folderPanel
            color: ui.canvas
            GridView {
                id: folderGrid
                objectName: "groupFolderGrid"
                anchors.fill: parent; anchors.margins: 18
                clip: true
                model: folderModel
                cellWidth: 126; cellHeight: 132
                move: Transition { NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutQuad } }
                displaced: Transition { NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutQuad } }
                delegate: ItemDelegate {
                    id: folderItem
                    objectName: "folderDevice-" + model.hostId
                    width: folderGrid.cellWidth - 10; height: folderGrid.cellHeight - 10
                    padding: 8
                    function osKeyFor(system) {
                        var os = (system || "").toLowerCase()
                        if (/mac|darwin|osx/.test(os)) return "apple"
                        if (/windows/.test(os)) return "windows"
                        for (var i = 0, names = ["nixos", "ubuntu", "debian", "fedora", "arch"]; i < names.length; ++i)
                            if (os.indexOf(names[i]) >= 0) return names[i]
                        return /linux/.test(os) ? "linux" : "computer"
                    }
                    background: Rectangle { radius: 16; color: folderItem.hovered || folderDrag.pressed ? ui.raised : ui.surface; border.color: folderDrag.pressed ? ui.accent : ui.line; border.width: folderDrag.pressed ? 2 : 1 }
                    contentItem: Column {
                        spacing: 7
                        Image { anchors.horizontalCenter: parent.horizontalCenter; width: 56; height: 56; sourceSize.width: 144; sourceSize.height: 144; source: "qrc:/res/os/" + folderItem.osKeyFor(model.operatingSystem) + ".svg"; fillMode: Image.PreserveAspectFit }
                        Label { width: parent.width; text: model.name; textFormat: Text.PlainText; color: ui.text; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Label { width: parent.width; text: model.online ? qsTr("Online") : qsTr("Offline"); color: model.online ? "#2FA66A" : ui.muted; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 11 }
                    }
                    SequentialAnimation on rotation {
                        running: folderDialog.editing && !folderDrag.pressed
                        loops: Animation.Infinite
                        NumberAnimation { to: -0.7; duration: 130 }
                        NumberAnimation { to: 0.7; duration: 130 }
                    }
                    MouseArea {
                        id: folderDrag
                        anchors.fill: parent
                        preventStealing: true
                        cursorShape: folderDialog.editing ? (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
                        onPositionChanged: function(mouse) {
                            if (!pressed || !folderDialog.editing) return
                            var point = mapToItem(folderGrid.contentItem, mouse.x, mouse.y)
                            var target = folderGrid.indexAt(point.x, point.y)
                            if (target >= 0 && target !== index) folderModel.moveComputer(index, target)
                        }
                        onReleased: function(mouse) {
                            if (!folderDialog.editing) {
                                if (model.isGroup || model.isAdd) return
                                folderDialog.close()
                                if (pcGrid.controlCenterForActiveSession) {
                                    if (model.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0) recallRemoteSession()
                                    else { showPcDetailsDialog.pcDetails = qsTr("A session with %1 is open. Disconnect it before connecting to another computer.").arg(pcGrid.sessionHostName); showPcDetailsDialog.open() }
                                } else if (model.online && model.serverSupported && model.paired) {
                                    stackView.push(Qt.resolvedUrl("DesktopSegue.qml"), {"computerIndex": model.sourceIndex, "objectName": model.name})
                                } else if (model.online) {
                                    navigateTo("qrc:/gui/BindView.qml", "BindView")
                                    stackView.currentItem.setAddress(model.hostAddress)
                                } else {
                                    showPcDetailsDialog.pcDetails = model.details
                                    showPcDetailsDialog.open()
                                }
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

    Dialog {
        id: deviceSettingsDialog
        objectName: "deviceSettingsDialog"
        modal: true
        closePolicy: Popup.CloseOnEscape
        width: Math.min(pcGrid.width - 32, 760)
        height: Math.min(pcGrid.height - 32, 680)
        x: Math.max(16, (pcGrid.width - width) / 2)
        y: Math.max(16, (pcGrid.height - height) / 2)
        padding: 0
        property var devicePreferences: null
        property string deviceName: ""
        property string deviceId: ""
        function openFor(hostId, name) {
            deviceId = hostId
            deviceName = name
            devicePreferences = StreamingPreferences.forDevice(hostId)
            open()
        }
        header: Rectangle {
            height: 56
            color: ui.surface
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: ui.line }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 12
                Label { text: qsTr("Device settings"); color: ui.text; font.pixelSize: ui.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
                UiButton { objectName: "closeDeviceSettings"; text: qsTr("Done"); highlighted: true; onClicked: deviceSettingsDialog.close() }
            }
        }
        contentItem: Item {
            Rectangle { anchors.fill: parent; color: ui.canvas }
            Loader {
                anchors.fill: parent
                active: deviceSettingsDialog.devicePreferences !== null
                sourceComponent: Component {
                    DeviceSettings {
                        preferences: deviceSettingsDialog.devicePreferences
                        deviceName: deviceSettingsDialog.deviceName
                        deviceId: deviceSettingsDialog.deviceId
                        popupMode: true
                        onAdvancedRequested: {
                            var settings = deviceSettingsDialog.devicePreferences
                            var name = deviceSettingsDialog.deviceName
                            deviceSettingsDialog.close()
                            stackView.push(Qt.resolvedUrl("DeviceAdvanced.qml"), {preferences: settings, deviceName: name})
                        }
                    }
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
    minMargin: 0
    topMargin: 16
    bottomMargin: 5
    cellWidth: settledWidth / Math.max(1, Math.floor(settledWidth / 235))
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
        height: pcGrid.inGroup || pcGrid.arranging ? controls.implicitHeight + 24 : 0
        ColumnLayout {
            id: controls; width: parent.width; spacing: ui.gap
            RowLayout {
                visible: pcGrid.inGroup
                Layout.fillWidth: true
                UiButton {
                    objectName: "groupBack"
                    visible: pcGrid.inGroup
                    text: "‹ " + qsTr("All devices")
                    onClicked: pcGrid.openGroup("")
                }
                Label {
                    objectName: "groupTitle"
                    text: (pcGrid.layoutRevision, computerModel.groupName(computerModel.currentGroup))
                    textFormat: Text.PlainText; elide: Text.ElideRight
                    font.pixelSize: ui.heading; font.weight: Font.DemiBold; color: ui.text; Layout.fillWidth: true
                }
                UiButton {
                    id: groupActionsButton
                    objectName: "groupActions"
                    visible: pcGrid.inGroup
                    text: "⋯"
                    Accessible.name: qsTr("Group actions")
                    onClicked: groupMenu.openFor(computerModel.currentGroup, groupActionsButton)
                }
            }
            Label {
                visible: pcGrid.arranging
                text: pcGrid.inGroup ? qsTr("Drag devices to change their order.") : qsTr("Drag cards to change their order, or onto another device to make a group.")
                color: ui.muted; font.pixelSize: ui.small; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }

        }
    }

    model: computerModel
    move: Transition { enabled: !pcGrid.resizing; NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad } }
    displaced: Transition { enabled: !pcGrid.resizing; NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad } }

    function promptNewGroup() { groupNameDialog.ask("", computerModel.defaultGroupName()) }

    delegate: NavigableItemDelegate {
        objectName: model.isAdd ? "addCard" : model.isGroup ? "group-" + model.groupId : "device-" + model.hostId
        // The add card hides while editing; it never moves.
        opacity: model.isAdd && pcGrid.arranging ? 0 : 1
        enabled: !(model.isAdd && pcGrid.arranging)
        width: pcGrid.cellWidth - 12; height: pcGrid.cellHeight - 12;
        padding: 0
        background: Item {}
        grid: pcGrid

        property alias pcContextMenu : pcContextMenuLoader.item

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
            onDetailsRequested: { showPcDetailsDialog.pcDetails = model.details; showPcDetailsDialog.open() }
            onSettingsRequested: deviceSettingsDialog.openFor(model.hostId, model.name)
            activeSession: pcGrid.sessionHostId.length > 0 && model.hostId === pcGrid.sessionHostId
            anotherSession: pcGrid.controlCenterForActiveSession && !activeSession
            onActivateRequested: parent.clicked()
            online: model.online; paired: model.paired; unknown: model.statusUnknown
            selected: parent.hovered || parent.highlighted
            onMoreRequested: {
                if (model.isGroup) groupMenu.openFor(model.groupId, parent)
                else if (pcContextMenuLoader.item) pcContextMenuLoader.item.open()
            }
        }

        Loader {
            id: pcContextMenuLoader
            asynchronous: true
            active: !model.isGroup && !model.isAdd
            sourceComponent: NavigableMenu {
                id: pcContextMenu
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    text: qsTr("Disconnect")
                    visible: model.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0
                    onTriggered: hostManager.disconnectViewer()
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    text: qsTr("Reconnect")
                    visible: model.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0
                    onTriggered: hostManager.reconnectViewer()
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    text: qsTr("Toggle fullscreen")
                    visible: model.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0
                    onTriggered: hostManager.toggleViewerFullscreen()
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    objectName: "moveOut-" + model.hostId
                    text: qsTr("Move out of group")
                    visible: pcGrid.inGroup
                    onTriggered: { var from = index, model = pcGrid.computerModel; pcGrid.afterInput(function() { model.moveOutOfGroup(from) }) }
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    objectName: "moveToFront-" + model.hostId
                    text: qsTr("Move to front")
                    visible: index > 0
                    onTriggered: computerModel.moveComputer(index, 0)
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    objectName: "changeAddress-" + model.hostId
                    text: qsTr("Change address")
                    visible: pcGrid.savedPeer(model.hostId) !== null
                    enabled: !pcGrid.controlCenterForActiveSession && (typeof peerManager !== "undefined" && !peerManager.busy)
                    onTriggered: peerEditor.edit(pcGrid.savedPeer(model.hostId))
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    text: qsTr("Pair with a legacy PIN")
                    visible: model.online && !model.paired
                    onTriggered: {
                        var pin = computerModel.generatePinString()
                        computerModel.pairComputer(index, pin)
                        pairDialog.pin = pin; pairDialog.open()
                    }
                }

                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    objectName: "setAlias-" + model.hostId
                    text: qsTr("Set alias")
                    onTriggered: {
                        renamePcDialog.pcIndex = index
                        renamePcDialog.originalName = model.reportedName
                        renamePcDialog.currentAlias = model.alias
                        renamePcDialog.open()
                    }
                }
                NavigableMenuItem {
                    parentMenu: pcContextMenu
                    text: qsTr("Remove from list")
                    onTriggered: {
                        deletePcDialog.pcIndex = index
                        deletePcDialog.pcName = model.name
                        deletePcDialog.open()
                    }
                }
            }
        }

        onClicked: {
            if (model.isAdd) return
            if (model.isGroup) {
                pcGrid.openGroup(model.groupId)
                return
            }
            if (controlCenterForActiveSession) {
                if (model.hostId === pcGrid.sessionHostId && pcGrid.sessionHostId.length > 0) recallRemoteSession()
                else {
                    showPcDetailsDialog.pcDetails = qsTr("A session with %1 is open. Disconnect it from the tray menu before connecting to another computer.").arg(pcGrid.sessionHostName) + "\n\n" + model.details
                    showPcDetailsDialog.open()
                }
                return
            }
            if (model.online) {
                if (!model.serverSupported) {
                    errorDialog.text = qsTr("The host on %1 uses an unsupported protocol version. Update the host and DeskPort before connecting.").arg(model.name)
                    errorDialog.helpText = ""
                    errorDialog.open()
                }
                else if (model.paired) {
                    stackView.push(Qt.resolvedUrl("DesktopSegue.qml"), {"computerIndex": model.sourceIndex, "objectName": model.name})
                }
                else {
                    navigateTo("qrc:/gui/BindView.qml", "BindView")
                    stackView.currentItem.setAddress(model.hostAddress)

                }
            } else if (!model.online) {
                // Using open() here because it may be activated by keyboard
                pcContextMenu.open()
            }
        }

        // While arranging, the whole card is a drag handle: it swaps places with the
        // card under the pointer and never connects or opens its menu.
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

        onPressAndHold: {
            if (pcGrid.arranging) return
            // popup() ensures the menu appears under the mouse cursor
            if (pcContextMenu.popup) {
                pcContextMenu.popup()
            }
            else {
                // Qt 5.9 doesn't have popup()
                pcContextMenu.open()
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton;
            onClicked: {
                parent.pressAndHold()
            }
        }

        Keys.onMenuPressed: {
            if (pcGrid.arranging) return
            // We must use open() here so the menu is positioned on
            // the ItemDelegate and not where the mouse cursor is
            pcContextMenu.open()
        }

        Keys.onDeletePressed: {
            deletePcDialog.pcIndex = index
            deletePcDialog.pcName = model.name
            deletePcDialog.open()
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

        // Pairing dialog must be modal to prevent double-clicks from triggering
        // pairing twice
        modal: true
        closePolicy: Popup.CloseOnEscape

        // don't allow edits to the rest of the window while open
        property string pin : "0000"
        text:qsTr("Please enter %1 on your host PC. This dialog will close when pairing is completed.").arg(pin)+"\n\n"+
             qsTr("If your host PC is running Sunshine, navigate to the Sunshine web UI to enter the PIN.")
        standardButtons: Dialog.Cancel
        onRejected: {
            // FIXME: We should interrupt pairing here
        }
    }

    NavigableMessageDialog {
        id: deletePcDialog
        // don't allow edits to the rest of the window while open
        property int pcIndex : -1
        property string pcName : ""
        text: qsTr("Are you sure you want to remove '%1'?").arg(pcName)
        standardButtons: Dialog.Yes | Dialog.No

        onAccepted: {
            computerModel.deleteComputer(pcIndex)
        }
    }

    NavigableMenu {
        id: groupMenu
        objectName: "groupMenu"
        property string groupId: ""
        function openFor(id, anchor) {
            groupId = id
            if (anchor) { parent = anchor; x = 0; y = anchor.height }
            open()
        }
        NavigableMenuItem {
            parentMenu: groupMenu
            objectName: "renameGroup"
            text: qsTr("Rename group")
            onTriggered: groupNameDialog.ask(groupMenu.groupId, computerModel.groupName(groupMenu.groupId))
        }
        NavigableMenuItem {
            parentMenu: groupMenu
            objectName: "deleteGroup"
            text: qsTr("Delete group")
            // Devices are kept: they return to the top level where the group was.
            onTriggered: { computerModel.deleteGroup(groupMenu.groupId); pcGrid.refreshLayout() }
        }
    }

    NavigableDialog {
        id: groupNameDialog
        objectName: "groupNameDialog"
        // Empty for a new group.
        property string groupId: ""
        function ask(id, name) { groupId = id; groupNameField.text = name; open() }
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { groupNameField.selectAll(); groupNameField.forceActiveFocus() }
        onAccepted: {
            if (groupId) computerModel.renameGroup(groupId, groupNameField.text)
            else computerModel.addGroup(groupNameField.text)
            pcGrid.refreshLayout()
        }
        ColumnLayout {
            Label { text: groupNameDialog.groupId ? qsTr("Rename group") : qsTr("New group"); font.bold: true }
            TextField {
                id: groupNameField
                objectName: "groupNameField"
                Layout.fillWidth: true
                maximumLength: 64
                Keys.onReturnPressed: groupNameDialog.accept()
                Keys.onEnterPressed: groupNameDialog.accept()
            }
        }
    }

    NavigableMessageDialog {
        id: showPcDetailsDialog
        objectName: "deviceDetails"
        property string pcDetails : "";
        text: showPcDetailsDialog.pcDetails
        imageSrc: "qrc:/res/baseline-help_outline-24px.svg"
        standardButtons: Dialog.Ok
    }

    ScrollBar.vertical: ScrollBar {}
}
