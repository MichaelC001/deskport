import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import QtQuick.Window 2.12
import StreamingPreferences 1.0

NavigableDialog {
    id: dialog
    objectName: "shortcutDialog"
    title: qsTr("Change shortcut")
    property string pending: ""
    property bool invalid: false
    property bool choosing: false
    property int chosenModifiers: Qt.NoModifier
    property int chosenKey: Qt.Key_A
    readonly property var keyChoices: StreamingPreferences.shortcutKeys()
    function isFocusNavigation(event) {
        return (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) &&
               (event.modifiers === Qt.NoModifier || event.modifiers === Qt.ShiftModifier)
    }
    function updateRecordingWindow() {
        if (typeof globalShortcut !== "undefined")
            globalShortcut.setRecordingWindow(opened && !choosing ? recorder.Window.window : null)
    }
    onChoosingChanged: updateRecordingWindow()
    function chooseSequence(sequence) {
        var parts = StreamingPreferences.shortcutParts(sequence)
        chosenModifiers = parts.modifiers
        chosenKey = parts.key
    }
    function choose() {
        capture(chosenKey, chosenModifiers)
    }
    function capture(key, modifiers) {
        pending = StreamingPreferences.shortcutFromKey(key, modifiers)
        invalid = pending.length === 0
    }
    function apply() {
        if (pending.length === 0) return
        StreamingPreferences.globalShortcutSequence = pending
        StreamingPreferences.save()
        accept()
    }
    onAboutToShow: {
        pending = StreamingPreferences.globalShortcutSequence
        invalid = false
        choosing = false
        chooseSequence(pending)
        if (typeof globalShortcut !== "undefined") globalShortcut.setRecording(true)
    }
    onOpened: { recorder.forceActiveFocus(); updateRecordingWindow() }
    onClosed: if (typeof globalShortcut !== "undefined") { globalShortcut.setRecordingWindow(null); globalShortcut.setRecording(false) }
    Component.onDestruction: if (visible && typeof globalShortcut !== "undefined") { globalShortcut.setRecordingWindow(null); globalShortcut.setRecording(false) }
    contentItem: FocusScope {
        id: recorder
        objectName: "shortcutRecorder"
        implicitHeight: contents.implicitHeight
        focus: true
        Keys.onShortcutOverride: function(event) {
            if (!dialog.choosing && !dialog.isFocusNavigation(event)) event.accepted = true
        }
        Keys.onPressed: function(event) {
            if (dialog.choosing || dialog.isFocusNavigation(event)) { event.accepted = false; return }
            event.accepted = true
            if (event.isAutoRepeat) return
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) { dialog.reject(); return }
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers === Qt.NoModifier) { dialog.apply(); return }
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Meta || event.key === Qt.Key_Alt || event.key === Qt.Key_Shift) return
            dialog.capture(event.key, event.modifiers)
        }
        Keys.onReleased: function(event) {
            if (!dialog.choosing && !dialog.isFocusNavigation(event)) event.accepted = true
        }
        Flickable {
            id: contentScroll
            objectName: "shortcutScroll"
            property Item focusTarget: null
            anchors.fill: parent
            contentHeight: contents.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: ScrollBar {}
            function reveal(item) {
                if (!item) return
                focusTarget = item
                Qt.callLater(function() {
                    if (focusTarget !== item || !item.activeFocus) return
                    var top = item.mapToItem(contents, 0, 0).y
                    if (top < contentY) contentY = top
                    else if (top + item.height > contentY + height) contentY = top + item.height - height
                    contentY = Math.max(0, Math.min(contentY, Math.max(0, contentHeight - height)))
                })
            }
            onHeightChanged: reveal(focusTarget)
            onContentHeightChanged: reveal(focusTarget)
        ColumnLayout {
            id: contents
            width: recorder.width
            spacing: 14
            Label {
                text: qsTr("Press a shortcut, or choose its keys below.")
                textFormat: Text.PlainText; color: dialog.panelTheme.muted
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Label {
                objectName: "shortcutPreview"
                text: dialog.invalid ? qsTr("Choose a supported key combination.") : StreamingPreferences.shortcutDisplay(dialog.pending)
                textFormat: Text.PlainText
                color: dialog.invalid ? dialog.panelTheme.danger : dialog.panelTheme.text
                font.pixelSize: 22; horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap; Layout.fillWidth: true
                Accessible.name: text
            }
            UiButton {
                objectName: "shortcutChooseKeys"
                text: dialog.choosing ? qsTr("Record shortcut") : qsTr("Choose keys")
                focusPolicy: Qt.TabFocus
                onActiveFocusChanged: if (activeFocus) contentScroll.reveal(this)
                onClicked: {
                    if (!dialog.choosing) dialog.chooseSequence(dialog.pending.length > 0 ? dialog.pending : StreamingPreferences.globalShortcutSequence)
                    dialog.choosing = !dialog.choosing
                    if (!dialog.choosing) recorder.forceActiveFocus()
                }
            }
            ColumnLayout {
                visible: dialog.choosing
                Layout.fillWidth: true
                spacing: 8
                Flow {
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: [
                            { label: "Control", mask: Qt.platform.os === "osx" ? Qt.MetaModifier : Qt.ControlModifier },
                            { label: Qt.platform.os === "osx" ? "Option" : "Alt", mask: Qt.AltModifier },
                            { label: Qt.platform.os === "osx" ? "Command" : "Meta", mask: Qt.platform.os === "osx" ? Qt.ControlModifier : Qt.MetaModifier },
                            { label: "Shift", mask: Qt.ShiftModifier }
                        ]
                        CheckBox {
                            objectName: "shortcutModifier" + index
                            text: modelData.label
                            checked: (dialog.chosenModifiers & modelData.mask) !== 0
                            onActiveFocusChanged: if (activeFocus) contentScroll.reveal(this)
                            onToggled: {
                                dialog.chosenModifiers = checked ? dialog.chosenModifiers | modelData.mask : dialog.chosenModifiers & ~modelData.mask
                                dialog.choose()
                            }
                        }
                    }
                }
                ComboBox {
                    objectName: "shortcutPrimaryKey"
                    Layout.fillWidth: true
                    Accessible.name: qsTr("Choose keys")
                    model: dialog.keyChoices
                    textRole: "label"
                    currentIndex: {
                        for (var i = 0; i < dialog.keyChoices.length; ++i)
                            if (dialog.keyChoices[i].key === dialog.chosenKey) return i
                        return -1
                    }
                    onActivated: function(index) { dialog.chosenKey = dialog.keyChoices[index].key; dialog.choose() }
                    onActiveFocusChanged: if (activeFocus) contentScroll.reveal(this)
                }
                Label {
                    text: qsTr("A shortcut needs Control, Alt/Option or Meta/Command.")
                    textFormat: Text.PlainText; color: dialog.panelTheme.muted
                    wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
            Label {
                text: qsTr("Some shortcuts are reserved by the desktop. If recording does not respond, choose the keys below. The desktop may ask you to resolve a conflict when you save.")
                textFormat: Text.PlainText; color: dialog.panelTheme.muted
                wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            UiButton {
                objectName: "shortcutReset"
                text: qsTr("Restore default")
                focusPolicy: Qt.TabFocus
                onActiveFocusChanged: if (activeFocus) contentScroll.reveal(this)
                onClicked: { dialog.pending = "Meta+'"; dialog.chooseSequence(dialog.pending); dialog.invalid = false; if (!dialog.choosing) recorder.forceActiveFocus() }
            }
        }
        }
    }
    footer: Item {
        implicitHeight: buttons.implicitHeight + 24
        RowLayout {
            id: buttons
            anchors.right: parent.right; anchors.rightMargin: 20
            anchors.top: parent.top; anchors.topMargin: 4
            spacing: 8
            UiButton {
                objectName: "shortcutCancel"
                text: qsTranslate("QPlatformTheme", "Cancel")
                focusPolicy: Qt.TabFocus
                onClicked: dialog.reject()
            }
            UiButton {
                objectName: "shortcutApply"
                text: qsTr("Save shortcut")
                enabled: dialog.pending.length > 0
                focusPolicy: Qt.TabFocus; filled: true
                onClicked: dialog.apply()
            }
        }
    }
}
