pragma ComponentBehavior: Bound
import QtQuick
import Arcade

// The full-screen overlay window's content: the dim and the ring at the
// pointer. main.cpp sets `controller` before the view is shown.
Item {
    id: root
    required property WheelController controller
    readonly property var appearance: controller.config.appearance
    readonly property bool active: controller.overlayVisible
    focus: true
    Keys.onEscapePressed: root.controller.cancelTrigger()
    // This view has its own engine: it follows the app theme and Wheel's accent.
    Binding { target: Theme; property: "appId"; value: "arcade.wheel" }
    Binding { target: Theme; property: "mode"; value: root.controller.config.general.theme || "system" }
    Binding { target: Theme; property: "reducedMotion"; value: !!root.appearance.reduceMotion }
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: root.active ? Math.max(0,Math.min(0.5,Number(root.appearance.dim || 0))) : 0
        Behavior on opacity { NumberAnimation { duration: root.appearance.reduceMotion ? 0 : 95 } }
    }
    WheelView {
        id: wheel
        deck: root.controller.currentDeck
        deckIndex: root.controller.currentDeckIndex
        deckCount: root.controller.deckCount
        iconGeneration: root.controller.iconGeneration
        selectedIndex: root.controller.selectedIndex
        appearance: root.appearance
        reduceMotion: !!root.appearance.reduceMotion
        opened: root.controller.overlayRevealed
        hubPressed: root.controller.centerGesturePressed
        x: root.controller.visualCenterX-width/2
        y: root.controller.visualCenterY-height/2
        onCloseFinished: root.controller.finishClose()
    }
    MouseArea {
        property bool centerCapture: false
        anchors.fill: parent
        enabled: root.active
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: root.controller.pointerMoved(mouseX,mouseY)
        onPositionChanged: function(event) { root.controller.pointerMoved(event.x,event.y) }
        onPressed: function(event) {
            if (event.button === Qt.RightButton) { root.controller.cancelTrigger(); return }
            centerCapture = root.controller.centerPressed(event.x, event.y)
        }
        // Let the gesture recognizer receive all press/release pairs, including
        // the second click that MouseArea normally consumes as doubleClicked.
        onDoubleClicked: function(event) { event.accepted = false }
        onReleased: function(event) {
            if (event.button !== Qt.LeftButton) return
            if (centerCapture) root.controller.centerReleased(event.x, event.y)
            else {
                root.controller.pointerMoved(event.x,event.y)
                root.controller.activateSlot(root.controller.selectedIndex)
            }
            centerCapture = false
        }
        onCanceled: { centerCapture = false; root.controller.cancelCenterGesture() }
        onWheel: function(event) {
            const d=event.angleDelta.y || event.pixelDelta.y
            if(d) { root.controller.scrollDeck(d<0 ? 1 : -1); event.accepted=true }
        }
    }
}
