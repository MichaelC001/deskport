import QtQuick 2.9
import QtQuick.Controls.impl 2.12

// The small tinted square that leads a settings row or manual chapter.
Rectangle {
    id: tile
    property url source
    property color iconColor: ui.accent
    implicitWidth: 32; implicitHeight: 32
    radius: 9
    color: ui.tint
    IconImage {
        anchors.centerIn: parent
        source: tile.source; color: tile.iconColor
        sourceSize.width: 18; sourceSize.height: 18
    }
}
