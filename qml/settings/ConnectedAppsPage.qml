pragma ComponentBehavior: Bound
import QtQuick
import Arcade

// Connected apps on the shared Link page layout: the master switch, one row
// per Arcade app (use it, or get it) and the Link diagnostics.
Column {
    id: page
    required property WheelController controller
    spacing: Theme.space32

    ListSection {
        width: parent.width
        title: "Arcade Link"
        SwitchRow {
            objectName: "linkMaster"
            title: "Work with other Arcade apps"
            subtitle: "Find their actions, and let them add actions to your wheel."
            checked: !!page.controller.config.link.enabled
            onToggled: on => page.controller.updateSetting("link", "enabled", on)
        }
    }
    ListSection {
        width: parent.width
        title: "Apps"
        note: page.controller.connectedApps.length ? "" : "Installed Arcade apps appear here."
        Repeater {
            model: page.controller.connectedApps
            delegate: ListRow {
                id: peerRow
                required property var modelData
                objectName: "peer-" + modelData.id
                width: parent.width
                title: modelData.name
                subtitle: modelData.installed ? modelData.state : modelData.pitch
                leading: Component { GlyphTile { appId: peerRow.modelData.id; size: Theme.tile } }
                Switch {
                    objectName: "peerToggle-" + peerRow.modelData.id
                    visible: peerRow.modelData.installed && peerRow.modelData.id !== "arcade.tools"
                    checked: peerRow.modelData.enabled
                    enabled: !!page.controller.config.link.enabled
                    onToggled: page.controller.setPeerEnabled(peerRow.modelData.id, checked)
                }
                LinkButton {
                    visible: !peerRow.modelData.installed
                    text: "Install"
                    onClicked: page.controller.getArcadeApp(peerRow.modelData.id)
                }
            }
        }
    }
    ListSection {
        id: diagnostics
        objectName: "linkDiagnostics"
        width: parent.width
        title: "Diagnostics"
        property bool expanded: false
        ListRow {
            title: diagnostics.expanded ? "Hide Link details" : "Show Link details"
            subtitle: "Where Arcade Wheel listens, and the last error from each app."
            Button {
                objectName: "linkDiagnosticsToggle"
                text: diagnostics.expanded ? "Hide" : "Show"
                onClicked: diagnostics.expanded = !diagnostics.expanded
            }
        }
        ListRow { visible: diagnostics.expanded; title: "Registry"; subtitle: page.controller.linkDiagnostics.registry }
        ListRow { visible: diagnostics.expanded; title: "Wheel endpoint"; subtitle: page.controller.linkDiagnostics.endpoint }
        ListRow { visible: diagnostics.expanded; title: "Last error"; subtitle: page.controller.linkDiagnostics.lastError || "None" }
        Repeater {
            model: diagnostics.expanded ? page.controller.connectedApps : []
            delegate: ListRow {
                required property var modelData
                width: parent.width
                title: modelData.name
                subtitle: modelData.endpoint + " · Last error: " + (modelData.lastError || "None")
            }
        }
    }
}
