import QtQuick

Item {
    id: root
    property var controller
    readonly property var appearance: controller ? controller.config.appearance : ({})
    readonly property bool active: controller ? controller.overlayVisible : false
    focus: true
    Keys.onEscapePressed: if (controller) controller.cancelTrigger()
    Rectangle {
        anchors.fill: parent
        color: "#080810"
        opacity: root.active ? Math.max(0,Math.min(0.5,Number(root.appearance.dim || 0))) : 0
        Behavior on opacity { NumberAnimation { duration: root.appearance.reduceMotion ? 0 : 95 } }
    }
    WheelView {
        id: wheel
        deck: root.controller ? root.controller.currentDeck : ({actions: []})
        deckIndex: root.controller ? root.controller.currentDeckIndex : 0
        deckCount: root.controller ? root.controller.deckCount : 1
        selectedIndex: root.controller ? root.controller.selectedIndex : -1
        appearance: root.appearance
        reduceMotion: !!root.appearance.reduceMotion
        opened: root.controller ? root.controller.overlayRevealed : false
        hubPressed: root.controller ? root.controller.centerGesturePressed : false
        x: (root.controller ? root.controller.visualCenterX : root.width/2)-width/2
        y: (root.controller ? root.controller.visualCenterY : root.height/2)-height/2
        onCloseFinished: if(root.controller) root.controller.finishClose()
    }
    MouseArea {
        property bool centerCapture: false
        anchors.fill: parent
        enabled: root.active
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: if(root.controller) root.controller.pointerMoved(mouseX,mouseY)
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
