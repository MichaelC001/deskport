import QtQuick 2.9
import QtQuick.Controls 2.2
Button {
    id: deskButton
    property bool destructive: false
    property var theme: typeof ui !== "undefined" ? ui : fallbackTheme
    UiTheme { id: fallbackTheme }
    implicitHeight: 40
    implicitWidth: Math.max(80, contentItem.implicitWidth + 28)
    leftPadding: 14; rightPadding: 14
    topInset: 0; bottomInset: 0; leftInset: 0; rightInset: 0
    hoverEnabled: true
    background: Rectangle {
        radius: 10
        color: deskButton.highlighted ? deskButton.theme.accent : deskButton.down || deskButton.hovered ? deskButton.theme.raised : deskButton.flat ? "transparent" : deskButton.theme.surface
        border.color: deskButton.activeFocus ? deskButton.theme.accent : deskButton.flat ? "transparent" : deskButton.theme.line
        border.width: deskButton.activeFocus ? 2 : 1
        opacity: deskButton.enabled ? 1 : 0.45
    }
    contentItem: Text {
        text: deskButton.text; font: deskButton.font; color: deskButton.destructive ? deskButton.theme.danger : deskButton.highlighted ? deskButton.theme.accentText : deskButton.theme.text
        opacity: deskButton.enabled ? 1 : 0.45
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
