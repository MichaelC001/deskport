import QtQuick 2.12
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0

NavigableDialog {
    id: dialog
    objectName: "shortcutDialog"
    title: qsTr("Change shortcut")
    property string pending: ""
    property bool invalid: false
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
        if (typeof globalShortcut !== "undefined") globalShortcut.setRecording(true)
    }
    onOpened: recorder.forceActiveFocus()
    onClosed: if (typeof globalShortcut !== "undefined") globalShortcut.setRecording(false)
    Component.onDestruction: if (visible && typeof globalShortcut !== "undefined") globalShortcut.setRecording(false)
    contentItem: FocusScope {
        id: recorder
        objectName: "shortcutRecorder"
        implicitHeight: contents.implicitHeight
        focus: true
        Keys.onShortcutOverride: event.accepted = true
        Keys.onPressed: {
            event.accepted = true
            if (event.isAutoRepeat) return
            if (event.key === Qt.Key_Escape && event.modifiers === Qt.NoModifier) { dialog.reject(); return }
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && event.modifiers === Qt.NoModifier) { dialog.apply(); return }
            if (event.key === Qt.Key_Control || event.key === Qt.Key_Meta || event.key === Qt.Key_Alt || event.key === Qt.Key_Shift) return
            dialog.capture(event.key, event.modifiers)
        }
        Keys.onReleased: event.accepted = true
        ColumnLayout {
            id: contents
            width: parent.width
            spacing: 14
            Label {
                text: qsTr("Press a shortcut. Use Control, Alt/Option or Meta/Command with a letter, number, F1–F12, Space or apostrophe. Shift is optional.")
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
                objectName: "shortcutReset"
                text: qsTr("Restore default")
                focusPolicy: Qt.NoFocus
                onClicked: { dialog.pending = "Meta+'"; dialog.invalid = false; recorder.forceActiveFocus() }
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
                text: qsTranslate("QPlatformTheme", "Cancel")
                focusPolicy: Qt.NoFocus
                onClicked: dialog.reject()
            }
            UiButton {
                objectName: "shortcutApply"
                text: qsTr("Save shortcut")
                enabled: dialog.pending.length > 0
                focusPolicy: Qt.NoFocus; filled: true
                onClicked: dialog.apply()
            }
        }
    }
}
