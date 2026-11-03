import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string tone: "normal"
    implicitHeight: 38
    implicitWidth: Math.max(38, label.implicitWidth + 28)
    leftPadding: 14
    rightPadding: 14
    opacity: enabled ? 1 : 0.4
    font.pixelSize: 13
    font.weight: Font.DemiBold
    contentItem: Text {
        id: label
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.tone === "primary" ? "#171225" : control.tone === "danger" ? "#FFA3A3" : Theme.text
    }
    background: Rectangle {
        radius: 8
        color: control.tone === "primary" ? (control.down ? "#A597EB" : control.hovered ? "#CCBEFF" : Theme.accent) : control.hovered ? "#2A303C" : control.tone === "quiet" ? "transparent" : "#222833"
        border.width: control.visualFocus ? 2 : control.tone === "quiet" || control.tone === "primary" ? 0 : 1
        border.color: control.visualFocus ? Theme.accent : Theme.border
        Behavior on color {
            ColorAnimation {
                duration: 100
            }
        }
    }
}
