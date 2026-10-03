import QtQuick 2.9
import QtQuick.Controls 2.5
import QtQuick.Layouts 1.3

// A rounded card of rows, with an optional caption above and a short footnote
// below.
ColumnLayout {
    id: group
    property string title
    property string footnote
    default property alias rows: rowColumn.data
    Layout.fillWidth: true
    spacing: 6
    Label {
        visible: text.length > 0
        text: group.title; textFormat: Text.PlainText
        color: ui.muted; font.pixelSize: ui.small; font.weight: Font.DemiBold
        leftPadding: 16; Layout.fillWidth: true; elide: Text.ElideRight
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: rowColumn.implicitHeight
        radius: 16
        color: ui.surface
        border.color: ui.line
        Column {
            id: rowColumn
            width: parent.width
            // Rows start and end inside the rounded border.
            topPadding: 2; bottomPadding: 2
        }
    }
    Label {
        visible: text.length > 0
        text: group.footnote; textFormat: Text.PlainText
        color: ui.muted; font.pixelSize: ui.small
        leftPadding: 16; rightPadding: 16
        wrapMode: Text.WordWrap; Layout.fillWidth: true
    }
}
