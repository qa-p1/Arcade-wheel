import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ScrollView {
    id: page
    objectName: "centerGesturesPage"
    clip: true
    property string gestureId: "singleClick"
    readonly property var group: controller.config.centerGestures[gestureId]
    readonly property var actions: group.actions || []
    signal editAction(string gesture, int slot, var action)
    contentWidth: availableWidth

    ColumnLayout {
        width: page.availableWidth - 60
        x: 30
        y: 8
        spacing: 18
        Text {
            text: "Your workspace, in a click"
            color: "#e8e3ef"; font.pixelSize: 19; font.weight: Font.Medium
        }
        Text {
            text: "Hold " + controller.config.trigger.shortcut + " and use the center of the wheel to run a group of apps or actions. Each gesture has its own saved setup."
            color: "#91889f"; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        RowLayout {
            spacing: 8
            Repeater {
                model: [
                    {key: "singleClick", title: "Single-click"},
                    {key: "doubleClick", title: "Double-click"},
                    {key: "tripleClick", title: "Triple-click"},
                    {key: "longPress", title: "Long press"}
                ]
                delegate: UiButton {
                    required property var modelData
                    text: modelData.title
                    accent: page.gestureId === modelData.key
                    onClicked: page.gestureId = modelData.key
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: groupContent.implicitHeight + 36
            radius: 14; color: "#19171f"; border.color: "#302b3c"
            ColumnLayout {
                id: groupContent
                anchors.left: parent.left; anchors.right: parent.right
                anchors.top: parent.top; anchors.margins: 18
                spacing: 14
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        text: page.group.name || ""
                        placeholderText: "Setup name"
                        onEditingFinished: controller.setCenterGesture(page.gestureId, text, page.group.enabled)
                    }
                    Switch {
                        text: "Enabled"
                        checked: !!page.group.enabled
                        onToggled: controller.setCenterGesture(page.gestureId, page.group.name, checked)
                    }
                }
                Text {
                    text: page.actions.length ? page.actions.length + " actions · launched together, in this order" : "Add the apps you want in this setup — a browser, terminal, editor, or any other action."
                    color: "#938a9f"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
                Repeater {
                    model: page.actions
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true; implicitHeight: 64
                        radius: 9; color: "#22202b"
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 10
                            Image {
                                source: "image://icons/" + encodeURIComponent(modelData.icon || "applications-other")
                                sourceSize: Qt.size(64,64)
                                Layout.preferredWidth: 30; Layout.preferredHeight: 30
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 3
                                Text { text: modelData.name; color: "#e4deeb"; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                                Text {
                                    readonly property string unavailable: controller.actionUnavailableReason(modelData)
                                    text: unavailable || modelData.type
                                    color: unavailable ? "#dfa0a9" : "#8f849f"
                                    font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight
                                }
                            }
                            UiButton { text: "Edit"; compact: true; onClicked: page.editAction(page.gestureId, index, modelData) }
                            UiButton { text: "↑"; compact: true; enabled: index > 0; onClicked: controller.moveCenterGestureAction(page.gestureId, index, index-1) }
                            UiButton { text: "↓"; compact: true; enabled: index+1 < page.actions.length; onClicked: controller.moveCenterGestureAction(page.gestureId, index, index+1) }
                            UiButton { text: "Remove"; compact: true; onClicked: controller.removeCenterGestureAction(page.gestureId, index) }
                        }
                    }
                }
                UiButton {
                    text: "+ Add app or action"; accent: true
                    enabled: page.actions.length < 16
                    onClicked: page.editAction(page.gestureId, -1, {})
                }
            }
        }
        Text {
            text: "The wheel closes as soon as the setup runs. Releasing " + controller.config.trigger.shortcut + " afterward won't launch anything twice. Move out of the center to abandon a gesture. Without a single-click setup, a single click closes the wheel."
            color: "#91889f"; font.pixelSize: 12; wrapMode: Text.WordWrap; Layout.fillWidth: true
        }
        SettingSlider {
            Layout.fillWidth: true
            title: "Time between clicks"
            detail: "Single-click waits this long when double- or triple-click is enabled; double-click waits only when triple-click is enabled. Release the trigger after clicking to run the pending setup immediately."
            section: "centerGestures"; settingKey: "clickIntervalMs"
            fromValue: 160; toValue: 500; stepValue: 10; unit: " ms"
        }
        SettingSlider {
            Layout.fillWidth: true
            title: "Long press duration"
            detail: "Hold the left mouse button in the center for this long."
            section: "centerGestures"; settingKey: "longPressMs"
            fromValue: 250; toValue: 1500; stepValue: 50; unit: " ms"
        }
        Item { Layout.preferredHeight: 30 }
    }
}
