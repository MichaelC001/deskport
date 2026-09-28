import QtQuick 2.0
import QtQuick.Controls 2.5

// Every choice, confirmation, message, text entry and wait is this centred,
// rounded panel on a dimmed backdrop: at most 460 wide and 82% of the window
// tall. Clicking outside, Esc, Cancel or the close button closes it. A titled
// panel keeps its title row and close button fixed while its body scrolls.
Dialog {
    id: panel
    readonly property var panelTheme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    // Shows the accepting button in red; it still needs an explicit click.
    property bool destructive: false
    // Replaces the accepting button's text, e.g. "Remove" instead of "Yes".
    property string acceptText: ""
    property bool showClose: title.length > 0
    readonly property real maximumHeight: Math.round((parent ? parent.height : 640) * 0.82)
    modal: true
    padding: 20
    topPadding: title.length > 0 ? 4 : 20
    width: Math.min(460, (parent ? parent.width : 492) - 32)
    height: Math.min(implicitHeight, maximumHeight)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: panel.panelTheme.scrim }
    background: Rectangle { color: panel.panelTheme.surface; radius: 20; border.color: panel.panelTheme.line }
    header: Item {
        visible: panel.title.length > 0
        implicitHeight: visible ? 58 : 0
        Label {
            anchors.left: parent.left; anchors.leftMargin: 20
            anchors.right: closeButton.visible ? closeButton.left : parent.right; anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: panel.title; textFormat: Text.PlainText
            color: panel.panelTheme.text; font.pixelSize: 18; font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        UiIconButton {
            id: closeButton
            objectName: "panelClose"
            visible: panel.showClose
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/res/ui/close.svg"
            Accessible.name: qsTranslate("NavigableDialog", "Close")
            onClicked: panel.reject()
        }
    }
    footer: DialogButtonBox {
        visible: count > 0
        standardButtons: panel.standardButtons
        alignment: Qt.AlignRight
        spacing: 8
        padding: 20; topPadding: 4
        background: Item {}
        delegate: UiButton {
            readonly property bool accepting: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                                              || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            filled: accepting
            destructive: accepting && panel.destructive
        }
    }

    parent: Overlay.overlay

    x: Math.round(((parent ? parent.width : width) - width) / 2)
    y: Math.round(((parent ? parent.height : height) - height) / 2)

    onAboutToShow: {
        if (acceptText.length === 0 || !standardButton) return
        var button = standardButton(Dialog.Ok) || standardButton(Dialog.Yes)
        if (button) button.text = acceptText
    }

    onAboutToHide: {
        // We must force focus back to the last item for platforms without
        // support for more than one active window like Steam Link. If
        // we don't, gamepad and keyboard navigation will break after a
        // dialog appears.
        if (typeof stackView !== "undefined") stackView.forceActiveFocus()
    }
}
