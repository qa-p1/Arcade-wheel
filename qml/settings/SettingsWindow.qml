pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Dialogs
import Arcade

// Settings and the wheel editor (Template A). main.cpp shows it in a normal
// window with native decorations and releases it a while after it closes.
// Each page is built when it is shown, so only one page's items exist.
Rectangle {
    id: root
    required property WheelController controller
    objectName: "settingsRoot"
    width: Theme.settingsWidth
    height: Theme.settingsHeight
    color: Theme.bg

    Binding { target: Theme; property: "appId"; value: "arcade.wheel" }
    Binding { target: Theme; property: "mode"; value: root.controller.config.general.theme || "system" }
    Binding { target: Theme; property: "reducedMotion"; value: !!root.controller.config.appearance.reduceMotion }

    readonly property var pages: [
        { title: "General", icon: "settings", subtitle: "How Arcade Wheel looks and starts, and your configuration file." },
        { title: "Shortcuts", icon: "keyboard", subtitle: "The key you hold to open the wheel." },
        { title: "Your wheel", icon: "orbit", subtitle: "Arrange your actions. Keep them within reach." },
        { title: "Actions", icon: "sparkle", subtitle: "What other Arcade apps let the wheel do." },
        { title: "Center gestures", icon: "command", subtitle: "Click or hold the middle of the wheel to run a routine." },
        { title: "Appearance", icon: "layout", subtitle: "The ring's shape, motion and colour." },
        { title: "Behaviour", icon: "wrench", subtitle: "How selection and deck scrolling respond." },
        { title: "Installed applications", icon: "apps", subtitle: "The applications you can put on the wheel." },
        { title: "Connected apps", icon: "link", subtitle: "Which Arcade apps work with Arcade Wheel." },
        { title: "About", icon: "info", subtitle: "Version, updates, help and licences." }
    ]
    readonly property int wheelPage: 2
    readonly property int shortcutsPage: 1
    readonly property bool triggerReady: root.controller.triggerStatus.indexOf("Ready") === 0
    property int selectedSlot: -1
    readonly property var activeDeck: root.controller.currentDeck
    readonly property var activeActions: activeDeck && activeDeck.actions ? activeDeck.actions : []
    readonly property var activeAction: selectedSlot >= 0 && selectedSlot < activeActions.length ? activeActions[selectedSlot] : ({})
    readonly property bool linkPending: Object.keys(root.controller.linkDraft).length > 0

    function showPage(index) { sidebar.currentIndex = index; sidebar.syncTabs(); }
    function scrollPreviewDeck(delta) {
        if (root.controller.deckCount < 2) return;
        const b = root.controller.config.behaviour;
        let next = root.controller.currentDeckIndex + (b.scrollReverse ? -delta : delta);
        next = b.wrapDecks ? (next + root.controller.deckCount) % root.controller.deckCount
                           : Math.max(0, Math.min(root.controller.deckCount - 1, next));
        root.controller.selectDeck(next);
    }
    function syncLinkDraft() {
        if (root.linkPending) {
            root.showPage(root.wheelPage);
            actionPicker.editLinkAction(root.activeDeck.id, root.activeDeck.name || "the wheel", -1, root.controller.linkDraft);
        } else if (actionPicker.linkMode) actionPicker.close();
    }
    Component.onCompleted: { root.showPage(root.wheelPage); root.syncLinkDraft(); }
    Shortcut { sequences: [StandardKey.Close]; onActivated: root.Window.window.close() }
    // Escape goes back one step. An open dialog, menu or the action editor takes
    // the key for itself (closePolicy); a text field gives its focus back first;
    // at the top of Settings it closes the window.
    focus: true
    Keys.onEscapePressed: function (event) {
        const win = root.Window.window;
        const focused = win ? win.activeFocusItem : null;
        event.accepted = true;
        if (focused && focused !== root && typeof focused.cursorPosition === "number") root.forceActiveFocus();
        else if (win) win.close();
    }
    Connections {
        target: root.controller
        function onCurrentDeckChanged() { if (root.selectedSlot >= root.activeActions.length) root.selectedSlot = -1; }
        function onLinkDraftChanged() { root.syncLinkDraft(); }
        function onLastErrorChanged() {
            if (root.controller.lastError.length) toast.show(root.controller.lastError, "error");
        }
    }

    ActionPicker { id: actionPicker; objectName: "actionPicker"; controller: root.controller }
    FileDialog {
        id: importDialog; title: "Import configuration"; nameFilters: ["JSON configuration (*.json)"]
        onAccepted: root.controller.importConfig(selectedFile.toString())
    }
    FileDialog {
        id: exportDialog; title: "Export configuration"; fileMode: FileDialog.SaveFile
        defaultSuffix: "json"; nameFilters: ["JSON configuration (*.json)"]
        onAccepted: if (root.controller.exportConfig(selectedFile.toString())) toast.show("Exported", "info")
    }
    ColorDialog {
        id: accentDialog
        title: "Accent colour"
        selectedColor: root.controller.config.appearance.accentColor
        onAccepted: root.controller.updateSetting("appearance", "accentColor", selectedColor.toString())
    }
    Dialog {
        id: resetDialog
        objectName: "resetDialog"
        title: "Reset everything?"
        body: "Every deck, slot and setting goes back to its default. Export your configuration first to keep a copy."
        destructive: true
        actions: [
            Button { text: "Keep my wheel"; onClicked: resetDialog.close() },
            Button { variant: "destructive"; text: "Reset everything"; onClicked: { root.controller.resetDefaults(); resetDialog.close(); } }
        ]
    }
    Dialog {
        id: deleteDeckDialog
        title: "Delete this deck?"
        body: "“" + (root.activeDeck.name || "Deck") + "” and its actions leave the wheel."
        destructive: true
        actions: [
            Button { text: "Keep"; onClicked: deleteDeckDialog.close() },
            Button { variant: "destructive"; text: "Delete deck"; onClicked: { root.controller.deleteDeck(root.activeDeck.id); deleteDeckDialog.close(); } }
        ]
    }

    Sidebar {
        id: sidebar
        objectName: "sidebar"
        height: parent.height
        appName: "Arcade Wheel"
        version: Qt.application.version
        collapsed: root.width < Theme.collapseBelow
        NavItem { objectName: "nav-0"; text: root.pages[0].title; iconName: root.pages[0].icon }
        NavItem { objectName: "nav-1"; text: root.pages[1].title; iconName: root.pages[1].icon }
        NavItem { objectName: "nav-2"; text: root.pages[2].title; iconName: root.pages[2].icon }
        NavItem { objectName: "nav-3"; text: root.pages[3].title; iconName: root.pages[3].icon }
        NavItem { objectName: "nav-4"; text: root.pages[4].title; iconName: root.pages[4].icon }
        NavItem { objectName: "nav-5"; text: root.pages[5].title; iconName: root.pages[5].icon }
        NavItem { objectName: "nav-6"; text: root.pages[6].title; iconName: root.pages[6].icon }
        NavItem { objectName: "nav-7"; text: root.pages[7].title; iconName: root.pages[7].icon }
        NavItem { objectName: "nav-8"; text: root.pages[8].title; iconName: root.pages[8].icon }
        NavItem { objectName: "nav-9"; text: root.pages[9].title; iconName: root.pages[9].icon }
        footer: [
            // Ready, or the shortcut needs attention (opens Shortcuts).
            Item {
                objectName: "triggerStatus"
                visible: !sidebar.collapsed
                width: parent ? parent.width : 0
                height: statusRow.implicitHeight
                Row {
                    id: statusRow
                    spacing: Theme.space8
                    Dot { anchors.verticalCenter: parent.verticalCenter; tone: root.triggerReady ? "ok" : "warn" }
                    Label {
                        text: root.triggerReady ? "Ready in background" : "Shortcut needs attention"
                        kind: "caption"
                        color: root.triggerReady ? Theme.okInk : Theme.warnInk
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: !root.triggerReady
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.showPage(root.shortcutsPage)
                }
            }
        ]
    }

    Loader {
        id: pageLoader
        objectName: "pageLoader"
        x: sidebar.width
        width: parent.width - x
        height: parent.height
        sourceComponent: [generalPage, shortcutsPage, wheelPageComponent, actionsPage, centerPage,
                          appearancePage, behaviourPage, applicationsPage, connectedPage, aboutPage][sidebar.currentIndex]
    }

    // One scrolling page: the head (title, subtitle, actions) and its sections.
    component Page: Flickable {
        id: page
        property string title: root.pages[sidebar.currentIndex].title
        property string subtitle: root.pages[sidebar.currentIndex].subtitle
        property real maxWidth: Theme.contentMax
        default property alias content: body.data
        property alias headActions: head.actions
        readonly property real columnWidth: body.width
        contentHeight: body.y + body.height + Theme.space48
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ScrollBar {}
        Column {
            id: body
            x: Math.max(Theme.space32, (page.width - width) / 2)
            y: Theme.space32
            width: Math.min(page.maxWidth, page.width - Theme.space32 * 2)
            spacing: Theme.space32
            PageHead { id: head; width: parent.width; title: page.title; subtitle: page.subtitle }
        }
    }

    // 1. General
    Component {
        id: generalPage
        Page {
            ListSection {
                width: parent.width
                title: "Appearance"
                ListRow {
                    title: "Theme"
                    subtitle: "System follows your desktop. The wheel follows it too."
                    SegmentedControl {
                        objectName: "themePicker"
                        options: [{ value: "system", label: "System" }, { value: "light", label: "Light" }, { value: "dark", label: "Dark" }]
                        value: root.controller.config.general.theme || "system"
                        onActivated: value => root.controller.updateSetting("general", "theme", value)
                    }
                }
            }
            ListSection {
                width: parent.width
                title: "Application"
                WheelSwitch {
                    controller: root.controller; section: "general"; settingKey: "startOnLogin"
                    title: "Start at login"
                    subtitle: "Arcade Wheel starts in the background, ready for your shortcut."
                }
                WheelSwitch {
                    controller: root.controller; section: "general"; settingKey: "showNotifications"
                    title: "Show failure notifications"
                    subtitle: "Tell me when an action couldn't run."
                }
                ListRow {
                    title: "Restart Arcade Wheel"
                    subtitle: "Reloads the configuration and the shortcut."
                    Button { text: "Restart"; onClicked: root.controller.requestRestart() }
                }
            }
            ListSection {
                width: parent.width
                title: "Configuration"
                note: "Portable exports include decks and slots. Credential, token, secret and device identity fields are left out. An import saves a private backup beside the current configuration first."
                ListRow {
                    title: "Configuration file"
                    subtitle: root.controller.configPath
                }
                ListRow {
                    title: "Export portable JSON"
                    subtitle: "Decks, slots and settings in one file."
                    Button { text: "Export…"; iconName: "download"; onClicked: exportDialog.open() }
                }
                ListRow {
                    title: "Import portable JSON"
                    subtitle: "Replaces your decks and settings."
                    Button { text: "Import…"; onClicked: importDialog.open() }
                }
                ListRow {
                    title: "Reset defaults"
                    subtitle: "Every deck, slot and setting goes back to its default."
                    Button { objectName: "resetButton"; text: "Reset…"; variant: "danger"; onClicked: resetDialog.open() }
                }
            }
        }
    }

    // 2. Shortcuts
    Component {
        id: shortcutsPage
        Page {
            ListSection {
                width: parent.width
                title: "Activation shortcut"
                note: "Hold the shortcut to open the wheel, move toward an action, then release to run it. Release in the middle to cancel."
                ListRow {
                    title: "Open the wheel"
                    stacked: true
                    ShortcutRecorder {
                        id: recorder
                        objectName: "shortcutRecorder"
                        sequence: root.controller.shortcutForKit(root.controller.config.trigger.shortcut)
                        defaultSequence: "F8"
                        conflict: root.controller.shortcutConflict
                        note: root.controller.triggerStatus
                        property string candidate: ""
                        // The trigger stays off while a key is being recorded,
                        // so pressing it here never opens the wheel.
                        onRecordingChanged: root.controller.setShortcutRecording(recording)
                        Component.onDestruction: root.controller.setShortcutRecording(false)
                        onCaptured: sequence => {
                            recorder.candidate = root.controller.shortcutFromKit(sequence);
                            if (recorder.candidate.length) root.controller.applyShortcut(recorder.candidate, false);
                        }
                        onResetRequested: root.controller.applyShortcut("F8", false)
                        onUseAnyway: if (recorder.candidate.length) root.controller.applyShortcut(recorder.candidate, true)
                        onChooseAnother: recording = true
                    }
                }
                WheelSlider {
                    controller: root.controller; section: "trigger"; settingKey: "holdThresholdMs"
                    title: "Hold threshold"
                    subtitle: "Wait this long before opening. 0 opens at once."
                    from: 0; to: 500; stepSize: 10; unit: "ms"
                }
            }
        }
    }

    // 3. Your wheel: decks, the live preview and the slot editor.
    Component {
        id: wheelPageComponent
        Page {
            id: wheelPage
            objectName: "wheelPage"
            maxWidth: 1200
            readonly property bool stacked: columnWidth < 780
            headActions: [
                Button { objectName: "openWheelButton"; text: "Open wheel"; variant: "primary"; onClicked: root.controller.showWheel() }
            ]
            Row {
                id: deckRow
                width: parent.width
                spacing: Theme.space8
                ComboBox {
                    objectName: "deckPicker"
                    width: Math.min(240, deckRow.width * 0.4)
                    model: root.controller.config.decks.map(d => d.name)
                    currentIndex: root.controller.currentDeckIndex
                    onActivated: i => root.controller.selectDeck(i)
                }
                Button { text: "New deck"; iconName: "plus"; onClicked: root.controller.createDeck() }
                Item { width: Math.max(0, deckRow.width - x - deckMeta.width - deckOptions.width - Theme.space8 * 2); height: 1 }
                Label {
                    id: deckMeta
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.activeActions.length === 1 ? "1 action" : root.activeActions.length + " actions"
                    kind: "small"; color: Theme.muted
                }
                Button {
                    id: deckOptions
                    objectName: "deckOptions"
                    text: "Deck options"; iconName: "more"
                    onClicked: deckMenu.popup(deckOptions, 0, deckOptions.height + Theme.space4)
                    Menu {
                        id: deckMenu
                        MenuItem { text: "Duplicate deck"; iconName: "copy"; onTriggered: root.controller.duplicateDeck(root.activeDeck.id) }
                        MenuItem { text: "Move earlier"; enabled: root.controller.currentDeckIndex > 0; onTriggered: root.controller.moveDeck(root.controller.currentDeckIndex, root.controller.currentDeckIndex - 1) }
                        MenuItem { text: "Move later"; enabled: root.controller.currentDeckIndex < root.controller.deckCount - 1; onTriggered: root.controller.moveDeck(root.controller.currentDeckIndex, root.controller.currentDeckIndex + 1) }
                        MenuSeparator {}
                        MenuItem { text: "Delete deck"; iconName: "trash"; danger: true; enabled: root.controller.deckCount > 1; onTriggered: deleteDeckDialog.open() }
                    }
                }
            }
            Flow {
                width: parent.width
                spacing: Theme.space16
                // The live preview: click a position to edit it, scroll to change decks.
                Rectangle {
                    id: previewPane
                    objectName: "previewPane"
                    width: wheelPage.stacked ? parent.width : parent.width - actionsPanel.width - Theme.space16
                    height: wheelPage.stacked ? 400 : Math.max(420, actionsPanel.height)
                    radius: Theme.radiusLg
                    color: Theme.sunken
                    border.color: Theme.line
                    clip: true
                    Label {
                        x: Theme.space16; y: Theme.space16
                        text: "Live preview"; kind: "caption"; color: Theme.muted
                    }
                    Button {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.margins: Theme.space10
                        text: "Replay"; iconName: "play"; variant: "ghost"; small: true
                        onClicked: previewWheel.replayEntrance()
                    }
                    Item {
                        id: previewArea
                        anchors.fill: parent; anchors.topMargin: 52; anchors.bottomMargin: 60
                        WheelView {
                            id: previewWheel
                            anchors.centerIn: parent
                            deck: root.activeDeck; deckIndex: root.controller.currentDeckIndex; deckCount: root.controller.deckCount
                            iconGeneration: root.controller.iconGeneration
                            selectedIndex: root.selectedSlot; appearance: root.controller.config.appearance
                            reduceMotion: !!root.controller.config.appearance.reduceMotion; interactive: true
                            scale: Math.max(0.1, Math.min(1.12, (previewArea.width - 22) / (width * appearanceScale), (previewArea.height - 12) / (height * appearanceScale)))
                            onSlotClicked: i => root.selectedSlot = i
                            onScrollDeck: d => root.scrollPreviewDeck(d)
                        }
                    }
                    Column {
                        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space16
                        anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.space12
                        spacing: Theme.space6
                        KeyCombo {
                            anchors.horizontalCenter: parent.horizontalCenter
                            small: true
                            keys: root.controller.shortcutForKit(root.controller.config.trigger.shortcut).split("+")
                        }
                        Label {
                            width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight
                            text: "Hold, move, release · Click a position to edit · Scroll to change decks"
                            kind: "caption"; color: Theme.muted
                        }
                    }
                }
                Surface {
                    id: actionsPanel
                    objectName: "actionsPanel"
                    // An in-page panel, not an overlay: no pop shadow.
                    shadow: false
                    width: wheelPage.stacked ? parent.width : 300
                    height: panelContent.implicitHeight + Theme.space16 * 2
                    radius: Theme.radiusLg
                    Column {
                        id: panelContent
                        x: Theme.space16; y: Theme.space16
                        width: parent.width - Theme.space16 * 2
                        spacing: Theme.space12
                        Column {
                            width: parent.width
                            spacing: Theme.space2
                            Label { text: "Actions"; kind: "heading" }
                            Label { text: "Clockwise, starting at the top"; kind: "caption"; color: Theme.muted }
                        }
                        Column {
                            width: parent.width
                            Repeater {
                                model: root.activeActions
                                delegate: ListRow {
                                    id: slotRow
                                    required property var modelData
                                    required property int index
                                    readonly property string unavailable: root.controller.actionUnavailableReason(modelData)
                                    width: parent.width
                                    minimumHeight: Theme.rowDialog
                                    clickable: true
                                    title: modelData.type === "none" ? "Choose action" : (modelData.name || "Choose action")
                                    subtitle: unavailable || ({ application: "Application", command: "Command", url: "Website", file: "File or folder", system: "System", media: "Media", desktop: "Desktop", arcade: "Arcade app", plugin: "Provider", none: "Empty" })[modelData.type] || modelData.type
                                    meta: String(index + 1)
                                    leading: Component {
                                        Image {
                                            width: Theme.tileSm; height: Theme.tileSm
                                            sourceSize: Qt.size(64, 64)
                                            fillMode: Image.PreserveAspectFit
                                            source: "image://icons/" + encodeURIComponent(slotRow.modelData.icon || "applications-other") + "?v=" + root.controller.iconGeneration
                                        }
                                    }
                                    Rectangle {
                                        visible: root.selectedSlot === slotRow.index
                                        width: Theme.selectionBar; height: Theme.space16; radius: 1
                                        color: Theme.accent
                                    }
                                    onClicked: root.selectedSlot = index
                                    TapHandler { acceptedButtons: Qt.LeftButton; onDoubleTapped: actionPicker.editAction(root.activeDeck.id, slotRow.index, slotRow.modelData) }
                                }
                            }
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.space6
                            Button {
                                objectName: "editActionButton"
                                width: parent.width - moveUp.width - moveDown.width - Theme.space6 * 2
                                text: "Edit action"; variant: "primary"
                                enabled: root.selectedSlot >= 0
                                onClicked: actionPicker.editAction(root.activeDeck.id, root.selectedSlot, root.activeAction)
                            }
                            Button {
                                id: moveUp
                                iconName: "chevronDown"; rotation: 180; tooltip: "Move earlier"
                                enabled: root.selectedSlot > 0
                                onClicked: { const n = root.selectedSlot - 1; root.controller.moveAction(root.activeDeck.id, root.selectedSlot, n); root.selectedSlot = n; }
                            }
                            Button {
                                id: moveDown
                                iconName: "chevronDown"; tooltip: "Move later"
                                enabled: root.selectedSlot >= 0 && root.selectedSlot < root.activeActions.length - 1
                                onClicked: { const n = root.selectedSlot + 1; root.controller.moveAction(root.activeDeck.id, root.selectedSlot, n); root.selectedSlot = n; }
                            }
                        }
                        Divider { width: parent.width }
                        Label { text: "Deck"; kind: "caption"; color: Theme.muted }
                        TextField {
                            objectName: "deckName"
                            width: parent.width
                            text: root.activeDeck.name || ""
                            placeholderText: "Deck name"
                            onEditingFinished: root.controller.setDeckName(root.activeDeck.id, text)
                        }
                        Item {
                            width: parent.width
                            height: positions.height
                            Label { anchors.verticalCenter: parent.verticalCenter; text: "Positions"; kind: "small" }
                            ComboBox {
                                id: positions
                                objectName: "positionsPicker"
                                anchors.right: parent.right
                                width: 96
                                model: ["4", "5", "6", "7", "8"]
                                currentIndex: root.activeActions.length - 4
                                onActivated: i => root.controller.resizeDeck(root.activeDeck.id, i + 4)
                            }
                        }
                    }
                }
            }
        }
    }

    // 4. Actions other Arcade apps offer
    Component {
        id: actionsPage
        Page {
            id: arcadePage
            property string query: ""
            readonly property var rows: root.controller.arcadeActions.filter(row =>
                (row.appName + " " + row.title + " " + row.id).toLowerCase().indexOf(arcadePage.query.toLowerCase()) >= 0)
            SearchField {
                width: parent.width
                placeholderText: "Search actions"
                visible: root.controller.arcadeActions.length > 0
                onEdited: text => arcadePage.query = text
            }
            ListSection {
                width: parent.width
                visible: arcadePage.rows.length > 0
                title: "Available actions"
                note: "Add one from Your wheel: select a position, then Edit action › Arcade apps."
                Repeater {
                    model: arcadePage.rows
                    delegate: ListRow {
                        id: arcadeRow
                        required property var modelData
                        width: parent.width
                        title: modelData.title + (modelData.outbound ? " ↗" : "")
                        subtitle: modelData.appName
                        leading: Component { GlyphTile { appId: arcadeRow.modelData.app; size: Theme.tileSm } }
                    }
                }
            }
            EmptyState {
                width: parent.width
                visible: arcadePage.rows.length === 0
                iconName: root.controller.arcadeActions.length ? "search" : "apps"
                title: root.controller.arcadeActions.length ? "Nothing matches “" + arcadePage.query + "”" : "No Arcade actions yet"
                text: root.controller.arcadeActions.length ? "Try another word." : "Install Arcade apps such as Box or Look and turn them on in Connected apps. Their actions appear here."
            }
        }
    }

    // 5. Center gestures
    Component {
        id: centerPage
        Page {
            CenterGesturesPage {
                width: parent.width
                controller: root.controller
                onEditAction: (gesture, slot, action) => actionPicker.editCenterAction(gesture, slot, action)
            }
        }
    }

    // 6. Appearance
    Component {
        id: appearancePage
        Page {
            ListSection {
                width: parent.width
                title: "Shape and motion"
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "radius"; title: "Wheel radius"; from: 110; to: 240; stepSize: 2; unit: "px" }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "scale"; title: "Wheel scale"; from: 0.75; to: 1.3; stepSize: 0.05; decimals: 2 }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "centerRadius"; title: "Center radius"; from: 36; to: 72; stepSize: 1; unit: "px" }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "cardSize"; title: "Segment depth"; from: 66; to: 110; stepSize: 2; unit: "px" }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "gap"; title: "Gap"; from: 4; to: 24; stepSize: 1; unit: "px" }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "iconSize"; title: "Icon size"; from: 24; to: 42; stepSize: 1; unit: "px" }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "opacity"; title: "Surface opacity"; from: 0.45; to: 1; stepSize: 0.05; decimals: 2 }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "dim"; title: "Background dim"; from: 0; to: 0.3; stepSize: 0.02; decimals: 2 }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "selectedScale"; title: "Selected scale"; from: 1; to: 1.12; stepSize: 0.01; decimals: 2 }
                WheelSlider { controller: root.controller; section: "appearance"; settingKey: "animationSpeed"; title: "Animation speed"; from: 0.5; to: 1.5; stepSize: 0.1; decimals: 1 }
                WheelSwitch { controller: root.controller; section: "appearance"; settingKey: "reduceMotion"; title: "Reduce motion"; subtitle: "The wheel appears at once, without animation." }
            }
            ListSection {
                width: parent.width
                title: "Labels and colour"
                WheelSwitch { controller: root.controller; section: "appearance"; settingKey: "labels"; title: "Show outer labels"; subtitle: "The center still shows the selected action's name." }
                WheelSwitch {
                    objectName: "customAccentToggle"
                    controller: root.controller; section: "appearance"; settingKey: "customAccent"
                    title: "Use a custom colour"
                    subtitle: "Otherwise the wheel uses Arcade Wheel's amber."
                }
                ListRow {
                    visible: !!root.controller.config.appearance.customAccent
                    title: "Accent colour"
                    subtitle: root.controller.config.appearance.accentColor
                    Rectangle {
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        width: Theme.tileSm; height: Theme.tileSm; radius: Theme.radiusSm
                        color: root.controller.config.appearance.accentColor
                        border.color: Theme.lineStrong
                    }
                    Button { text: "Choose…"; onClicked: accentDialog.open() }
                }
            }
        }
    }

    // 7. Behaviour
    Component {
        id: behaviourPage
        Page {
            ListSection {
                width: parent.width
                title: "Interaction"
                WheelSlider { controller: root.controller; section: "behaviour"; settingKey: "deadZone"; title: "Cancel dead zone"; subtitle: "Return to the middle before releasing to cancel."; from: 30; to: 120; stepSize: 2; unit: "px" }
                WheelSlider { controller: root.controller; section: "behaviour"; settingKey: "selectionSensitivity"; title: "Selection sensitivity"; from: 0.5; to: 2; stepSize: 0.1; decimals: 1 }
                WheelSlider { controller: root.controller; section: "behaviour"; settingKey: "scrollCooldownMs"; title: "Scroll cooldown"; subtitle: "Prevents accidental jumps across several decks."; from: 0; to: 300; stepSize: 10; unit: "ms" }
                WheelSwitch { controller: root.controller; section: "behaviour"; settingKey: "wrapDecks"; title: "Wrap deck scrolling" }
                WheelSwitch { controller: root.controller; section: "behaviour"; settingKey: "scrollReverse"; title: "Reverse scroll direction" }
                WheelSwitch { controller: root.controller; section: "behaviour"; settingKey: "focusExisting"; title: "Focus an open window"; subtitle: "When the desktop can find one, bring it forward instead of starting another copy." }
            }
        }
    }

    // 8. Installed applications
    Component {
        id: applicationsPage
        Page {
            id: appsPage
            property string query: ""
            readonly property var rows: root.controller.applications.filter(app =>
                app.name.toLowerCase().indexOf(appsPage.query.toLowerCase()) >= 0)
            headActions: [
                Button { text: "Refresh"; iconName: "refresh"; onClicked: root.controller.refreshApplications() }
            ]
            SearchField {
                width: parent.width
                placeholderText: "Search applications"
                visible: root.controller.applications.length > 0
                onEdited: text => appsPage.query = text
            }
            ListSection {
                width: parent.width
                visible: appsPage.rows.length > 0
                title: "Applications"
                meta: appsPage.rows.length === 1 ? "1 app" : appsPage.rows.length + " apps"
                note: "Arcade Wheel finds desktop entries and Start menu shortcuts by itself."
                Repeater {
                    model: appsPage.rows
                    delegate: ListRow {
                        id: appRow
                        required property var modelData
                        width: parent.width
                        minimumHeight: Theme.rowDialog
                        title: modelData.name
                        meta: modelData.id
                        leading: Component {
                            Image {
                                width: Theme.tileSm; height: Theme.tileSm
                                sourceSize: Qt.size(56, 56)
                                fillMode: Image.PreserveAspectFit
                                source: "image://icons/" + encodeURIComponent(appRow.modelData.icon || "application-x-executable") + "?v=" + root.controller.iconGeneration
                            }
                        }
                    }
                }
            }
            EmptyState {
                width: parent.width
                visible: appsPage.rows.length === 0
                iconName: root.controller.applications.length ? "search" : "apps"
                title: root.controller.applications.length ? "Nothing matches “" + appsPage.query + "”" : "No applications found"
                text: root.controller.applications.length ? "Try another word." : "Arcade Wheel couldn't find installed applications. Refresh after installing one."
                Button { visible: !root.controller.applications.length; text: "Refresh"; onClicked: root.controller.refreshApplications() }
            }
        }
    }

    // 9. Connected apps
    Component {
        id: connectedPage
        Page {
            ConnectedAppsPage { width: parent.width; controller: root.controller }
        }
    }

    // 10. About
    Component {
        id: aboutPage
        Page {
            ListSection {
                width: parent.width
                title: "Arcade Wheel"
                ListRow {
                    title: "Version " + Qt.application.version
                    subtitle: "Your desktop, closer."
                    leading: Component { GlyphTile { appId: "arcade.wheel"; size: Theme.tile } }
                    Badge { text: root.controller.channel.length ? root.controller.channel.charAt(0).toUpperCase() + root.controller.channel.slice(1) : "Not installed" }
                }
                ListRow { title: "Updates"; subtitle: "Updates are managed by Arcade Tools." }
                ListRow {
                    title: "User guide"
                    subtitle: "Decks, actions, gestures and shortcuts."
                    Button { text: "Open user guide"; onClicked: root.controller.openUserGuide() }
                }
                ListRow {
                    title: "Logs"
                    subtitle: root.controller.logsPath
                    Button { text: "Open logs folder"; onClicked: root.controller.openLogsFolder() }
                }
                ListRow {
                    title: "Diagnostics"
                    subtitle: "App, version, desktop and Link details for a bug report. No action contents."
                    Button { text: "Copy diagnostics"; onClicked: { root.controller.copyDiagnostics(); toast.show("Copied", "info"); } }
                }
                ListRow {
                    title: "Licences"
                    subtitle: "MIT. Qt under LGPLv3; Inter under the SIL Open Font License."
                    Button { text: "Open"; onClicked: root.controller.openLicences() }
                }
                ListRow {
                    title: "Quit Arcade Wheel"
                    subtitle: "The wheel won't open until you start it again."
                    Button { objectName: "quitButton"; text: "Quit"; variant: "danger"; onClicked: root.controller.requestQuit() }
                }
            }
        }
    }

    // The latest message (a failed action, a copy) along the bottom.
    Toast {
        id: toast
        objectName: "settingsToast"
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: Theme.space16
        onDismissed: root.controller.clearError()
    }
}
