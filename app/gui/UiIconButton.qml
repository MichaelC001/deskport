import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Controls.impl 2.12

// A round icon-only button, such as a panel's close or settings button.
ToolButton {
    id: iconButton
    property url source
    property var theme: typeof ui !== "undefined" ? ui : fallbackTheme
    property color iconColor: theme.muted
    property int iconSize: 20
    UiTheme { id: fallbackTheme }
    implicitWidth: 36; implicitHeight: 36
    padding: (implicitWidth - iconSize) / 2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    background: Rectangle {
        radius: width / 2
        color: iconButton.down || iconButton.hovered ? iconButton.theme.raised : "transparent"
        border.width: iconButton.activeFocus ? 2 : 0
        border.color: iconButton.theme.accent
    }
    contentItem: IconImage {
        source: iconButton.source; color: iconButton.iconColor
        sourceSize.width: iconButton.iconSize; sourceSize.height: iconButton.iconSize
        fillMode: Image.PreserveAspectFit
    }
}
