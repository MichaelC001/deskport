import QtQuick 2.0
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.2

// A message, question or wait in the shared floating panel.
NavigableDialog {
    id: dialog

    property alias text: dialogLabel.dialogText
    property alias showSpinner: dialogSpinner.visible
    // Kept for callers that set it; panels follow the theme instead of an image.
    property url imageSrc

    property string helpText
    property string helpUrl : "https://github.com/moonlight-stream/moonlight-docs/wiki/Troubleshooting"
    property string helpTextSeparator : " "

    onOpened: {
        // Force keyboard focus on the label so keyboard navigation works
        dialogLabel.forceActiveFocus()
    }

    contentItem: RowLayout {
        spacing: 14

        BusyIndicator {
            id: dialogSpinner
            visible: false
            implicitWidth: 36; implicitHeight: 36
        }

        Label {
            property string dialogText

            id: dialogLabel
            text: dialogText + ((helpText && (standardButtons & Dialog.Help)) ? (helpTextSeparator + helpText) : "")
            color: dialog.panelTheme.text
            wrapMode: Text.Wrap
            elide: Label.ElideRight
            Layout.fillWidth: true
            Layout.maximumHeight: 400

            Keys.onReturnPressed: {
                accept()
            }

            Keys.onEnterPressed: {
                accept()
            }

            Keys.onEscapePressed: {
                reject()
            }
        }
    }

    footer: DialogButtonBox {
        id: dialogButtonBox
        visible: count > 0
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        spacing: 8
        padding: 20; topPadding: 4
        background: Item {}
        delegate: UiButton {
            objectName: accepting ? "dialogAcceptButton" : "dialogRejectButton"
            readonly property bool accepting: DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole
                                              || DialogButtonBox.buttonRole === DialogButtonBox.YesRole
            filled: accepting
            destructive: accepting && dialog.destructive
        }

        onHelpRequested: {
            Qt.openUrlExternally(helpUrl)
            close()
        }
    }
}
