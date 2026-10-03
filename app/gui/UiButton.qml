import QtQuick 2.9
import QtQuick.Controls 2.2
Button {
    id: deskButton
    property bool destructive: false
    // Accent text on the accent tint, for secondary actions.
    property bool tinted: false
    // Solid accent (or red when destructive). Button boxes manage highlighted
    // themselves, so panel buttons set this instead.
    property bool filled: highlighted
    property var theme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    implicitHeight: 40
    implicitWidth: Math.max(80, contentItem.implicitWidth + 28)
    leftPadding: 14; rightPadding: 14
    topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
    hoverEnabled: true
    background: Rectangle {
        radius: 10
        readonly property color fill: deskButton.destructive ? deskButton.theme.danger : deskButton.theme.accent
        color: deskButton.filled ? (deskButton.down || deskButton.hovered ? Qt.darker(fill, 1.08) : fill)
            : deskButton.tinted ? (deskButton.down || deskButton.hovered ? Qt.rgba(deskButton.theme.accent.r, deskButton.theme.accent.g, deskButton.theme.accent.b, 0.24) : deskButton.theme.tint)
            : deskButton.down || deskButton.hovered ? deskButton.theme.raised : deskButton.flat ? "transparent" : deskButton.theme.surface
        border.color: deskButton.activeFocus ? deskButton.theme.accent : deskButton.flat || deskButton.tinted || deskButton.filled ? "transparent" : deskButton.theme.line
        border.width: deskButton.activeFocus ? 2 : 1
        opacity: deskButton.enabled ? 1 : 0.45
    }
    contentItem: Text {
        text: deskButton.text; font.pixelSize: deskButton.font.pixelSize; font.weight: deskButton.filled || deskButton.tinted ? Font.DemiBold : Font.Medium; color: deskButton.filled ? (deskButton.destructive ? (deskButton.theme.luminance(deskButton.theme.danger) > 0.179 ? "#000000" : "#ffffff") : deskButton.theme.accentText) : deskButton.destructive ? deskButton.theme.danger : deskButton.tinted ? deskButton.theme.accent : deskButton.theme.text
        opacity: deskButton.enabled ? 1 : 0.45
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
