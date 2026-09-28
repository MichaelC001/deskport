import QtQuick 2.9
import QtQuick.Controls 2.5

// A few mutually exclusive choices side by side, such as the theme.
Rectangle {
    id: segments
    property var model: []
    property int currentIndex: 0
    signal activated(int index)
    implicitHeight: 36
    implicitWidth: 300
    radius: 10
    color: ui.raised
    Row {
        anchors.fill: parent; anchors.margins: 3
        Repeater {
            model: segments.model
            AbstractButton {
                id: segment
                objectName: "segment" + index
                width: (segments.width - 6) / Math.max(1, segments.model.length)
                height: parent.height
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                readonly property bool current: index === segments.currentIndex
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData
                Accessible.checked: current
                onClicked: if (!current) segments.activated(index)
                background: Rectangle {
                    radius: 8
                    color: segment.current ? ui.surface : segment.hovered ? Qt.rgba(ui.text.r, ui.text.g, ui.text.b, 0.06) : "transparent"
                    border.width: segment.activeFocus ? 2 : segment.current ? 1 : 0
                    border.color: segment.activeFocus ? ui.accent : ui.line
                }
                contentItem: Text {
                    text: modelData
                    color: segment.current ? ui.text : ui.muted
                    font.pixelSize: ui.body; font.weight: segment.current ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
