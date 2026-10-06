import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Popup {
    id: picker
    property string deckId: ""
    property string gestureId: ""
    property int slotIndex: -1
    property string selectedType: "application"
    property string selectedAppId: ""
    property string selectedIcon: "applications-other"
    // Placing an action another Arcade app asked to add (Arcade Link).
    property bool linkMode: false
    property string linkDeckName: ""
    property var placementDeck: controller.config.decks.find(function(deck) { return deck.id === picker.deckId }) || ({})
    readonly property bool compact: width < 640
    property var types: [
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
    property var visibleTypes: types.filter(function(type) { return type.id !== "arcade" || controller.arcadeActions.length > 0 || picker.selectedType === "arcade" })
    function sameArcade(row) {
        return row.app === arcadePayload.app && row.id === arcadePayload.action
            && (row.options && row.options.pipeline || "") === (arcadePayload.options && arcadePayload.options.pipeline || "")
            && (row.preset || "") === (arcadePayload.preset || "")
    }
    property var arcadeRows: controller.arcadeActions.filter(function(row) {
        const search = arcadeSearch.text.toLowerCase()
        return (row.appName + " " + row.title + " " + row.id).toLowerCase().indexOf(search) >= 0
    })
    property var selectedArcade: controller.arcadeActions.find(function(row) { return picker.sameArcade(row) }) || ({})
    property var arcadeModes: selectedArcade.inputModes || []
    function inputTitle(mode) {
        return ({none: "None", clipboard: "Clipboard", "lens-selection": "Lens selection", "file-selection": "File selection"})[mode] || mode
    }
    function arcadeSummary() {
        if (!arcadePayload.app) return "Choose an action"
        const app = selectedArcade.appName || controller.peerDisplayName(arcadePayload.app)
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
    property var systemActions: [
        { id: "lock", name: "Lock screen", icon: "system-lock-screen" },
        { id: "suspend", name: "Suspend", icon: "system-suspend" },
        { id: "logout", name: "Log out", icon: "system-log-out" },
        { id: "poweroff", name: "Power off", icon: "system-shutdown" },
        { id: "screenshot", name: "Screenshot", icon: "camera-photo" }
    ]
    property var mediaActions: [
        { id: "play-pause", name: "Play / pause", icon: "media-playback-start" },
        { id: "next", name: "Next track", icon: "media-skip-forward" },
        { id: "previous", name: "Previous track", icon: "media-skip-backward" }
    ]

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
        desktopField.text = action && action.payload ? (action.payload.id || "") : ""
        providerField.text = action && action.payload ? (action.payload.providerId || "") : ""
        toolField.text = action && action.payload ? (action.payload.toolId || "") : ""
        arcadePayload = action && action.type === "arcade" && action.payload ? JSON.parse(JSON.stringify(action.payload)) : ({})
        arcadeSearch.text = ""
        appSearch.text = ""
        systemCombo.currentIndex = 0
        mediaCombo.currentIndex = 0
        for (let i = 0; i < systemActions.length; i++)
            if (action && action.payload && systemActions[i].id === action.payload.id) systemCombo.currentIndex = i
        for (let i = 0; i < mediaActions.length; i++)
            if (action && action.payload && mediaActions[i].id === action.payload.id) mediaCombo.currentIndex = i
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
            payload.id = desktopField.text.trim()
            icon = "preferences-desktop"
            if (!title) title = "Desktop action"
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
        if (gestureId.length > 0) controller.setCenterGestureAction(gestureId, slotIndex, action)
        else if (!controller.setAction(deckId, slotIndex, action)) return
        if (linkMode) controller.finishLinkAction(true, linkDeckName)
        linkMode = false
        close()
    }

    anchors.centerIn: Overlay.overlay
    width: Math.max(1, Math.min(880, (parent ? parent.width : 916) - 36))
    height: Math.max(1, Math.min(linkMode ? 760 : 660, (parent ? parent.height : 796) - 36))
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onClosed: { if (linkMode) controller.finishLinkAction(false); linkMode = false }
    Overlay.modal: Rectangle { color: "#aa070b12" }
    Shortcut {
        sequence: StandardKey.Close
        enabled: picker.visible
        onActivated: { picker.close(); picker.parent.Window.window.close() }
    }
    background: Rectangle { color: "#18171f"; radius: 18; border.color: "#3a3044"; border.width: 1 }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 68
            Layout.leftMargin: 24
            Layout.rightMargin: 22
            Text { text: picker.linkMode ? "Add to Wheel" : "Choose action"; color: "#f0e9f6"; font.pixelSize: 21; font.weight: Font.DemiBold; Layout.fillWidth: true }
            UiButton { text: "Close"; compact: true; onClicked: picker.close() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#2e2737" }
        ColumnLayout {
            visible: picker.linkMode
            Layout.fillWidth: true
            Layout.leftMargin: 24; Layout.rightMargin: 22; Layout.topMargin: picker.linkMode ? 12 : 0; Layout.bottomMargin: picker.linkMode ? 12 : 0
            spacing: 8
            Text {
                Layout.fillWidth: true; color: "#c9bfd9"; font.pixelSize: 12; wrapMode: Text.WordWrap
                text: (controller.linkSource || "Another Arcade app") + " wants to add “" + (controller.linkDraft.name || "an action") + "”. Choose a slot, then confirm."
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 3
                    Text { text: "Deck"; color: "#9AA3B2"; font.pixelSize: 11 }
                    ComboBox {
                        Layout.fillWidth: true
                        model: controller.config.decks.map(function(deck) { return deck.name })
                        currentIndex: controller.config.decks.findIndex(function(deck) { return deck.id === picker.deckId })
                        onActivated: function(index) {
                            const deck = controller.config.decks[index]
                            picker.deckId = deck.id; picker.linkDeckName = deck.name; picker.slotIndex = -1
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 3
                    Text { text: "Slot"; color: "#9AA3B2"; font.pixelSize: 11 }
                    ComboBox {
                        id: placementSlot; objectName: "placementSlot"
                        Layout.fillWidth: true
                        model: (picker.placementDeck.actions || []).map(function(action, index) { return (index + 1) + " · " + (action.name || "Choose action") })
                        currentIndex: picker.slotIndex
                        displayText: picker.slotIndex < 0 ? "Choose a slot" : currentText
                        onActivated: function(index) { picker.slotIndex = index }
                    }
                }
            }
        }
        Rectangle { visible: picker.linkMode; Layout.fillWidth: true; height: 1; color: "#2e2737" }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Rectangle {
                Layout.preferredWidth: 178
                Layout.fillHeight: true
                visible: !picker.compact
                color: "#14141b"
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 10
                    ColumnLayout {
                        width: parent.width
                        spacing: 3
                        Repeater {
                            model: picker.visibleTypes
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                height: 38
                                radius: 8
                                color: picker.selectedType === modelData.id ? "#382e48" : hover.containsMouse ? "#27212e" : "transparent"
                                Text { anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 12; text: modelData.title; color: picker.selectedType === modelData.id ? "#e9e1f5" : "#aaa0b8"; font.pixelSize: 13 }
                                MouseArea { id: hover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: picker.selectedType = modelData.id }
                            }
                        }
                    }
                }
            }
            Rectangle { width: 1; Layout.fillHeight: true; visible: !picker.compact; color: "#2e2737" }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: picker.compact ? 14 : 22
                spacing: 12
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
                Text { visible: !picker.compact; text: picker.selectedTypeTitle(); color: "#f1f6ff"; font.pixelSize: 18; font.weight: Font.Medium }
                Text { text: "Action name"; color: "#a8b8cb"; font.pixelSize: 12 }
                TextField { id: nameField; Layout.fillWidth: true; placeholderText: "Name shown in the wheel center" }

                ColumnLayout {
                    visible: picker.selectedType === "application"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 9
                    TextField { id: appSearch; Layout.fillWidth: true; placeholderText: "Search installed applications" }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: controller.applications.filter(function(app) { return app.name.toLowerCase().indexOf(appSearch.text.toLowerCase()) >= 0 })
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 48
                            radius: 9
                            color: picker.selectedAppId === modelData.id ? "#304966" : hoverApp.containsMouse ? "#243345" : "#1b2633"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 11
                                Image { source: "image://icons/" + encodeURIComponent(modelData.icon || "application-x-executable"); sourceSize: Qt.size(28, 28); Layout.preferredWidth: 28; Layout.preferredHeight: 28; fillMode: Image.PreserveAspectFit }
                                Text { text: modelData.name; color: "#e8eff9"; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            MouseArea {
                                id: hoverApp
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    picker.selectedAppId = modelData.id
                                    picker.selectedIcon = modelData.icon || "application-x-executable"
                                    nameField.text = modelData.name
                                }
                            }
                        }
                    }
                    Text {
                        visible: !picker.selectedAppId
                        Layout.fillWidth: true
                        text: controller.applications.length > 0
                              ? "Choose an application to enable Save."
                              : "No installed applications were found."
                        color: "#91889f"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                ColumnLayout {
                    visible: picker.selectedType === "command"
                    Layout.fillWidth: true
                    Text { text: "Command and arguments"; color: "#a8b8cb"; font.pixelSize: 12 }
                    TextField { id: commandField; Layout.fillWidth: true; placeholderText: "example: kitty --directory /home/you/Projects" }
                    Text { text: "Commands run directly, without a shell, so ~ and $VARIABLES are not expanded. Use sh -c '…' when shell syntax is needed."; color: "#8193aa"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
                ColumnLayout {
                    visible: picker.selectedType === "url"
                    Layout.fillWidth: true
                    Text { text: "URL"; color: "#a8b8cb"; font.pixelSize: 12 }
                    TextField { id: urlField; Layout.fillWidth: true; placeholderText: "https://example.com" }
                }
                ColumnLayout {
                    visible: picker.selectedType === "file"
                    Layout.fillWidth: true
                    Text { text: "File or folder path"; color: "#a8b8cb"; font.pixelSize: 12 }
                    TextField { id: fileField; Layout.fillWidth: true; placeholderText: "/home/you/Documents" }
                }
                ColumnLayout {
                    visible: picker.selectedType === "system"
                    Layout.fillWidth: true
                    Text { text: "System action"; color: "#a8b8cb"; font.pixelSize: 12 }
                    ComboBox { id: systemCombo; Layout.fillWidth: true; model: picker.systemActions.map(function(x) { return x.name }) }
                }
                ColumnLayout {
                    visible: picker.selectedType === "media"
                    Layout.fillWidth: true
                    Text { text: "Media action"; color: "#a8b8cb"; font.pixelSize: 12 }
                    ComboBox { id: mediaCombo; Layout.fillWidth: true; model: picker.mediaActions.map(function(x) { return x.name }) }
                }
                ColumnLayout {
                    visible: picker.selectedType === "desktop"
                    Layout.fillWidth: true
                    Text { text: "Desktop action ID"; color: "#a8b8cb"; font.pixelSize: 12 }
                    TextField { id: desktopField; Layout.fillWidth: true; placeholderText: "example: fullscreen" }
                    Text { text: "Supported desktop actions depend on the platform backend."; color: "#8193aa"; font.pixelSize: 12 }
                }
                ColumnLayout {
                    visible: picker.selectedType === "arcade"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.maximumHeight: implicitHeight
                    spacing: 7
                    TextField { id: arcadeSearch; Layout.fillWidth: true; placeholderText: "Search Arcade actions" }
                    ListView {
                        id: arcadeList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: Math.min(contentHeight, 160)
                        Layout.minimumHeight: Math.min(contentHeight, 38)
                        Layout.maximumHeight: Math.min(contentHeight, 160)
                        clip: true
                        model: picker.arcadeRows
                        section.property: "appName"
                        section.delegate: Text {
                            required property string section
                            text: section; color: "#9AA3B2"; font.pixelSize: 11; height: 24
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            width: arcadeList.width; height: 38
                            text: modelData.title + (modelData.outbound ? " ↗" : "")
                            highlighted: picker.sameArcade(modelData)
                            icon.source: modelData.glyph
                            icon.color: "#E7EAF0"
                            onClicked: picker.chooseArcade(modelData)
                        }
                    }
                    Text {
                        Layout.fillWidth: true; color: "#C9BFD9"; font.pixelSize: 12; wrapMode: Text.WordWrap
                        text: picker.arcadeSummary()
                    }
                    Text {
                        visible: text.length > 0
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#9AA3B2"; font.pixelSize: 11
                        text: picker.effectSummary()
                    }
                    Text { text: "Input"; color: "#a8b8cb"; font.pixelSize: 12 }
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
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#e3aab5"; font.pixelSize: 11
                        text: picker.arcadePayload.action ? controller.actionUnavailableReason(picker.arcadeSlot()) : ""
                    }
                }
                ColumnLayout {
                    visible: picker.selectedType === "plugin"
                    Layout.fillWidth: true
                    TextField { id: providerField; Layout.fillWidth: true; placeholderText: "Provider ID" }
                    TextField { id: toolField; Layout.fillWidth: true; placeholderText: "Action/tool ID" }
                    Text { text: "Provider actions remain visible when their provider is unavailable."; color: "#8193aa"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
                Item { Layout.fillHeight: true }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#2e2737" }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            spacing: 10
            UiButton {
                text: picker.gestureId.length > 0 ? "Remove action" : "Clear slot"
                visible: !picker.linkMode && (picker.gestureId.length === 0 || picker.slotIndex >= 0)
                destructive: true
                onClicked: {
                    if (picker.gestureId.length > 0) controller.removeCenterGestureAction(picker.gestureId, picker.slotIndex)
                    else controller.setAction(picker.deckId, picker.slotIndex, {type: "none", name: "Choose action", icon: "applications-other", payload: {}})
                    picker.close()
                }
            }
            Item { Layout.fillWidth: true }
            UiButton { text: "Cancel"; onClicked: picker.close() }
            UiButton {
                objectName: "saveActionButton"
                text: picker.linkMode ? "Add to selected slot" : "Save action"
                accent: true
                enabled: (!picker.linkMode || picker.slotIndex >= 0) && (picker.selectedType === "arcade" ? !!picker.arcadePayload.app && !!picker.arcadePayload.action : picker.selectedType !== "application" || picker.selectedAppId.length > 0)
                onClicked: picker.saveAction()
            }
        }
    }
}
