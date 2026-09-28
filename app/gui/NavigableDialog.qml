import QtQuick 2.0
import QtQuick.Controls 2.5

Dialog {
    id: panel
    readonly property var panelTheme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    modal: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { color: panelTheme.surface; radius: 20; border.color: panelTheme.line }
    header: Label {
        visible: panel.title.length > 0
        text: panel.title; textFormat: Text.PlainText
        color: panelTheme.text; font.pixelSize: 18; font.bold: true
        padding: 20; bottomPadding: 4; wrapMode: Text.WordWrap
    }
    footer: DialogButtonBox {
        visible: count > 0
        standardButtons: panel.standardButtons
        background: Item {}
        delegate: UiButton {}
    }

    parent: Overlay.overlay

    x: Math.round(((parent ? parent.width : width) - width) / 2)
    y: Math.round(((parent ? parent.height : height) - height) / 2)

    onAboutToHide: {
        // We must force focus back to the last item for platforms without
        // support for more than one active window like Steam Link. If
        // we don't, gamepad and keyboard navigation will break after a
        // dialog appears.
        if (typeof stackView !== "undefined") stackView.forceActiveFocus()
    }
}
