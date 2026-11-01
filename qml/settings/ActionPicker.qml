pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC
import QtQuick.Layouts
import QtQuick.Templates as T
import Arcade

// Choose what a slot (or a center-gesture step) does. The kit dialog shell
// with a type list on the left (a select on a narrow window), the type's
// fields on the right, and Clear / Cancel / Save in the footer. In link
// mode it places an action another Arcade app asked to add.
T.Popup {
    id: picker
    required property WheelController controller
    property string deckId: ""
    property string gestureId: ""
    property int slotIndex: -1
    property string selectedType: "application"
    property string selectedAppId: ""
    property string selectedIcon: "applications-other"
    // Placing an action another Arcade app asked to add (Arcade Link).
    property bool linkMode: false
    property string linkDeckName: ""
    readonly property var placementDeck: picker.controller.config.decks.find(function(deck) { return deck.id === picker.deckId }) || ({})
    readonly property bool compact: width < 640
    readonly property var types: [
        { id: "application", title: "Applications" },
        { id: "command", title: "Command" },
        { id: "url", title: "Website" },
        { id: "file", title: "File or folder" },
        { id: "system", title: "System" },
        { id: "media", title: "Media" },
        { id: "desktop", title: "Desktop" },
        { id: "arcade", title: "Arcade apps" },
        { id: "plugin", title: "Provider" }
    ]
    property var arcadePayload: ({})
    readonly property var visibleTypes: types.filter(function(type) {
        return type.id !== "arcade" || picker.controller.arcadeActions.length > 0 || picker.selectedType === "arcade"
    })
    function sameArcade(row) {
        return row.app === arcadePayload.app && row.id === arcadePayload.action
            && (row.options && row.options.pipeline || "") === (arcadePayload.options && arcadePayload.options.pipeline || "")
            && (row.preset || "") === (arcadePayload.preset || "")
    }
    readonly property var arcadeRows: picker.controller.arcadeActions.filter(function(row) {
        const search = arcadeSearch.text.toLowerCase()
        return (row.appName + " " + row.title + " " + row.id).toLowerCase().indexOf(search) >= 0
    })
    readonly property var appRows: picker.controller.applications.filter(function(app) {
        return app.name.toLowerCase().indexOf(appSearch.text.toLowerCase()) >= 0
    })
    readonly property var selectedArcade: picker.controller.arcadeActions.find(function(row) { return picker.sameArcade(row) }) || ({})
    readonly property var arcadeModes: selectedArcade.inputModes || []
    function inputTitle(mode) {
        return ({none: "None", clipboard: "Clipboard", "lens-selection": "Lens selection", "file-selection": "File selection"})[mode] || mode
    }
    function arcadeSummary() {
        if (!arcadePayload.app) return "Choose an action"
        const app = selectedArcade.appName || picker.controller.peerDisplayName(arcadePayload.app)
        const title = selectedArcade.title || nameField.text || "Saved action"
        return app + " · " + (arcadePayload.options && arcadePayload.options.pipeline ? "Pipeline · " : "") + title
    }
    function effectSummary() {
        const names = {"sends-to-device": "↗ Sends to your devices", "writes-files": "Writes files", "opens-ui": "Opens a window",
                       "clipboard": "Uses the clipboard", "overwrites-files": "Overwrites files", "deletes-files": "Deletes files",
                       "launch-apps": "Launches apps", "executes-commands": "Runs commands", "window-control": "Controls windows",
                       "persists": "Saves settings", "network": "Uses the network", "uploads-content": "↗ Uploads content"}
        const effects = selectedArcade.effects || []
        return ["sends-to-device", "uploads-content", "network", "opens-ui", "launch-apps", "writes-files", "overwrites-files", "deletes-files", "clipboard", "executes-commands", "window-control", "persists"]
            .filter(function(effect) { return effects.indexOf(effect) >= 0 }).map(function(effect) { return names[effect] }).join(" · ")
    }
    function arcadeSlot() { return {type: "arcade", payload: arcadePayload} }
    function chooseArcade(row) {
        arcadePayload = {app: row.app, action: row.id, version: row.version || 1, input: row.input, options: row.options ? JSON.parse(JSON.stringify(row.options)) : {}}
        if (row.preset) arcadePayload.preset = row.preset
        nameField.text = row.title
        selectedIcon = row.glyph
    }
    readonly property var systemActions: [
        { id: "lock", name: "Lock screen", icon: "system-lock-screen" },
        { id: "suspend", name: "Suspend", icon: "system-suspend" },
        { id: "hibernate", name: "Hibernate", icon: "system-suspend-hibernate" },
        { id: "logout", name: "Log out", icon: "system-log-out" },
        { id: "reboot", name: "Restart", icon: "system-reboot" },
        { id: "poweroff", name: "Power off", icon: "system-shutdown" },
        { id: "screenshot", name: "Screenshot", icon: "camera-photo" }
    ]
    readonly property var mediaActions: [
        { id: "play-pause", name: "Play / pause", icon: "media-playback-start" },
        { id: "next", name: "Next track", icon: "media-skip-forward" },
        { id: "previous", name: "Previous track", icon: "media-skip-backward" },
        { id: "stop", name: "Stop", icon: "media-playback-stop" },
        { id: "volume-up", name: "Volume up", icon: "audio-volume-high" },
        { id: "volume-down", name: "Volume down", icon: "audio-volume-low" },
        { id: "mute", name: "Mute / unmute", icon: "audio-volume-muted" }
    ]
    // The window and workspace actions the Linux backend runs in a Hyprland session.
    readonly property var desktopActions: [
        { id: "fullscreen", name: "Toggle fullscreen", icon: "view-fullscreen" },
        { id: "togglefloating", name: "Toggle floating", icon: "window-restore" },
        { id: "pin", name: "Pin window on all workspaces", icon: "window-pin" },
        { id: "centerwindow", name: "Center window", icon: "zoom-fit-best" },
        { id: "killactive", name: "Close window", icon: "window-close" },
        { id: "movefocus-left", name: "Focus window on the left", icon: "go-previous" },
        { id: "movefocus-right", name: "Focus window on the right", icon: "go-next" },
        { id: "movefocus-up", name: "Focus window above", icon: "go-up" },
        { id: "movefocus-down", name: "Focus window below", icon: "go-down" },
        { id: "workspace-next", name: "Next workspace", icon: "go-next" },
        { id: "workspace-previous", name: "Previous workspace", icon: "go-previous" },
        { id: "workspace-last", name: "Last used workspace", icon: "view-refresh" },
        { id: "movetoworkspace-next", name: "Move window to next workspace", icon: "go-last" },
        { id: "movetoworkspace-previous", name: "Move window to previous workspace", icon: "go-first" }
    ]
    readonly property bool canSave: (!linkMode || slotIndex >= 0)
        && (selectedType === "arcade" ? !!arcadePayload.app && !!arcadePayload.action
                                      : selectedType !== "application" || selectedAppId.length > 0)

    function hasType(typeId) {
        return types.some(function(type) { return type.id === typeId })
    }

    function selectedTypeTitle() {
        const type = types.find(function(item) { return item.id === selectedType })
        return type ? type.title : "Action"
    }

    function editAction(deckIdValue, slot, action) {
        linkMode = false
        gestureId = ""
        deckId = deckIdValue
        slotIndex = slot
        const actionType = action && action.type && action.type !== "none" ? action.type : "application"
        selectedType = hasType(actionType) ? actionType : "application"
        selectedIcon = action && action.icon ? action.icon : "applications-other"
        selectedAppId = action && action.payload ? (action.payload.desktopId || "") : ""
        nameField.text = action && action.type !== "none" ? (action.name || "") : ""
        commandField.text = action && action.payload ? (action.payload.command || "") : ""
        urlField.text = action && action.payload ? (action.payload.url || "") : ""
        fileField.text = action && action.payload ? (action.payload.path || "") : ""
        providerField.text = action && action.payload ? (action.payload.providerId || "") : ""
        toolField.text = action && action.payload ? (action.payload.toolId || "") : ""
        arcadePayload = action && action.type === "arcade" && action.payload ? JSON.parse(JSON.stringify(action.payload)) : ({})
        arcadeSearch.text = ""
        appSearch.text = ""
        systemCombo.currentIndex = 0
        mediaCombo.currentIndex = 0
        desktopCombo.currentIndex = 0
        for (let i = 0; i < systemActions.length; i++)
            if (action && action.payload && systemActions[i].id === action.payload.id) systemCombo.currentIndex = i
        for (let i = 0; i < mediaActions.length; i++)
            if (action && action.payload && mediaActions[i].id === action.payload.id) mediaCombo.currentIndex = i
        for (let i = 0; i < desktopActions.length; i++)
            if (action && action.payload && desktopActions[i].id === action.payload.id) desktopCombo.currentIndex = i
        open()
    }

    function editLinkAction(deckIdValue, deckNameValue, slot, action) {
        editAction(deckIdValue, slot, action)
        linkMode = true
        linkDeckName = deckNameValue
    }

    function editCenterAction(gesture, slot, action) {
        editAction("", slot, action)
        gestureId = gesture
    }

    function saveAction() {
        let payload = {}
        let title = nameField.text.trim()
        let icon = selectedIcon
        if (selectedType === "application") {
            payload.desktopId = selectedAppId
            if (!selectedAppId) return
        } else if (selectedType === "command") {
            payload.command = commandField.text.trim()
            icon = "utilities-terminal"
            if (!title) title = "Command"
        } else if (selectedType === "url") {
            payload.url = urlField.text.trim()
            icon = "internet-web-browser"
            if (!title) title = "Website"
        } else if (selectedType === "file") {
            payload.path = fileField.text.trim()
            icon = "folder"
            if (!title) title = "File or folder"
        } else if (selectedType === "system") {
            let item = systemActions[systemCombo.currentIndex]
            payload.id = item.id
            icon = item.icon
            if (!title) title = item.name
        } else if (selectedType === "media") {
            let item = mediaActions[mediaCombo.currentIndex]
            payload.id = item.id
            icon = item.icon
            if (!title) title = item.name
        } else if (selectedType === "desktop") {
            let item = desktopActions[desktopCombo.currentIndex]
            payload.id = item.id
            icon = item.icon
            if (!title) title = item.name
        } else if (selectedType === "arcade") {
            payload = JSON.parse(JSON.stringify(arcadePayload))
            if (!payload.app || !payload.action) return
            icon = "qrc:/assets/arcade/" + payload.app + ".svg"
            if (!title) title = "Arcade action"
        } else if (selectedType === "plugin") {
            payload.providerId = providerField.text.trim()
            payload.toolId = toolField.text.trim()
            icon = "applications-other"
            if (!title) title = "Provider action"
        }
        const action = {type: selectedType, name: title, icon: icon, payload: payload}
        if (gestureId.length > 0) picker.controller.setCenterGestureAction(gestureId, slotIndex, action)
        else if (!picker.controller.setAction(deckId, slotIndex, action)) return
        if (linkMode) picker.controller.finishLinkAction(true, linkDeckName)
        linkMode = false
        close()
    }

    anchors.centerIn: T.Overlay.overlay
    // A 24 px margin each side leaves room for the pop shadow.
    width: Math.max(1, Math.min(820, (T.Overlay.overlay ? T.Overlay.overlay.width : 868) - Theme.space48))
    height: Math.max(1, Math.min(linkMode ? 700 : 600, (T.Overlay.overlay ? T.Overlay.overlay.height : 648) - Theme.space48))
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    onClosed: { if (linkMode) picker.controller.finishLinkAction(false); linkMode = false }
    T.Overlay.modal: Rectangle { color: Theme.scrim }
    Shortcut {
        sequences: [StandardKey.Close]
        enabled: picker.visible
        onActivated: { picker.close(); if (picker.parent && picker.parent.Window.window) picker.parent.Window.window.close() }
    }
    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.panel
        border.color: Theme.line
        Shadow { radius: Theme.radiusLg }
    }

    // A label above a field.
    component FieldLabel: Label { kind: "small"; medium: true; color: Theme.muted; Layout.fillWidth: true }
    // A sentence under a field.
    component FieldNote: Label { kind: "small"; color: Theme.muted; wrapMode: Text.Wrap; Layout.fillWidth: true }

    contentItem: ColumnLayout {
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space20 * 3
            Layout.minimumHeight: Theme.space20 * 3
            Layout.leftMargin: Theme.space24
            Layout.rightMargin: Theme.space16
            spacing: Theme.space8
            Label { text: picker.linkMode ? "Add to Wheel" : "Choose action"; kind: "titleSm"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { variant: "ghost"; small: true; iconName: "x"; tooltip: "Close"; onClicked: picker.close() }
        }
        Divider { Layout.fillWidth: true }
        ColumnLayout {
            visible: picker.linkMode
            Layout.fillWidth: true
            Layout.margins: Theme.space20
            Layout.leftMargin: Theme.space24
            spacing: Theme.space10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: (picker.controller.linkSource || "Another Arcade app") + " wants to add “" + (picker.controller.linkDraft.name || "an action") + "”. Choose a slot, then confirm."
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: Theme.space4
                    FieldLabel { text: "Deck" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: picker.controller.config.decks.map(function(deck) { return deck.name })
                        currentIndex: picker.controller.config.decks.findIndex(function(deck) { return deck.id === picker.deckId })
                        onActivated: function(index) {
                            const deck = picker.controller.config.decks[index]
                            picker.deckId = deck.id; picker.linkDeckName = deck.name; picker.slotIndex = -1
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    spacing: Theme.space4
                    FieldLabel { text: "Slot" }
                    ComboBox {
                        id: placementSlot
                        objectName: "placementSlot"
                        Layout.fillWidth: true
                        model: (picker.placementDeck.actions || []).map(function(action, index) { return (index + 1) + " · " + (action.name || "Choose action") })
                        currentIndex: picker.slotIndex
                        displayText: picker.slotIndex < 0 ? "Choose a slot" : currentText
                        onActivated: function(index) { picker.slotIndex = index }
                    }
                }
            }
        }
        Divider { visible: picker.linkMode; Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            // The action types, as sidebar tabs.
            Rectangle {
                Layout.preferredWidth: 184
                Layout.fillHeight: true
                visible: !picker.compact
                color: Theme.bg
                bottomLeftRadius: Theme.radiusLg
                QQC.ScrollView {
                    anchors.fill: parent
                    anchors.margins: Theme.space10
                    Column {
                        width: parent.width
                        spacing: Theme.space2
                        Repeater {
                            model: picker.visibleTypes
                            delegate: NavItem {
                                required property var modelData
                                text: modelData.title
                                iconName: ({application: "apps", command: "code", url: "globe", file: "folder", system: "power",
                                            media: "play", desktop: "layout", arcade: "sparkle", plugin: "cube"})[modelData.id] || "sparkle"
                                checked: picker.selectedType === modelData.id
                                onClicked: picker.selectedType = modelData.id
                            }
                        }
                    }
                }
            }
            Divider { vertical: true; visible: !picker.compact; Layout.fillHeight: true }
            // The type's fields; they scroll when the window is short, so
            // the footer buttons stay in view.
            Flickable {
                id: fieldsFlick
                readonly property real inset: picker.compact ? Theme.space16 : Theme.space24
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: fields.height + inset * 2
                boundsBehavior: Flickable.StopAtBounds
                QQC.ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: fields
                    x: fieldsFlick.inset
                    y: fieldsFlick.inset
                    width: fieldsFlick.width - fieldsFlick.inset * 2
                    height: Math.max(implicitHeight, fieldsFlick.height - fieldsFlick.inset * 2)
                    spacing: Theme.space12
                    ComboBox {
                        visible: picker.compact
                        Layout.fillWidth: true
                        model: picker.visibleTypes.map(function(type) { return type.title })
                        currentIndex: {
                            for (let i = 0; i < picker.visibleTypes.length; ++i)
                                if (picker.visibleTypes[i].id === picker.selectedType) return i
                            return 0
                        }
                        onActivated: function(index) { picker.selectedType = picker.visibleTypes[index].id }
                    }
                    Label { visible: !picker.compact; text: picker.selectedTypeTitle(); kind: "heading" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "Action name" }
                        TextField { id: nameField; objectName: "nameField"; Layout.fillWidth: true; placeholderText: "Name shown in the wheel center" }
                    }

                    ColumnLayout {
                        visible: picker.selectedType === "application"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.space8
                        SearchField { id: appSearch; Layout.fillWidth: true; placeholderText: "Search installed applications" }
                        ListView {
                            id: appList
                            visible: picker.appRows.length > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: Theme.rowDialog * 3
                            clip: true
                            spacing: Theme.space2
                            boundsBehavior: Flickable.StopAtBounds
                            reuseItems: true
                            model: picker.appRows
                            QQC.ScrollBar.vertical: ScrollBar {}
                            delegate: Rectangle {
                                id: appRow
                                required property var modelData
                                readonly property bool chosen: picker.selectedAppId === modelData.id
                                width: ListView.view.width
                                height: Theme.rowDialog
                                radius: Theme.radiusSm
                                color: chosen ? Theme.accentSoft : appHover.hovered ? Theme.hover : "transparent"
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.name
                                Accessible.selected: chosen
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space10
                                    anchors.rightMargin: Theme.space10
                                    spacing: Theme.space10
                                    Image {
                                        source: "image://icons/" + encodeURIComponent(appRow.modelData.icon || "application-x-executable") + "?v=" + picker.controller.iconGeneration
                                        sourceSize: Qt.size(Theme.tileSm, Theme.tileSm)
                                        Layout.preferredWidth: Theme.tileSm
                                        Layout.preferredHeight: Theme.tileSm
                                        fillMode: Image.PreserveAspectFit
                                        asynchronous: true
                                    }
                                    Label { text: appRow.modelData.name; medium: appRow.chosen; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Icon { visible: appRow.chosen; name: "check"; color: Theme.accentInk }
                                }
                                HoverHandler { id: appHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler {
                                    onTapped: {
                                        picker.selectedAppId = appRow.modelData.id
                                        picker.selectedIcon = appRow.modelData.icon || "application-x-executable"
                                        nameField.text = appRow.modelData.name
                                    }
                                }
                            }
                        }
                        EmptyState {
                            visible: picker.appRows.length === 0
                            Layout.fillWidth: true
                            iconName: picker.controller.applications.length > 0 ? "search" : "apps"
                            title: picker.controller.applications.length > 0 ? "No matches" : "No applications found"
                            text: picker.controller.applications.length > 0
                                  ? "Nothing installed matches “" + appSearch.text + "”."
                                  : "Install an application, then refresh the list on Installed applications."
                        }
                        Item { visible: picker.appRows.length === 0; Layout.fillHeight: true }
                        FieldNote {
                            visible: !picker.selectedAppId && picker.appRows.length > 0
                            text: "Choose an application to enable Save."
                        }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "command"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "Command and arguments" }
                        TextField { id: commandField; Layout.fillWidth: true; placeholderText: "example: kitty --directory /home/you/Projects" }
                        FieldNote { text: "Commands run directly, without a shell, so ~ and $VARIABLES are not expanded. Use sh -c '…' when shell syntax is needed." }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "url"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "URL" }
                        TextField { id: urlField; Layout.fillWidth: true; placeholderText: "https://example.com" }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "file"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "File or folder path" }
                        TextField { id: fileField; Layout.fillWidth: true; placeholderText: "/home/you/Documents" }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "system"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "System action" }
                        ComboBox { id: systemCombo; objectName: "systemCombo"; Layout.fillWidth: true; model: picker.systemActions.map(function(x) { return x.name }) }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "media"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "Media action" }
                        ComboBox { id: mediaCombo; objectName: "mediaCombo"; Layout.fillWidth: true; model: picker.mediaActions.map(function(x) { return x.name }) }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "desktop"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "Window or workspace action" }
                        ComboBox { id: desktopCombo; objectName: "desktopCombo"; Layout.fillWidth: true; model: picker.desktopActions.map(function(x) { return x.name }) }
                        FieldNote { text: "Desktop actions run in a Hyprland session on Linux." }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "arcade"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.space8
                        SearchField { id: arcadeSearch; Layout.fillWidth: true; placeholderText: "Search Arcade actions" }
                        ListView {
                            id: arcadeList
                            visible: picker.arcadeRows.length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(contentHeight, 168)
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: picker.arcadeRows
                            QQC.ScrollBar.vertical: ScrollBar {}
                            section.property: "appName"
                            section.delegate: Label {
                                required property string section
                                text: section
                                kind: "caption"
                                color: Theme.faint
                                height: Theme.space24
                                verticalAlignment: Text.AlignVCenter
                            }
                            delegate: Rectangle {
                                id: arcadeRow
                                required property var modelData
                                readonly property bool chosen: picker.sameArcade(modelData)
                                width: ListView.view.width
                                height: Theme.control + Theme.space6
                                radius: Theme.radiusSm
                                color: chosen ? Theme.accentSoft : arcadeHover.hovered ? Theme.hover : "transparent"
                                Accessible.role: Accessible.ListItem
                                Accessible.name: modelData.title
                                Accessible.selected: chosen
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Theme.space10
                                    anchors.rightMargin: Theme.space10
                                    spacing: Theme.space10
                                    GlyphTile { appId: arcadeRow.modelData.app; size: Theme.tileSm }
                                    Label { text: arcadeRow.modelData.title + (arcadeRow.modelData.outbound ? " ↗" : ""); medium: arcadeRow.chosen; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Icon { visible: arcadeRow.chosen; name: "check"; color: Theme.accentInk }
                                }
                                HoverHandler { id: arcadeHover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: picker.chooseArcade(arcadeRow.modelData) }
                            }
                        }
                        EmptyState {
                            visible: picker.arcadeRows.length === 0
                            Layout.fillWidth: true
                            iconName: arcadeSearch.text.length ? "search" : "sparkle"
                            title: arcadeSearch.text.length ? "No matches" : "No Arcade actions yet"
                            text: arcadeSearch.text.length ? "No Arcade action matches “" + arcadeSearch.text + "”."
                                                           : "Arcade apps you run offer their actions here."
                        }
                        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: picker.arcadeSummary() }
                        FieldNote { visible: text.length > 0; text: picker.effectSummary() }
                        FieldLabel { text: "Input" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: picker.arcadeModes.map(function(mode) { return picker.inputTitle(mode) })
                            currentIndex: picker.arcadeModes.indexOf(picker.arcadePayload.input || "none")
                            enabled: picker.arcadeModes.length > 0
                            displayText: currentIndex < 0 ? "Choose input" : currentText
                            onActivated: function(index) {
                                let payload = JSON.parse(JSON.stringify(picker.arcadePayload))
                                payload.input = picker.arcadeModes[index]
                                picker.arcadePayload = payload
                            }
                        }
                        Label {
                            visible: text.length > 0
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            kind: "small"
                            color: Theme.dangerInk
                            text: picker.arcadePayload.action ? picker.controller.actionUnavailableReason(picker.arcadeSlot()) : ""
                        }
                    }
                    ColumnLayout {
                        visible: picker.selectedType === "plugin"
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        FieldLabel { text: "Provider" }
                        TextField { id: providerField; Layout.fillWidth: true; placeholderText: "Provider ID" }
                        FieldLabel { text: "Action" }
                        TextField { id: toolField; Layout.fillWidth: true; placeholderText: "Action/tool ID" }
                        FieldNote { text: "Provider actions remain visible when their provider is unavailable." }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        Divider { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.space32 * 2
            Layout.minimumHeight: Theme.space32 * 2
            Layout.leftMargin: Theme.space20
            Layout.rightMargin: Theme.space20
            spacing: Theme.space8
            Button {
                text: picker.gestureId.length > 0 ? "Remove action" : "Clear slot"
                visible: !picker.linkMode && (picker.gestureId.length === 0 || picker.slotIndex >= 0)
                variant: "danger"
                onClicked: {
                    if (picker.gestureId.length > 0) picker.controller.removeCenterGestureAction(picker.gestureId, picker.slotIndex)
                    else picker.controller.setAction(picker.deckId, picker.slotIndex, {type: "none", name: "Choose action", icon: "applications-other", payload: {}})
                    picker.close()
                }
            }
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: picker.close() }
            Button {
                objectName: "saveActionButton"
                text: picker.linkMode ? "Add to selected slot" : "Save action"
                variant: "primary"
                enabled: picker.canSave
                onClicked: picker.saveAction()
            }
        }
    }
}
