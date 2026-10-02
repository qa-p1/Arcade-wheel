import QtQuick
import QtQuick.Controls

AbstractButton {
    id: control
    property bool accent: false
    property bool destructive: false
    property bool compact: false
    implicitWidth: Math.max(compact ? 32 : 80, label.implicitWidth + (compact ? 20 : 28))
    implicitHeight: compact ? 32 : 39
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.38
    scale: down ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: 80 } }
    background: Rectangle {
        radius: 9
        color: control.accent ? (control.hovered ? "#cbc4ff" : "#b7aff1")
             : control.down ? "#30303f" : control.hovered ? "#272733" : "#202029"
        border.width: 1
        border.color: control.activeFocus ? "#c6bfff" : control.accent ? "transparent" : "#12ffffff"
        Behavior on color { ColorAnimation { duration: 90 } }
    }
    contentItem: Text {
        id: label
        text: control.text
        color: control.accent ? "#191725" : control.destructive ? "#e8a5ad" : "#d9d7e4"
        font.pixelSize: control.compact ? 12 : 13
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
}
