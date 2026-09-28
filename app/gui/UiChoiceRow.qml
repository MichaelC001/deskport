import QtQuick 2.9

// A settings row that shows the current choice and opens the options in a
// floating panel. activated(index) reports a choice, like a combo box.
UiRow {
    id: choice
    property var options: []
    property int currentIndex: -1
    // Shown instead of the chosen label when set, e.g. a custom value.
    property string displayText
    property string note
    signal activated(int index)
    function shortLabel(index) { var o = options[index]; return o === undefined ? "" : typeof o === "string" ? o : (o.short || o.label) }
    value: displayText.length > 0 ? displayText : currentIndex >= 0 ? shortLabel(currentIndex) : ""
    onClicked: picker.open()
    UiOptionPanel {
        id: picker
        objectName: choice.objectName.length > 0 ? choice.objectName + "Panel" : ""
        title: choice.title
        note: choice.note
        model: choice.options
        currentIndex: choice.currentIndex
        onChosen: function(index) { choice.activated(index) }
    }
}
