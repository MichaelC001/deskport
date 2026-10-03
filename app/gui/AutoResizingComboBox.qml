import QtQuick 2.9
import QtQuick.Controls 2.5

import SdlGamepadKeyNavigation 1.0
import SystemProperties 1.0

// https://stackoverflow.com/questions/45029968/how-do-i-set-the-combobox-width-to-fit-the-largest-item
ComboBox {
    id: comboBox
    property int textWidth
    property int desiredWidth : leftPadding + textWidth + indicator.width + rightPadding
    property int maximumWidth : parent.width

    implicitWidth: desiredWidth < maximumWidth ? desiredWidth : maximumWidth

    TextMetrics {
        id: popupMetrics
    }

    TextMetrics {
        id: textMetrics
    }

    function recalculateWidth() {
        textMetrics.font = font
        popupMetrics.font = popup.font
        textWidth = 0
        for (var i = 0; i < count; i++){
            textMetrics.text = textAt(i)
            popupMetrics.text = textAt(i)
            textWidth = Math.max(textMetrics.width, textWidth)
            textWidth = Math.max(popupMetrics.width, textWidth)
        }
    }

    // We call this every time the options change (and init)
    // so we can adjust the combo box width here too
    onActivated: recalculateWidth()

    // Options open in the shared floating panel style instead of a drop-down.
    popup: Popup {
        parent: Overlay.overlay
        modal: true
        width: Math.min(460, (parent ? parent.width : 492) - 32)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (parent ? parent.height : 640) * 0.82)
        x: Math.round(((parent ? parent.width : width) - width) / 2)
        y: Math.round(((parent ? parent.height : height) - height) / 2)
        padding: 0; topPadding: 12; bottomPadding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: typeof ui !== "undefined" ? ui.scrim : Qt.rgba(0, 0, 0, 0.4) }
        background: Rectangle {
            radius: 20
            color: typeof ui !== "undefined" ? ui.surface : "white"
            border.color: typeof ui !== "undefined" ? ui.line : "#dce2ea"
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            boundsBehavior: Flickable.StopAtBounds
            model: comboBox.popup.visible ? comboBox.delegateModel : null
            currentIndex: comboBox.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        // Switch to normal navigation for combo boxes
        onAboutToShow: SdlGamepadKeyNavigation.setUiNavMode(false)
        onAboutToHide: SdlGamepadKeyNavigation.setUiNavMode(true)
    }
}
