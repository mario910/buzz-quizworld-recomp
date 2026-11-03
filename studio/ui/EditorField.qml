import QtQuick
import QtQuick.Controls

TextField {
    id: control
    implicitHeight: 38
    color: Theme.text
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accentDark
    selectedTextColor: Theme.text
    font.pixelSize: 13
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    background: Rectangle {
        radius: 7
        color: Theme.input
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
    }
}
