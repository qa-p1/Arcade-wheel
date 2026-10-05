import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property var deck: ({actions: []})
    property int deckIndex: 0
    property int deckCount: 1
    property int selectedIndex: -1
    property var appearance: ({})
    property bool interactive: false
    property bool reduceMotion: false
    property bool opened: true
    property bool hubPressed: false
    property int hoverIndex: -1
    signal slotClicked(int index)
    signal scrollDeck(int delta)
    signal closeFinished()

    function setting(key, fallback) {
        const v = appearance ? appearance[key] : undefined
        return v !== undefined && isFinite(Number(v)) ? Number(v) : fallback
    }
    function duration(ms) { return reduceMotion ? 0 : Math.round(ms / animationSpeed) }
    function iconSource(action) { return action && action.icon ? "image://icons/" + encodeURIComponent(action.icon) : "" }
    function selectedName() {
        const a = actions[effectiveSelection]
        return a ? (a.name || "") + (a.outbound ? " ↗" : "") : ""
    }
    function polar(radius, angle) {
        return Qt.point(width / 2 + radius * Math.cos(angle), height / 2 + radius * Math.sin(angle))
    }
    function xy(p) { return p.x.toFixed(3) + " " + p.y.toFixed(3) }
    // Rounded annular sector. The icon stays upright while the surface follows
    // the ring; each action owns the entire angular region, not its icon bounds.
    function segmentPath(index) {
        const step = Math.PI * 2 / Math.max(1, slotCount)
        const gap = slotGap / ((innerRadius + outerRadius) / 2)
        const a = -Math.PI / 2 + index * step - step / 2 + gap / 2
        const b = a + step - gap
        const r = Math.min(13, (outerRadius - innerRadius) / 4)
        const oi = r / outerRadius, ii = r / innerRadius
        return "M " + xy(polar(outerRadius-r,a))
            + " Q " + xy(polar(outerRadius,a)) + " " + xy(polar(outerRadius,a+oi))
            + " A " + outerRadius + " " + outerRadius + " 0 0 1 " + xy(polar(outerRadius,b-oi))
            + " Q " + xy(polar(outerRadius,b)) + " " + xy(polar(outerRadius-r,b))
            + " L " + xy(polar(innerRadius+r,b))
            + " Q " + xy(polar(innerRadius,b)) + " " + xy(polar(innerRadius,b-ii))
            + " A " + innerRadius + " " + innerRadius + " 0 0 0 " + xy(polar(innerRadius,a+ii))
            + " Q " + xy(polar(innerRadius,a)) + " " + xy(polar(innerRadius+r,a)) + " Z"
    }
    function slotAt(x, y) {
        const dx = (x-width/2) / appearanceScale, dy = (y-height/2) / appearanceScale
        if (Math.hypot(dx,dy) < centerRadius + 10) return -1
        const step = Math.PI*2 / Math.max(1,slotCount)
        return Math.floor(((Math.atan2(dy,dx)+Math.PI/2+step/2+Math.PI*2) % (Math.PI*2))/step)
    }
    function replayEntrance() {
        closeTimer.stop()
        for (let i=0; i<slots.count; ++i) slots.itemAt(i).enter()
    }
    readonly property var actions: deck && deck.actions ? deck.actions : []
    readonly property int slotCount: Math.min(8, actions.length)
    readonly property int effectiveSelection: interactive && hoverIndex >= 0 ? hoverIndex : selectedIndex
    readonly property real centerRadius: Math.max(32, Math.min(72,setting("centerRadius",56)))
    readonly property real ringRadius: Math.max(centerRadius+60,Math.min(260,setting("radius",148)))
    readonly property real cardSize: Math.max(66,Math.min(130,setting("cardSize",96)))
    readonly property real innerRadius: Math.max(centerRadius+16,ringRadius-cardSize*0.68)
    readonly property real outerRadius: ringRadius+cardSize*0.40
    readonly property real iconRadius: (innerRadius+outerRadius)/2
    readonly property real slotGap: Math.max(5,Math.min(24,setting("gap",8)))
    readonly property real appearanceScale: Math.max(0.75,Math.min(1.4,setting("scale",1)))
    readonly property real iconSize: Math.max(24,Math.min(54,setting("iconSize",40)))
    readonly property real animationSpeed: Math.max(0.5,Math.min(2,setting("animationSpeed",1)))
    readonly property real selectedScale: Math.max(1,Math.min(1.12,setting("selectedScale",1.06)))
    readonly property bool labelsEnabled: appearance && appearance.labels === true
    readonly property color accentColor: appearance && appearance.accentColor ? appearance.accentColor : "#b7b7f7"
    readonly property real surfaceOpacity: Math.max(0.45,Math.min(1,setting("opacity",0.94)))
    width: (outerRadius+24)*2
    height: width
    implicitWidth: width
    implicitHeight: height

    property real deckPhase: 1
    property int previousDeck: deckIndex
    property real deckDirection: 1
    onDeckIndexChanged: {
        deckDirection = deckIndex >= previousDeck ? 1 : -1
        previousDeck = deckIndex
        if (!reduceMotion && opened) deckAnimation.restart()
    }
    onOpenedChanged: {
        if (opened) replayEntrance()
        else {
            for (let i=0;i<slots.count;++i) slots.itemAt(i).leave()
            closeTimer.restart()
        }
    }
    onReduceMotionChanged: {
        deckAnimation.stop(); deckPhase=1
        for (let i=0;i<slots.count;++i) slots.itemAt(i).settle()
    }
    Component.onCompleted: if (opened) Qt.callLater(replayEntrance)
    Timer { id: closeTimer; interval: root.duration(95); onTriggered: if (!root.opened) root.closeFinished() }
    NumberAnimation { id: deckAnimation; target: root; property: "deckPhase"; from: 0; to: 1; duration: root.duration(145); easing.type: Easing.OutCubic }

    Item {
        anchors.fill: parent
        scale: root.appearanceScale
        Repeater {
            id: slots
            model: root.slotCount
            delegate: Item {
                id: slot
                required property int index
                property var action: root.actions[index]
                property real progress: 0
                property real highlight: root.effectiveSelection === index ? 1 : 0
                readonly property real angle: -Math.PI/2 + index*Math.PI*2/Math.max(1,root.slotCount)
                readonly property real entranceScale: 0.68 + 0.32*progress
                readonly property var iconPoint: root.polar(root.iconRadius,angle)
                readonly property string path: root.segmentPath(index)
                width: root.width; height: root.height
                x: -Math.cos(angle)*root.iconRadius*entranceScale*(1-progress)
                y: -Math.sin(angle)*root.iconRadius*entranceScale*(1-progress)
                scale: entranceScale*(1+highlight*(root.selectedScale-1))
                opacity: Math.min(1,progress*1.8)*(0.5+0.5*root.deckPhase)
                rotation: (1-root.deckPhase)*root.deckDirection*7
                z: highlight > 0 ? 2 : 1
                function enter() {
                    exitAnimation.stop(); entrance.stop()
                    if (root.reduceMotion) progress=1
                    else { progress=0; entrance.start() }
                }
                function leave() {
                    entrance.stop()
                    if (root.reduceMotion) progress=0
                    else exitAnimation.restart()
                }
                function settle() { entrance.stop(); exitAnimation.stop(); progress=root.opened ? 1 : 0 }
                Behavior on highlight { NumberAnimation { duration: root.duration(105); easing.type: Easing.OutCubic } }
                SequentialAnimation {
                    id: entrance
                    PauseAnimation { duration: root.duration(slot.index*26) }
                    NumberAnimation { target: slot; property: "progress"; to: 1; duration: root.duration(310); easing.type: Easing.OutCubic }
                }
                NumberAnimation { id: exitAnimation; target: slot; property: "progress"; to: 0; duration: root.duration(85); easing.type: Easing.InCubic }
                Shape {
                    y: 5; anchors.fill: parent
                    ShapePath { strokeColor: "transparent"; fillColor: "#30000000"; PathSvg { path: slot.path } }
                }
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeWidth: 1
                        strokeColor: Qt.rgba(root.accentColor.r,root.accentColor.g,root.accentColor.b,0.11+0.52*slot.highlight)
                        fillColor: Qt.rgba(0.085+0.05*slot.highlight,0.087+0.052*slot.highlight,0.115+0.085*slot.highlight,root.surfaceOpacity)
                        PathSvg { path: slot.path }
                    }
                }
                Item {
                    x: slot.iconPoint.x-width/2
                    y: slot.iconPoint.y-height/2-(root.labelsEnabled ? 8 : 0)
                    width: root.iconSize; height: width
                    scale: 1+0.08*slot.highlight
                    opacity: slot.action && slot.action.unavailableReason ? 0.35 : 1
                    Text {
                        anchors.centerIn: parent
                        visible: appIcon.status !== Image.Ready
                        text: slot.action && slot.action.type !== "none" ? (slot.action.name || "A").substring(0,1).toUpperCase() : "+"
                        font.pixelSize: root.iconSize*0.65; font.weight: Font.Medium
                        color: "#c9c9db"
                    }
                    Image {
                        id: appIcon
                        anchors.fill: parent
                        source: root.iconSource(slot.action)
                        sourceSize.width: 96; sourceSize.height: 96
                        asynchronous: false; cache: true
                        fillMode: Image.PreserveAspectFit
                    }
                }
                Text {
                    visible: root.labelsEnabled
                    x: slot.iconPoint.x-width/2; y: slot.iconPoint.y+root.iconSize/2-3
                    width: 106; horizontalAlignment: Text.AlignHCenter
                    text: slot.action ? slot.action.name || "" : ""
                    elide: Text.ElideRight; color: "#c9c8d4"; font.pixelSize: 11
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: root.centerRadius*2+10; height: width; radius: width/2
            color: "#23000000"; opacity: hub.opacity
        }
        Rectangle {
            id: hub
            anchors.centerIn: parent
            width: root.centerRadius*2; height: width; radius: width/2; z: 5
            color: Qt.rgba(0.065,0.068,0.09,root.surfaceOpacity)
            border.color: root.hubPressed ? root.accentColor : "#20d0d0f0"; border.width: 1
            opacity: root.opened ? 1 : 0
            scale: root.opened ? (root.hubPressed ? 0.95 : 1) : 0.85
            Behavior on opacity { NumberAnimation { duration: root.duration(90) } }
            Behavior on scale { NumberAnimation { duration: root.duration(140); easing.type: Easing.OutCubic } }
            Text {
                anchors.fill: parent; anchors.margins: 9
                text: root.selectedName()
                color: "#edecf4"; font.pixelSize: text.length>16 ? 12 : 14; font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap; maximumLineCount: 3; elide: Text.ElideRight
                opacity: root.effectiveSelection >= 0 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: root.duration(65) } }
            }
            Text {
                anchors.top: parent.bottom; anchors.topMargin: 10
                anchors.horizontalCenter: parent.horizontalCenter
                width: 300; color: "#e3aab5"; font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                text: root.effectiveSelection >= 0 && root.actions[root.effectiveSelection] ? root.actions[root.effectiveSelection].unavailableReason || "" : ""
            }
            Row {
                anchors.centerIn: parent; spacing: 6
                opacity: root.effectiveSelection < 0 ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: root.duration(65) } }
                Repeater {
                    model: root.deckCount
                    Rectangle {
                        required property int index
                        width: 5; height: 5; radius: 2.5
                        color: index===root.deckIndex ? root.accentColor : "#4d4e63"
                    }
                }
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: true; cursorShape: root.hoverIndex>=0 ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPositionChanged: function(event) { root.hoverIndex=root.slotAt(event.x,event.y) }
        onExited: root.hoverIndex=-1
        onClicked: function(event) { const i=root.slotAt(event.x,event.y); if(i>=0) root.slotClicked(i) }
        onWheel: function(event) {
            const d=event.angleDelta.y || event.pixelDelta.y
            if(d) { root.scrollDeck(d<0 ? 1 : -1); event.accepted=true }
        }
    }
}
