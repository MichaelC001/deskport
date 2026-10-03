import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
ScrollView {
    id: page
    clip: true
    property string heading
    property string description
    default property alias body: bodyColumn.data
    Accessible.name: heading
    contentItem: Flickable {
        contentWidth: width
        contentHeight: pageColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        ColumnLayout {
        id: pageColumn
        width: page.availableWidth
        spacing: 12
        Item { height: 4; Layout.fillWidth: true }
        // The top bar names the page, so the heading is kept for accessibility only.
        Label {
            visible: text.length > 0
            text: page.description; color: ui.muted; font.pixelSize: ui.body; wrapMode: Text.WordWrap
            Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        }
        ColumnLayout { id: bodyColumn; spacing: 16; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
        Item { height: 24; Layout.fillWidth: true }
    }
    }
}
