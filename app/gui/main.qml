import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import QtQuick.Controls.Material 2.2

import ComputerManager 1.0
import AutoUpdateChecker 1.0
import StreamingPreferences 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

ApplicationWindow {
    property bool pollingActive: false
    readonly property bool navigationVisible: !stackView.currentItem || stackView.currentItem.hidesNavigation !== true

    // Set by SettingsView to force the back operation to pop all
    // pages except the initial view. This is required when doing
    // a retranslate() because AppView breaks for some reason.
    property bool clearOnBack: false

    id: window
    title: "DeskPort"
    onClosing: function(event) {
        event.accepted = false; window.hide();
    }
    width: 1120
    height: 760
    minimumWidth: 640
    minimumHeight: 560
    font.pixelSize: 14
    UiTheme { id: ui; mode: StreamingPreferences.uiTheme; accentMode: StreamingPreferences.uiAccent; systemDark: SystemProperties.systemDark; systemAccent: SystemProperties.systemAccent }
    Material.theme: ui.dark ? Material.Dark : Material.Light
    Material.accent: ui.accent
    Material.primary: ui.surface
    Material.background: ui.canvas
    Material.foreground: ui.text
    color: ui.canvas

    Component.onCompleted: {
        AutoUpdateChecker.start()
        peerManager.restoreHosts()
        if (initialView === "qrc:/gui/PcView.qml") Qt.callLater(function() {
            if (!hostManager.setupComplete && peerManager.peers.length === 0)
                navigateTo("qrc:/gui/SetupView.qml", "SetupView")
            else if (startSharingPage) navigateTo("qrc:/gui/HostView.qml", "HostView")
        })
        if (startInBackground) return
        // Show the window according to the user's preferences
        if (SystemProperties.hasDesktopEnvironment) {
            if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_MAXIMIZED) {
                window.showMaximized()
            }
            else if (StreamingPreferences.uiDisplayMode == StreamingPreferences.UI_FULLSCREEN) {
                window.showFullScreen()
            }
            else {
                window.show()
            }
        } else {
            window.showFullScreen()
        }

        // Display any modal dialogs for configuration warnings
        if (SystemProperties.isWow64) {
            wow64Dialog.open()
        }
        else if (!SystemProperties.hasHardwareAcceleration) {
            if (SystemProperties.isRunningXWayland) {
                xWaylandDialog.open()
            }
            else {
                noHwDecoderDialog.open()
            }
        }

        if (SystemProperties.unmappedGamepads) {
            unmappedGamepadDialog.unmappedGamepads = SystemProperties.unmappedGamepads
            unmappedGamepadDialog.open()
        }
    }
  
    readonly property var activeStreamPage: {
        var count = stackView.depth
        var top = stackView.currentItem // Replacements can preserve depth.
        return stackView.find(function(item) { return item.connectionPending === true || (item.session !== undefined && item.session !== null) })
    }
    readonly property string activeHostId: activeStreamPage && activeStreamPage.session ? activeStreamPage.session.hostId : ""
    readonly property string activeHostName: activeStreamPage && activeStreamPage.session ? activeStreamPage.session.hostName : ""
    function showDevices() {
        if (activeStreamPage) {
            if (activeStreamPage.session !== undefined && activeStreamPage.session) activeStreamPage.session.setViewerRequested(false)
            if (stackView.currentItem.controlCenterForActiveSession === true) return
            if (stackView.currentItem !== activeStreamPage) stackView.pop(activeStreamPage, StackView.Immediate)
            stackView.push(Qt.resolvedUrl("PcView.qml"), {"controlCenterForActiveSession": true}, StackView.Immediate)
        } else stackView.pop(null)
    }
    function showDevicesDuringSession() {
        showDevices()
        if (window.windowState === Qt.WindowMinimized) window.showNormal()
        else window.show()
        window.raise()
        window.requestActivate()
    }
    function prepareViewerRecall() {
        if (!activeStreamPage) return false
        if (activeStreamPage && stackView.currentItem !== activeStreamPage) stackView.pop(activeStreamPage, StackView.Immediate)
        if (activeStreamPage.session !== undefined && activeStreamPage.session) {
            activeStreamPage.session.setViewerRequested(true)
            if (activeStreamPage.session.viewerReady) { window.hide(); return true }
        }
        // Session ownership precedes native-window readiness. Keep the progress
        // page visible until its owner confirms a usable loading/video window.
        if (window.windowState === Qt.WindowMinimized) window.showNormal()
        else window.show()
        window.raise()
        window.requestActivate()
        return true
    }
    function recallRemoteSession() { hostManager.recallViewer() }
    function goBack() {
        if (activeStreamPage && stackView.currentItem.controlCenterForActiveSession === true) {
            recallRemoteSession()
            return
        }
        if (clearOnBack) {
            // Pop all items except the first one
            showDevices()
            clearOnBack = false
        }
        else {
            stackView.pop()
        }
    }

    StackView {
        id: stackView
        initialItem: initialView
        anchors.fill: parent
        anchors.leftMargin: navigationVisible ? 16 : 0
        anchors.rightMargin: navigationVisible ? 16 : 0
        anchors.topMargin: navigationVisible ? topBar.height : 0
        focus: true

        onCurrentItemChanged: {
            // Ensure focus travels to the next view when going back
            if (currentItem) {
                currentItem.forceActiveFocus()
            }
        }

        Keys.onEscapePressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                window.hide()
            }
        }

        Keys.onBackPressed: {
            if (depth > 1) {
                goBack()
            }
            else {
                window.hide()
            }
        }

        Keys.onMenuPressed: {
            navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome")
        }

        // This is a keypress we've reserved for letting the
        // SdlGamepadKeyNavigation object tell us to show settings
        // when Menu is consumed by a focused control.
        Keys.onHangupPressed: {
            navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome")
        }
    }

    // This timer keeps us polling for 5 minutes of inactivity
    // to allow the user to work with Moonlight on a second display
    // while dealing with configuration issues. This will ensure
    // machines come online even if the input focus isn't on Moonlight.
    Timer {
        id: inactivityTimer
        interval: 5 * 60000
        onTriggered: {
            if (!active && pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
    }

    onVisibleChanged: {
        // When we become invisible while streaming is going on,
        // stop polling immediately.
        if (!visible) {
            inactivityTimer.stop()

            if (pollingActive) {
                ComputerManager.stopPollingAsync()
                pollingActive = false
            }
        }
        else if (active) {
            // When we become visible and active again, start polling
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
    }

    onActiveChanged: {
        if (active) {
            // Stop the inactivity timer
            inactivityTimer.stop()

            // Restart polling if it was stopped
            if (!pollingActive) {
                ComputerManager.startPolling()
                pollingActive = true
            }
        }
        else {
            // Start the inactivity timer to stop polling
            // if focus does not return within a few minutes.
            inactivityTimer.restart()
        }
    }

    // Workaround for lack of instanceof in Qt 5.9.
    //
    // Based on https://stackoverflow.com/questions/13923794/how-to-do-a-is-a-typeof-or-instanceof-in-qml
    function qmltypeof(obj, className) { // QtObject, string -> bool
        // className plus "(" is the class instance without modification
        // className plus "_QML" is the class instance with user-defined properties
        if (!obj) return false
        var str = obj.toString();
        return str.startsWith(className + "(") || str.startsWith(className + "_QML");
    }

    function navigateTo(url, objectType)
    {
        if (objectType === "PcView") { showDevices(); return }
        var existingItem = stackView.find(function(item, index) {
            return qmltypeof(item, objectType) && (!activeStreamPage || index > activeStreamPage.StackView.index)
        })

        if (existingItem !== null) {
            // Pop to the existing item
            if (stackView.currentItem !== existingItem) stackView.pop(existingItem, StackView.Immediate)
        }
        else {
            // Create a new item
            stackView.push(url, StackView.Immediate)
        }
    }

    Connections {
        target: peerManager
        function onPeerBound(peer) { ComputerManager.addBoundHost(peer) }
    }
    BindingApproval {
        manager: peerManager
        appWindow: window
    }

    // One top bar replaces the sidebar: brand, version and session traffic on
    // the left; sharing, edit, refresh and settings on the right.
    Rectangle {
        id: topBar
        objectName: "topBar"
        visible: navigationVisible
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 56
        color: ui.surface
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: ui.line }
        readonly property var devicesPage: qmltypeof(stackView.currentItem, "PcView") ? stackView.currentItem : null
        readonly property bool compact: window.width < 900
        // Very narrow windows drop the traffic chip so the right-hand buttons always fit.
        readonly property bool narrow: window.width < 720
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
            ToolButton { objectName: "backButton"; visible: stackView.depth > 1 && topBar.devicesPage === null; text: "←"; Accessible.name: qsTr("Back"); onClicked: goBack() }
            Image {
                source: "qrc:/res/deskport.svg"; fillMode: Image.PreserveAspectFit
                Layout.preferredWidth: 28; Layout.preferredHeight: 28
                sourceSize.width: 84; sourceSize.height: 84
                MouseArea { anchors.fill: parent; onClicked: showDevices() }
            }
            Label {
                text: "DeskPort"; font.pixelSize: 17; font.weight: Font.DemiBold; color: ui.text
                MouseArea { anchors.fill: parent; onClicked: showDevices() }
            }
            UiButton {
                objectName: "versionUpdateButton"
                visible: !topBar.compact || AutoUpdateChecker.status === "available"
                text: topBar.compact ? qsTr("Update available")
                    : "v" + SystemProperties.versionString + (AutoUpdateChecker.status === "available" ? " · " + qsTr("Update available") : "")
                flat: true; highlighted: AutoUpdateChecker.status === "available"
                font.pixelSize: ui.small
                onClicked: {
                    updateDialog.open()
                    if (AutoUpdateChecker.status === "idle" || AutoUpdateChecker.status === "error") AutoUpdateChecker.start()
                }
            }
            UiButton {
                id: trafficSummary; objectName: "trafficSummary"
                visible: !topBar.narrow
                Layout.leftMargin: 4
                flat: true; font.pixelSize: ui.small
                readonly property bool connected: activeHostId.length > 0
                readonly property bool counting: connected && StreamingPreferences.showTraffic && Qt.platform.os !== "windows"
                property real received: 0
                property real sent: 0
                property real downRate: 0
                property real upRate: 0
                property real sampledAt: 0
                property string sampledHost: ""
                function amount(n) { return n >= 1000000000 ? (n / 1000000000).toFixed(2) + " GB" : n >= 1000000 ? (n / 1000000).toFixed(1) + " MB" : (n / 1000).toFixed(1) + " KB" }
                function sample() {
                    if (!activeStreamPage || !activeStreamPage.session) return
                    var sample = activeStreamPage.session.traffic(), now = Date.now()
                    var elapsed = (now - sampledAt) / 1000
                    downRate = sampledHost === activeHostId && sampledAt > 0 && elapsed > 0 ? Math.max(0, sample.received - received) / elapsed : 0
                    upRate = sampledHost === activeHostId && sampledAt > 0 && elapsed > 0 ? Math.max(0, sample.sent - sent) / elapsed : 0
                    received = sample.received; sent = sample.sent; sampledAt = now; sampledHost = activeHostId
                }
                onCountingChanged: { sampledAt = 0; if (counting) sample() }
                // Narrow windows keep the connection and download speed only.
                text: !connected ? "○  " + qsTr("Not connected")
                    : "●  " + activeHostName + (!counting ? "" : topBar.compact
                        ? "  ↓ " + amount(downRate) + "/s"
                        : "  " + amount(received + sent) + "  ↓ " + amount(downRate) + "/s  ↑ " + amount(upRate) + "/s")
                Accessible.name: connected ? qsTr("Session data") + " · " + activeHostName : qsTr("Not connected")
                onClicked: if (connected) trafficDetails.open()
                Timer { interval: 1000; repeat: true; running: trafficSummary.counting && window.visible; onTriggered: trafficSummary.sample() }
            }
            Label {
                visible: stackView.depth > 1 && topBar.devicesPage === null && !topBar.compact
                text: stackView.currentItem ? stackView.currentItem.objectName : ""
                color: ui.muted; elide: Text.ElideRight; Layout.leftMargin: 8
            }
            Item { Layout.fillWidth: true }
            UiButton {
                objectName: "sharingButton"
                text: !hostManager.running ? qsTr("Sharing off") : hostManager.readiness === "attention" ? qsTr("Check permissions") : qsTr("Sharing service on")
                flat: !hostManager.running; highlighted: qmltypeof(stackView.currentItem, "HostView")
                font.pixelSize: ui.small
                onClicked: { showDevices(); navigateTo("qrc:/gui/HostView.qml", "HostView") }
            }
            ToolButton {
                objectName: "arrangeDevices"
                visible: topBar.devicesPage !== null && (topBar.devicesPage.canEdit || topBar.devicesPage.arranging)
                // Square-and-pencil edit icon; a check mark while editing.
                readonly property bool editing: topBar.devicesPage !== null && topBar.devicesPage.arranging
                text: editing ? "✓" : ""
                icon.source: editing ? "" : "qrc:/res/edit-square.svg"
                icon.color: ui.text
                icon.width: 20; icon.height: 20
                font.pixelSize: 18
                Accessible.name: topBar.devicesPage && topBar.devicesPage.arranging ? qsTr("Done") : qsTr("Edit")
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
                onClicked: topBar.devicesPage.arranging = !topBar.devicesPage.arranging
            }
            ToolButton {
                objectName: "refreshDevices"
                visible: topBar.devicesPage !== null
                text: "↻"; font.pixelSize: 18
                Accessible.name: qsTr("Refresh devices")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Check saved devices and look for new ones")
                // Restarting polling checks every saved device again and restarts discovery.
                onClicked: { ComputerManager.stopPollingAsync(); ComputerManager.startPolling() }
            }
            ToolButton {
                objectName: "settingsButton"
                text: "⚙"; font.pixelSize: 18
                Accessible.name: qsTr("Settings")
                ToolTip.visible: hovered; ToolTip.text: qsTr("Settings")
                highlighted: qmltypeof(stackView.currentItem, "SettingsHome")
                onClicked: { showDevices(); navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
            }
        }
    }
    Timer { interval: 21600000; repeat: true; running: true; onTriggered: AutoUpdateChecker.start() }
    Dialog {
        id: updateDialog; objectName: "updateDialog"
        title: qsTr("DeskPort updates")
        modal: true; anchors.centerIn: parent
        width: Math.min(window.width - 40, 560)
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: 12
            Label {
                text: AutoUpdateChecker.status === "checking" ? qsTr("Checking for updates…")
                    : AutoUpdateChecker.status === "available" ? qsTr("New version: %1").arg(AutoUpdateChecker.latestVersion)
                    : AutoUpdateChecker.status === "current" ? qsTr("No newer stable release is available.")
                    : qsTr("Could not check for updates. Please try again.")
                color: ui.text; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label {
                visible: AutoUpdateChecker.publishedAt.length > 0
                text: qsTr("Published: %1").arg(AutoUpdateChecker.publishedAt.slice(0, 10))
                color: ui.muted
            }
            ScrollView {
                visible: AutoUpdateChecker.releaseUrl.length > 0
                Layout.fillWidth: true; Layout.preferredHeight: 230
                clip: true
                TextArea {
                    text: AutoUpdateChecker.releaseNotes || qsTr("No release notes provided.")
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    color: ui.text
                }
            }
            Label {
                text: qsTr("Opens the GitHub release page. Install using your usual method; Nix-managed installations should be updated through Nix.")
                color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            RowLayout {
                UiButton {
                    text: qsTr("Check again")
                    enabled: AutoUpdateChecker.status !== "checking"
                    onClicked: AutoUpdateChecker.start()
                }
                UiButton {
                    text: qsTr("Open download page")
                    enabled: AutoUpdateChecker.releaseUrl.length > 0
                    onClicked: Qt.openUrlExternally(AutoUpdateChecker.releaseUrl)
                }
            }
        }
    }
    Dialog {
        id: trafficDetails; title: qsTr("Session data"); modal: true
        width: Math.min(window.width - 40, 460); anchors.centerIn: parent
        standardButtons: Dialog.Ok
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("Received: %1").arg(trafficSummary.amount(trafficSummary.received)); color: ui.text }
            Label { text: qsTr("Sent: %1").arg(trafficSummary.amount(trafficSummary.sent)); color: ui.text }
            Label { text: qsTr("Counts media, control and clipboard transfer bytes for this session, including temporary reconnects. Excludes IP/VPN overhead, TLS overhead for clipboard, discovery and host-side sharing traffic. This is not your carrier's bill."); color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
    }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.New]; onActivated: navigateTo("qrc:/gui/BindView.qml", "BindView") }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.Preferences]; onActivated: navigateTo("qrc:/gui/SettingsHome.qml", "SettingsHome") }
    Shortcut { enabled: navigationVisible; sequences: [StandardKey.HelpContents]; onActivated: navigateTo("qrc:/gui/SetupView.qml", "SetupView") }

    ErrorMessageDialog {
        id: noHwDecoderDialog
        text: qsTr("No functioning hardware accelerated video decoder was detected by Moonlight. " +
                   "Your streaming performance may be severely degraded in this configuration.")
        helpText: qsTr("Click the Help button for more information on solving this problem.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    ErrorMessageDialog {
        id: xWaylandDialog
        text: qsTr("Hardware acceleration doesn't work on XWayland. Continuing on XWayland may result in poor streaming performance. " +
                   "Try running with QT_QPA_PLATFORM=wayland or switch to X11.")
        helpText: qsTr("Click the Help button for more information.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Fixing-Hardware-Decoding-Problems"
    }

    NavigableMessageDialog {
        id: wow64Dialog
        standardButtons: Dialog.Ok | Dialog.Cancel
        text: qsTr("This version of Moonlight isn't optimized for your PC. Please download the '%1' version of Moonlight for the best streaming performance.").arg(SystemProperties.friendlyNativeArchName)
        onAccepted: {
            Qt.openUrlExternally("https://github.com/keithxc/deskport/releases");
        }
    }

    ErrorMessageDialog {
        id: unmappedGamepadDialog
        property string unmappedGamepads : ""
        text: qsTr("Moonlight detected gamepads without a mapping:") + "\n" + unmappedGamepads
        helpTextSeparator: "\n\n"
        helpText: qsTr("Click the Help button for information on how to map your gamepads.")
        helpUrl: "https://github.com/moonlight-stream/moonlight-docs/wiki/Gamepad-Mapping"
    }

    // This dialog appears when quitting via keyboard or gamepad button
    NavigableMessageDialog {
        id: quitConfirmationDialog
        standardButtons: Dialog.Yes | Dialog.No
        text: qsTr("Are you sure you want to quit?")
        // For keyboard/gamepad navigation
        onAccepted: window.hide()
    }

    // HACK: This belongs in StreamSegue but keeping a dialog around after the parent
    // dies can trigger bugs in Qt 5.12 that cause the app to crash. For now, we will
    // host this dialog in a QML component that is never destroyed.
    //
    // To repro: Start a stream, cut the network connection to trigger the "Connection
    // terminated" dialog, wait until the app grid times out back to the PC grid, then
    // try to dismiss the dialog.
    ErrorMessageDialog {
        id: streamSegueErrorDialog

        property bool quitAfter: false

        onClosed: {
            if (quitAfter) {
                Qt.quit()
            }

            // StreamSegue assumes its dialog will be re-created each time we
            // start streaming, so fake it by wiping out the text each time.
            text = ""
        }
    }

    NavigableDialog {
        id: addPcDialog
        property string label: qsTr("Enter the host IP address or hostname:")

        standardButtons: Dialog.Ok | Dialog.Cancel

        onOpened: {
            // Force keyboard focus on the textbox so keyboard navigation works
            editText.forceActiveFocus()
        }

        onClosed: {
            editText.clear()
        }

        onAccepted: {
            if (editText.text) {
                ComputerManager.addNewHostManually(editText.text.trim())
            }
        }

        ColumnLayout {
            Label {
                text: addPcDialog.label
                font.bold: true
            }

            Label {
                text: qsTr("DeskPort defaults to :48989. If the sharing page shows another port, enter address:port. For a default Sunshine host, use :47989.")
                Layout.preferredWidth: 420
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            TextField {
                id: editText
                Layout.fillWidth: true
                focus: true

                Keys.onReturnPressed: {
                    addPcDialog.accept()
                }

                Keys.onEnterPressed: {
                    addPcDialog.accept()
                }
            }
        }
    }
}
