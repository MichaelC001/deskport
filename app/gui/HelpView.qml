import QtQuick 2.9
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.3
import StreamingPreferences 1.0
import Manual 1.0

// The manual that ships inside the application: the shared core file, bundled as
// a resource. Nothing is fetched at runtime, so it always matches this build.
// Chapters are collapsed and open one at a time, like the mobile clients.
UiPage {
    id: page
    objectName: qsTr("Manual")
    heading: qsTr("How to use DeskPort.")
    description: qsTr("Open a chapter to see the steps. This manual ships with the app and is the same on computers and phones.")
    property int openChapter: 0
    // Re-read when the language changes so an open page follows the setting.
    readonly property var chapters: {
        StreamingPreferences.language
        return Manual.chapters(StreamingPreferences.manualLanguage())
    }
    Repeater {
        model: page.chapters
        UiCard {
            id: card
            objectName: "chapter" + index
            readonly property bool open: page.openChapter === index
            // Pane cannot infer the height of a layout whose rows appear and
            // disappear; bind it so an opened chapter grows the card.
            contentHeight: body.implicitHeight
            ColumnLayout {
                id: body
                width: card.availableWidth; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: modelData.title; color: ui.text; font.pixelSize: ui.title
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                    Label { text: card.open ? "\u2304" : "\u203a"; color: ui.muted; font.pixelSize: ui.title }
                }
                ColumnLayout {
                    visible: card.open; spacing: 8; Layout.fillWidth: true
                    Repeater {
                        model: modelData.steps
                        Label {
                            text: (index + 1) + ". " + modelData
                            color: ui.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent; z: -1
                onClicked: page.openChapter = card.open ? -1 : index
            }
        }
    }
}
