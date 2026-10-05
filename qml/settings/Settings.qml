import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import "../wheel"

Rectangle {
    id: root
    color: "#101015"
    width: 1240; height: 820
    Material.theme: Material.Dark
    Material.accent: "#b7aff1"
    Material.foreground: "#e5e2ed"
    Material.background: "#1b1b24"
    property string currentPage: "Wheel"
    property int selectedSlot: -1
    property var activeDeck: controller.currentDeck
    property var activeActions: activeDeck && activeDeck.actions ? activeDeck.actions : []
    property var activeAction: selectedSlot >= 0 && selectedSlot < activeActions.length ? activeActions[selectedSlot] : ({})
    property var pages: ["Wheel", "Center gestures", "Actions", "Trigger", "Appearance", "Behaviour", "Integrations", "General"]
    readonly property bool triggerReady: controller.triggerStatus.indexOf("Ready") === 0
    function scrollPreviewDeck(delta) {
        if (controller.deckCount < 2) return
        const b = controller.config.behaviour
        let next = controller.currentDeckIndex + (b.scrollReverse ? -delta : delta)
        next = b.wrapDecks ? (next+controller.deckCount)%controller.deckCount : Math.max(0,Math.min(controller.deckCount-1,next))
        controller.selectDeck(next)
    }
    readonly property bool linkPending: Object.keys(controller.linkDraft).length > 0
    Shortcut { sequence: StandardKey.Close; onActivated: root.Window.window.close() }
    Connections {
        target: controller
        function onCurrentDeckChanged() { if(root.selectedSlot >= root.activeActions.length) root.selectedSlot=-1 }
        function onLinkDraftChanged() { if (root.linkPending) root.currentPage = "Wheel" }
    }
    ActionPicker { id: actionPicker }
    // Another Arcade app asked to add an action. Nothing is saved until the
    // user chooses a slot and saves it in the picker.
    Rectangle {
        id: linkBanner
        visible: root.linkPending
        z: 10
        anchors.top: parent.top; anchors.topMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 240, 760); height: 58; radius: 12
        color: "#2a2338"; border.color: "#514462"; border.width: 1
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 10; spacing: 10
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#e9e1f5"; font.pixelSize: 12
                text: (controller.linkSource || "Another Arcade app") + " wants to add “" + (controller.linkDraft.name || "an action") + "” to the Wheel. Select a slot, then place it."
            }
            UiButton {
                text: "Place in selected slot"; accent: true; compact: true
                enabled: root.selectedSlot >= 0
                onClicked: actionPicker.editLinkAction(root.activeDeck.id, root.activeDeck.name || "the Wheel", root.selectedSlot, controller.linkDraft)
            }
            UiButton { text: "Cancel"; compact: true; onClicked: controller.finishLinkAction(false) }
        }
    }
    FileDialog {
        id: importDialog; title: "Import configuration"; nameFilters: ["JSON configuration (*.json)"]
        onAccepted: controller.importConfig(selectedFile.toString())
    }
    FileDialog {
        id: exportDialog; title: "Export configuration"; fileMode: FileDialog.SaveFile
        defaultSuffix: "json"; nameFilters: ["JSON configuration (*.json)"]
        onAccepted: controller.exportConfig(selectedFile.toString())
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.preferredWidth: 196; Layout.fillHeight: true
            color: "#14141b"
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#0cffffff" }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 18; spacing: 5
                RowLayout {
                    Layout.topMargin: 16; Layout.bottomMargin: 36; spacing: 10
                    Image { source: "qrc:/assets/arcade-wheel.png"; Layout.preferredWidth: 30; Layout.preferredHeight: 30; sourceSize: Qt.size(60,60) }
                    Column {
                        spacing: 2
                        Text { text: "Arcade Wheel"; color: "#eeedf4"; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { text: "Your desktop, closer."; color: "#757382"; font.pixelSize: 10 }
                    }
                }
                Text { text: "WORKSPACE"; color: "#686675"; font.pixelSize: 9; font.letterSpacing: 1.4; Layout.leftMargin: 12; Layout.bottomMargin: 8 }
                Repeater {
                    model: root.pages
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true; height: 41; radius: 9
                        color: root.currentPage===modelData ? "#292637" : navHover.containsMouse ? "#1d1c26" : "transparent"
                        Behavior on color { ColorAnimation { duration: 90 } }
                        Text { anchors.left: parent.left; anchors.leftMargin: 13; anchors.verticalCenter: parent.verticalCenter; text: modelData; font.pixelSize: 13; color: root.currentPage===modelData ? "#d9d1ff" : "#92909f"; font.weight: root.currentPage===modelData ? Font.Medium : Font.Normal }
                        MouseArea { id: navHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPage=modelData }
                    }
                }
                Item { Layout.fillHeight: true }
                Rectangle {
                    Layout.fillWidth: true; height: 86; radius: 11; color: "#1b1b24"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 13; spacing: 8
                        RowLayout {
                            spacing: 7
                            Rectangle { width: 5; height: 5; radius: 3; color: root.triggerReady ? "#93c9b3" : "#d9bc83" }
                            Text { text: root.triggerReady ? "Ready in background" : "Shortcut needs attention"; color: "#aaa7b7"; font.pixelSize: 10 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Hold to open"; color: "#777484"; font.pixelSize: 10; Layout.fillWidth: true }
                            Rectangle {
                                implicitWidth: shortcutLabel.implicitWidth+14; height: 23; radius: 5; color: "#292833"; border.color: "#35333f"
                                Text { id: shortcutLabel; anchors.centerIn: parent; text: controller.config.trigger.shortcut; font.pixelSize: 10; color: "#c1bace" }
                            }
                        }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.currentPage="Trigger" }
                }
                Text { text: "ARCADE WHEEL  /  0.2"; color: "#555260"; font.pixelSize: 9; font.letterSpacing: 0.8; Layout.topMargin: 9; Layout.leftMargin: 3 }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 30; Layout.rightMargin: 30; Layout.topMargin: 28; Layout.bottomMargin: 23
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 5
                    Text { text: root.currentPage==="Wheel" ? "Your wheel" : root.currentPage; color: "#f1eff7"; font.pixelSize: 27; font.weight: Font.DemiBold }
                    Text { text: root.currentPage==="Wheel" ? "Arrange your actions. Keep them within reach." : "Make Arcade Wheel work the way you do."; color: "#807c90"; font.pixelSize: 12 }
                }
                UiButton { text: "Open wheel"; accent: true; onClicked: controller.showWheel() }
            }
            Rectangle {
                visible: controller.lastError.length>0; Layout.fillWidth: true; Layout.leftMargin: 28; Layout.rightMargin: 28; Layout.bottomMargin: 10
                implicitHeight: errorText.implicitHeight+24; radius: 9; color: "#30212a"
                Text { id: errorText; anchors.left: parent.left; anchors.right: dismiss.left; anchors.margins: 12; anchors.verticalCenter: parent.verticalCenter; text: controller.lastError; color: "#e3aab5"; font.pixelSize: 12; wrapMode: Text.WordWrap }
                UiButton { id: dismiss; anchors.right: parent.right; anchors.rightMargin: 7; anchors.verticalCenter: parent.verticalCenter; compact: true; text: "×"; onClicked: controller.clearError() }
            }
            Loader {
                Layout.fillWidth: true; Layout.fillHeight: true
                sourceComponent: root.currentPage==="Wheel" ? wheelPage : root.currentPage==="Actions" ? actionsPage
                    : root.currentPage==="Center gestures" ? centerGesturesPage
                    : root.currentPage==="Trigger" ? triggerPage : root.currentPage==="Appearance" ? appearancePage
                    : root.currentPage==="Behaviour" ? behaviourPage : root.currentPage==="Integrations" ? integrationsPage : generalPage
            }
        }
    }
    Component {
        id: centerGesturesPage
        CenterGesturesPage {
            onEditAction: function(gesture, slot, action) { actionPicker.editCenterAction(gesture, slot, action) }
        }
    }
    Component {
        id: wheelPage
        ColumnLayout {
            anchors.fill: parent; anchors.leftMargin: 28; anchors.rightMargin: 28; anchors.bottomMargin: 24; spacing: 18
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                ComboBox { Layout.preferredWidth: 220; model: controller.config.decks.map(d=>d.name); currentIndex: controller.currentDeckIndex; onActivated: function(i) { controller.selectDeck(i) } }
                UiButton { text: "+ New deck"; compact: true; onClicked: controller.createDeck() }
                Item { Layout.fillWidth: true }
                Text { text: root.activeActions.length + " actions"; color: "#726e81"; font.pixelSize: 11 }
                UiButton { text: "Deck options"; compact: true; onClicked: deckMenu.open() }
                Menu {
                    id: deckMenu
                    MenuItem { text: "Duplicate deck"; onTriggered: controller.duplicateDeck(root.activeDeck.id) }
                    MenuItem { text: "Move earlier"; enabled: controller.currentDeckIndex>0; onTriggered: controller.moveDeck(controller.currentDeckIndex,controller.currentDeckIndex-1) }
                    MenuItem { text: "Move later"; enabled: controller.currentDeckIndex<controller.deckCount-1; onTriggered: controller.moveDeck(controller.currentDeckIndex,controller.currentDeckIndex+1) }
                    MenuSeparator {}
                    MenuItem { text: "Delete deck"; enabled: controller.deckCount>1; onTriggered: controller.deleteDeck(root.activeDeck.id) }
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 18
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true; radius: 16; color: "#0d0e13"; border.color: "#1f202a"; clip: true
                    Canvas {
                        anchors.fill: parent
                        onPaint: {
                            const c=getContext("2d"); c.clearRect(0,0,width,height); c.fillStyle="#20212b"
                            for(let x=22;x<width;x+=24) for(let y=22;y<height;y+=24) { c.beginPath(); c.arc(x,y,0.65,0,Math.PI*2); c.fill() }
                        }
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                    }
                    RowLayout {
                        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 20
                        Text { text: "LIVE PREVIEW"; color: "#737081"; font.pixelSize: 9; font.letterSpacing: 1.3; Layout.fillWidth: true }
                        UiButton { text: "Replay"; compact: true; onClicked: previewWheel.replayEntrance() }
                    }
                    Item {
                        id: previewArea
                        anchors.fill: parent; anchors.topMargin: 65; anchors.bottomMargin: 65
                        WheelView {
                            id: previewWheel
                            anchors.centerIn: parent
                            deck: root.activeDeck; deckIndex: controller.currentDeckIndex; deckCount: controller.deckCount
                            selectedIndex: root.selectedSlot; appearance: controller.config.appearance
                            reduceMotion: !!controller.config.appearance.reduceMotion; interactive: true
                            scale: Math.min(1.12,(previewArea.width-22)/(width*appearanceScale),(previewArea.height-12)/(height*appearanceScale))
                            onSlotClicked: function(i) { root.selectedSlot=i }
                            onScrollDeck: function(d) { root.scrollPreviewDeck(d) }
                        }
                    }
                    Column {
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 21; anchors.horizontalCenter: parent.horizontalCenter; spacing: 7
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Hold " + controller.config.trigger.shortcut + "   →   move   →   release"; color: "#aaa4ba"; font.pixelSize: 11 }
                        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Select a position to edit · Scroll to change decks"; color: "#5e5b6c"; font.pixelSize: 10 }
                    }
                }
                Rectangle {
                    Layout.preferredWidth: 278; Layout.fillHeight: true; radius: 16; color: "#17171f"; border.color: "#24232f"
                    ColumnLayout {
                        anchors.fill: parent; anchors.margins: 17; spacing: 10
                        Text { text: "Actions"; color: "#e6e2ee"; font.pixelSize: 16; font.weight: Font.Medium }
                        Text { text: "Clockwise, starting at the top"; color: "#797486"; font.pixelSize: 10; Layout.bottomMargin: 6 }
                        ListView {
                            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 5
                            model: root.activeActions
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                width: ListView.view.width; height: 53; radius: 9
                                color: root.selectedSlot===index ? "#2e293e" : slotHover.containsMouse ? "#22212c" : "transparent"
                                border.color: root.selectedSlot===index ? "#514462" : "transparent"
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 11
                                    Image { source: "image://icons/"+encodeURIComponent(modelData.icon || "applications-other"); sourceSize: Qt.size(64,64); Layout.preferredWidth: 29; Layout.preferredHeight: 29 }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 3
                                        Text { text: modelData.name || "Choose action"; color: "#dcd7e7"; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                                        Text { text: controller.actionUnavailableReason(modelData) || (modelData.type==="application" ? "Application" : modelData.type); color: controller.actionUnavailableReason(modelData) ? "#d6a0a5" : "#7b758b"; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideRight }
                                    }
                                    Text { text: index+1; font.pixelSize: 10; color: "#625c72" }
                                }
                                MouseArea { id: slotHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.selectedSlot=index; onDoubleClicked: actionPicker.editAction(root.activeDeck.id,index,modelData) }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 6
                            UiButton { text: "Edit action"; accent: true; Layout.fillWidth: true; enabled: root.selectedSlot>=0; onClicked: actionPicker.editAction(root.activeDeck.id,root.selectedSlot,root.activeAction) }
                            UiButton { text: "↑"; compact: true; enabled: root.selectedSlot>0; onClicked: { const n=root.selectedSlot-1; controller.moveAction(root.activeDeck.id,root.selectedSlot,n); root.selectedSlot=n } }
                            UiButton { text: "↓"; compact: true; enabled: root.selectedSlot>=0 && root.selectedSlot<root.activeActions.length-1; onClicked: { const n=root.selectedSlot+1; controller.moveAction(root.activeDeck.id,root.selectedSlot,n); root.selectedSlot=n } }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2634"; Layout.topMargin: 9; Layout.bottomMargin: 4 }
                        Text { text: "DECK SETTINGS"; color: "#777082"; font.pixelSize: 9; font.letterSpacing: 1.1 }
                        TextField { id: deckName; Layout.fillWidth: true; text: root.activeDeck.name || ""; placeholderText: "Deck name"; onEditingFinished: controller.setDeckName(root.activeDeck.id,text) }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Positions"; color: "#9890a5"; font.pixelSize: 11; Layout.fillWidth: true }
                            ComboBox { Layout.preferredWidth: 77; model: [4,5,6,7,8]; currentIndex: root.activeActions.length-4; onActivated: function(i) { controller.resizeDeck(root.activeDeck.id,i+4) } }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: actionsPage
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 28
            spacing: 14
            Text { text: "Installed applications"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
            Text { text: "Pick any application from the Wheel editor. Arcade Wheel discovers desktop entries and Start Menu shortcuts automatically."; color: "#91889f"; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: catalogSearch; Layout.fillWidth: true; placeholderText: "Search applications" }
                UiButton { text: "Refresh"; onClicked: controller.refreshApplications() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 13; color: "#17171f"; border.color: "#282532"
                ListView {
                    anchors.fill: parent; anchors.margins: 10; clip: true; spacing: 4
                    model: controller.applications.filter(function(app) { return app.name.toLowerCase().indexOf(catalogSearch.text.toLowerCase()) >= 0 })
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 50; radius: 8; color: "#20202a"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                            Image { source: "image://icons/" + encodeURIComponent(modelData.icon || "application-x-executable"); sourceSize: Qt.size(28, 28); Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                            Text { text: modelData.name; color: "#e7e1f0"; font.pixelSize: 13; Layout.fillWidth: true }
                            Text { text: modelData.id; color: "#7b6e89"; font.pixelSize: 11; elide: Text.ElideMiddle; Layout.maximumWidth: 270 }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: triggerPage
            ScrollView {
                anchors.fill: parent
                clip: true
            ColumnLayout {
                width: Math.max(0, Math.min(700, parent.width - 60))
                x: 30; y: 26; spacing: 22
                Text { text: "Activation shortcut"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
                Text { text: "Click the field and press your shortcut. Hold it to open the wheel, move toward an action, then release to launch."; color: "#91889f"; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Rectangle {
                    id: shortcutField
                    objectName: "shortcutField"
                    Layout.fillWidth: true; implicitHeight: 58
                    radius: 10
                    color: recording ? "#292538" : "#1d1b26"
                    border.color: recording ? "#b7b7f7" : "#42394f"
                    border.width: recording ? 2 : 1
                    activeFocusOnTab: true
                    property bool recording: false
                    property string candidate: ""
                    property string modifierLabel: ""
                    property int capturedKey: 0
                    property bool conflictVisible: false
                    function beginRecording() {
                        if (recording) return
                        candidate = ""; capturedKey = 0; modifierLabel = ""
                        conflictVisible = false
                        recording = true
                        controller.setShortcutRecording(true)
                        captureTimeout.restart()
                    }
                    function endRecording() {
                        captureTimeout.stop()
                        recording = false
                        controller.setShortcutRecording(false)
                    }
                    function cancelRecording() {
                        endRecording()
                        candidate = ""; capturedKey = 0
                    }
                    function acceptCandidate(overrideConflict) {
                        conflictVisible = !controller.applyShortcut(candidate, overrideConflict)
                        if (!conflictVisible) candidate = ""
                    }
                    onActiveFocusChanged: {
                        // Restoring window focus (including cursor warping on
                        // Hyprland) must never start capture and cancel a wheel.
                        if (!activeFocus && recording) cancelRecording()
                    }
                    Component.onDestruction: controller.setShortcutRecording(false)
                    Keys.priority: Keys.BeforeItem
                    Keys.onPressed: function(event) {
                        if (!recording) {
                            if (!event.isAutoRepeat && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)) {
                                event.accepted = true
                                beginRecording()
                            }
                            return
                        }
                        event.accepted = true
                        if (event.isAutoRepeat) return
                        if (event.key === Qt.Key_Escape) { cancelRecording(); return }
                        if (capturedKey !== 0) return
                        const shortcut = controller.shortcutFromKey(event.key, event.modifiers)
                        if (shortcut.length > 0) {
                            candidate = shortcut
                            capturedKey = event.key
                        } else {
                            let parts = []
                            if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
                            if (event.modifiers & Qt.AltModifier) parts.push("Alt")
                            if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
                            if (event.modifiers & Qt.MetaModifier) parts.push("Super")
                            modifierLabel = parts.join(" + ") + " + …"
                        }
                    }
                    Keys.onReleased: function(event) {
                        if (!recording) return
                        event.accepted = true
                        if (event.isAutoRepeat) return
                        if (capturedKey !== 0 && event.key === capturedKey) {
                            endRecording()
                            acceptCandidate(false)
                        } else if (capturedKey === 0) modifierLabel = ""
                    }
                    Text {
                        anchors.left: parent.left; anchors.leftMargin: 18; anchors.verticalCenter: parent.verticalCenter
                        text: shortcutField.candidate || (shortcutField.recording ? (shortcutField.modifierLabel || "Press a key or combination…") : controller.config.trigger.shortcut)
                        color: "#e8e3ef"; font.pixelSize: 16; font.weight: Font.Medium
                    }
                    Text {
                        anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                        text: shortcutField.recording ? "Esc to cancel" : "Click to change"
                        color: "#91889f"; font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: { shortcutField.forceActiveFocus(); shortcutField.beginRecording() }
                    }
                    Timer { id: captureTimeout; interval: 10000; onTriggered: shortcutField.cancelRecording() }
                    Connections {
                        target: root.Window.window
                        function onActiveChanged() { if (target && !target.active && shortcutField.recording) shortcutField.cancelRecording() }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: conflictContent.implicitHeight + 28
                    visible: shortcutField.conflictVisible && controller.shortcutConflict.length > 0
                    radius: 10; color: "#302722"; border.color: "#67513e"
                    ColumnLayout {
                        id: conflictContent
                        anchors.fill: parent; anchors.margins: 14; spacing: 12
                        Text { text: controller.shortcutConflict; color: "#efd8bd"; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: "Override uses this key for Arcade Wheel while it is running. The previous shortcut is restored when you change keys or quit."; color: "#bca995"; font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        RowLayout {
                            UiButton { text: "Override"; accent: true; onClicked: shortcutField.acceptCandidate(true) }
                            UiButton { text: "Choose another key"; onClicked: { shortcutField.forceActiveFocus(); shortcutField.beginRecording() } }
                        }
                    }
                }
                Text { text: controller.triggerStatus; color: "#b4a6c9"; font.pixelSize: 12; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                SettingSlider { Layout.fillWidth: true; title: "Hold threshold"; detail: "Wait this long before opening. Zero opens immediately."; section: "trigger"; settingKey: "holdThresholdMs"; fromValue: 0; toValue: 500; stepValue: 10; unit: " ms" }
            }
        }
    }

    Component {
        id: appearancePage
        RowLayout {
            id: appearanceLayout
            anchors.fill: parent; anchors.margins: 28; spacing: 18
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                ColumnLayout {
                    width: Math.max(0, Math.min(560, parent.width - 18)); spacing: 20
                    Text { text: "Shape and motion"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
                    SettingSlider { Layout.fillWidth: true; title: "Wheel radius"; section: "appearance"; settingKey: "radius"; fromValue: 110; toValue: 240; stepValue: 2; unit: " px" }
                    SettingSlider { Layout.fillWidth: true; title: "Wheel scale"; section: "appearance"; settingKey: "scale"; fromValue: 0.75; toValue: 1.3; stepValue: 0.05 }
                    SettingSlider { Layout.fillWidth: true; title: "Center radius"; section: "appearance"; settingKey: "centerRadius"; fromValue: 36; toValue: 72; stepValue: 1; unit: " px" }
                    SettingSlider { Layout.fillWidth: true; title: "Segment depth"; section: "appearance"; settingKey: "cardSize"; fromValue: 66; toValue: 110; stepValue: 2; unit: " px" }
                    SettingSlider { Layout.fillWidth: true; title: "Gap"; section: "appearance"; settingKey: "gap"; fromValue: 4; toValue: 24; stepValue: 1; unit: " px" }
                    SettingSlider { Layout.fillWidth: true; title: "Icon size"; section: "appearance"; settingKey: "iconSize"; fromValue: 24; toValue: 42; stepValue: 1; unit: " px" }
                    SettingSlider { Layout.fillWidth: true; title: "Surface opacity"; section: "appearance"; settingKey: "opacity"; fromValue: 0.45; toValue: 1; stepValue: 0.05 }
                    SettingSlider { Layout.fillWidth: true; title: "Background dim"; section: "appearance"; settingKey: "dim"; fromValue: 0; toValue: 0.3; stepValue: 0.02 }
                    SettingSlider { Layout.fillWidth: true; title: "Selected scale"; section: "appearance"; settingKey: "selectedScale"; fromValue: 1; toValue: 1.12; stepValue: 0.01 }
                    SettingSlider { Layout.fillWidth: true; title: "Animation speed"; section: "appearance"; settingKey: "animationSpeed"; fromValue: 0.5; toValue: 1.5; stepValue: 0.1 }
                    SettingToggle { Layout.fillWidth: true; title: "Show outer labels"; detail: "The center still shows the selected action name."; section: "appearance"; settingKey: "labels" }
                    SettingToggle { Layout.fillWidth: true; title: "Reduce motion"; section: "appearance"; settingKey: "reduceMotion" }
                    Item { Layout.preferredHeight: 20 }
                }
            }
            Rectangle {
                Layout.preferredWidth: 310; Layout.minimumWidth: 270; Layout.fillHeight: true
                visible: appearanceLayout.width >= 720
                radius: 15; color: "#17171f"; border.color: "#282532"
                Text { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 16; text: "PREVIEW"; color: "#968ba6"; font.pixelSize: 11; font.weight: Font.DemiBold }
                WheelView {
                    anchors.centerIn: parent
                    deck: root.activeDeck
                    deckIndex: controller.currentDeckIndex
                    deckCount: controller.deckCount
                    selectedIndex: root.selectedSlot
                    appearance: controller.config.appearance
                    reduceMotion: !!controller.config.appearance.reduceMotion
                    scale: Math.max(0.1, Math.min(0.68,
                        280 / (width * appearanceScale), (parent.height - 30) / (height * appearanceScale)))
                }
            }
        }
    }

    Component {
        id: behaviourPage
        ScrollView {
            anchors.fill: parent; clip: true
            ColumnLayout {
                width: Math.max(0, Math.min(700, parent.width - 60)); x: 30; y: 26; spacing: 22
                Text { text: "Interaction"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
                SettingSlider { Layout.fillWidth: true; title: "Cancel dead zone"; detail: "Return to the screen center before releasing to cancel."; section: "behaviour"; settingKey: "deadZone"; fromValue: 30; toValue: 120; stepValue: 2; unit: " px" }
                SettingSlider { Layout.fillWidth: true; title: "Selection sensitivity"; section: "behaviour"; settingKey: "selectionSensitivity"; fromValue: 0.5; toValue: 2; stepValue: 0.1 }
                SettingSlider { Layout.fillWidth: true; title: "Scroll cooldown"; detail: "Prevents accidental multi-deck jumps."; section: "behaviour"; settingKey: "scrollCooldownMs"; fromValue: 0; toValue: 300; stepValue: 10; unit: " ms" }
                SettingToggle { Layout.fillWidth: true; title: "Wrap deck scrolling"; section: "behaviour"; settingKey: "wrapDecks" }
                SettingToggle { Layout.fillWidth: true; title: "Reverse scroll direction"; section: "behaviour"; settingKey: "scrollReverse" }
                SettingToggle { Layout.fillWidth: true; title: "Focus an existing application window"; detail: "When the platform can find one, activate it instead of launching another."; section: "behaviour"; settingKey: "focusExisting" }
            }
        }
    }

    Component {
        id: integrationsPage
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 28; spacing: 16
            Text { text: "Arcade actions"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
            Text { text: "Saved Arcade actions remain in your decks when their app is unavailable."; color: "#91889f"; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                Text { text: controller.arcadeActions.length ? controller.arcadeActions.length + " tools available" : "No tools discovered"; color: "#b7aacb"; font.pixelSize: 13; Layout.fillWidth: true }
                UiButton { text: "Refresh"; onClicked: controller.refreshProviders() }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; radius: 13; color: "#17171f"; border.color: "#282532"
                ListView {
                    anchors.fill: parent; anchors.margins: 12; clip: true; spacing: 5
                    model: controller.arcadeActions
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width; height: 58; radius: 8; color: "#20202a"
                        Column { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 13; spacing: 3
                            Text { text: modelData.title || modelData.id; color: "#e8e2f0"; font.pixelSize: 13 }
                            Text { text: modelData.description || modelData.id; color: "#91839f"; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: generalPage
        ScrollView {
            anchors.fill: parent; clip: true
            ColumnLayout {
                width: Math.max(0, Math.min(720, parent.width - 60)); x: 30; y: 26; spacing: 23
                Text { text: "Application"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
                SettingToggle { Layout.fillWidth: true; title: "Start on login"; detail: "Keep Arcade Wheel ready in the background."; section: "general"; settingKey: "startOnLogin" }
                SettingToggle { Layout.fillWidth: true; title: "Show failure notifications"; section: "general"; settingKey: "showNotifications" }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#302938" }
                Text { text: "Configuration"; color: "#e8e3ef"; font.pixelSize: 18; font.weight: Font.Medium }
                Text { text: controller.configPath; color: "#8d809e"; font.pixelSize: 12; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                RowLayout {
                    spacing: 9
                    UiButton { text: "Import JSON"; onClicked: importDialog.open() }
                    UiButton { text: "Export JSON"; onClicked: exportDialog.open() }
                    UiButton { text: "Reset defaults"; destructive: true; onClicked: resetDialog.open() }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#302938" }
                RowLayout {
                    spacing: 9
                    UiButton { text: "Restart Arcade Wheel"; onClicked: controller.requestRestart() }
                    UiButton { text: "Quit Arcade Wheel"; destructive: true; onClicked: controller.requestQuit() }
                }
                Dialog {
                    id: resetDialog
                    title: "Reset configuration?"
                    modal: true
                    anchors.centerIn: Overlay.overlay
                    standardButtons: Dialog.Ok | Dialog.Cancel
                    onAccepted: controller.resetDefaults()
                    Text { text: "This replaces every deck and setting with defaults."; color: "#e6edf8"; font.pixelSize: 13 }
                }
            }
        }
    }
}
