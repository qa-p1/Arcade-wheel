import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    ColumnLayout {
        width: Math.max(0, Math.min(760, page.availableWidth - 60))
        x: 30; y: 18; spacing: 14
        SettingToggle {
            objectName: "linkMaster"
            Layout.fillWidth: true
            title: "Connect with other Arcade apps"
            detail: "Discover actions and let other apps offer actions to your Wheel."
            section: "link"; settingKey: "enabled"
        }
        Repeater {
            model: controller.connectedApps
            delegate: Rectangle {
                required property var modelData
                objectName: "peer-" + modelData.id
                Layout.fillWidth: true
                implicitHeight: row.implicitHeight + 28
                radius: 12; color: "#1C2027"; border.color: "#2A2F38"
                ColumnLayout {
                    id: row
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Image {
                            source: "image://icons/" + encodeURIComponent(modelData.glyph)
                            Layout.preferredWidth: 20; Layout.preferredHeight: 20
                            sourceSize: Qt.size(40, 40)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 3
                            Text { text: modelData.name; color: "#E7EAF0"; font.pixelSize: 14 }
                            Text { text: modelData.state; color: "#9AA3B2"; font.pixelSize: 11 }
                        }
                        Switch {
                            objectName: "peerToggle-" + modelData.id
                            visible: modelData.installed && modelData.id !== "arcade.tools"
                            text: "Use with Arcade Wheel"
                            checked: modelData.enabled
                            enabled: controller.config.link.enabled
                            onToggled: controller.setPeerEnabled(modelData.id, checked)
                        }
                    }
                    RowLayout {
                        visible: !modelData.installed
                        Layout.fillWidth: true
                        Text { text: modelData.pitch; color: "#9AA3B2"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        UiButton { text: "Get"; compact: true; onClicked: controller.getArcadeApp(modelData.id) }
                    }
                }
            }
        }
        UiButton {
            id: diagnostics
            objectName: "linkDiagnosticsToggle"
            property bool expanded: false
            text: (expanded ? "▾ " : "▸ ") + "Diagnostics"
            onClicked: expanded = !expanded
        }
        ColumnLayout {
            visible: diagnostics.expanded
            Layout.fillWidth: true; spacing: 8
            Text { text: "Registry: " + controller.linkDiagnostics.registry; color: "#9AA3B2"; font.pixelSize: 11; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Text { text: "Wheel endpoint: " + controller.linkDiagnostics.endpoint; color: "#9AA3B2"; font.pixelSize: 11 }
            Text { text: "Last error: " + (controller.linkDiagnostics.lastError || "None"); color: "#9AA3B2"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Repeater {
                model: controller.connectedApps
                Text {
                    required property var modelData
                    text: modelData.name + ": " + modelData.endpoint + " · Last error: " + (modelData.lastError || "None")
                    color: "#9AA3B2"; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
        }
        Item { Layout.preferredHeight: 24 }
    }
}
