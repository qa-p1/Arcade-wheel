pragma ComponentBehavior: Bound
import QtQuick
import Arcade

// Center gestures: a click, double-click, triple-click or long press in the
// middle of the open wheel runs a saved group of actions.
Column {
    id: page
    objectName: "centerGesturesPage"
    required property WheelController controller
    property string gestureId: "singleClick"
    readonly property var group: controller.config.centerGestures[gestureId]
    readonly property var actions: group.actions || []
    readonly property string triggerKey: controller.shortcutForKit(controller.config.trigger.shortcut)
    signal editAction(string gesture, int slot, var action)
    spacing: Theme.space32

    ListSection {
        width: parent.width
        title: "Gesture"
        note: "Hold " + page.triggerKey + " and use the middle of the wheel to run a group of apps or actions. Each gesture has its own setup."
        ListRow {
            title: "Edit the setup for"
            SegmentedControl {
                objectName: "gesturePicker"
                options: [{ value: "singleClick", label: "Click" }, { value: "doubleClick", label: "Double-click" },
                          { value: "tripleClick", label: "Triple-click" }, { value: "longPress", label: "Long press" }]
                value: page.gestureId
                onActivated: value => page.gestureId = value
            }
        }
        ListRow {
            title: "Setup name"
            TextField {
                width: 220
                text: page.group.name || ""
                placeholderText: "Setup name"
                onEditingFinished: page.controller.setCenterGesture(page.gestureId, text, page.group.enabled)
            }
        }
        SwitchRow {
            objectName: "gestureEnabled"
            title: "Run this setup"
            subtitle: "Off keeps the actions but ignores the gesture."
            checked: !!page.group.enabled
            onToggled: on => page.controller.setCenterGesture(page.gestureId, page.group.name, on)
        }
    }
    ListSection {
        width: parent.width
        title: "Actions"
        meta: page.actions.length ? page.actions.length + " of 16" : ""
        note: page.actions.length ? "They run together, in this order. The wheel closes as soon as the setup runs."
                                  : "Add the apps you want in this setup: a browser, a terminal, an editor or any other action."
        Repeater {
            model: page.actions
            delegate: ListRow {
                id: gestureRow
                required property var modelData
                required property int index
                readonly property string unavailable: page.controller.actionUnavailableReason(modelData)
                width: parent.width
                minimumHeight: Theme.rowDialog
                title: modelData.name
                subtitle: unavailable || modelData.type
                leading: Component {
                    Image {
                        width: Theme.tileSm; height: Theme.tileSm
                        sourceSize: Qt.size(64, 64)
                        fillMode: Image.PreserveAspectFit
                        source: "image://icons/" + encodeURIComponent(gestureRow.modelData.icon || "applications-other") + "?v=" + page.controller.iconGeneration
                    }
                }
                Button { text: "Edit"; small: true; onClicked: page.editAction(page.gestureId, gestureRow.index, gestureRow.modelData) }
                Button { iconName: "chevronDown"; rotation: 180; small: true; tooltip: "Move earlier"; enabled: gestureRow.index > 0; onClicked: page.controller.moveCenterGestureAction(page.gestureId, gestureRow.index, gestureRow.index - 1) }
                Button { iconName: "chevronDown"; small: true; tooltip: "Move later"; enabled: gestureRow.index + 1 < page.actions.length; onClicked: page.controller.moveCenterGestureAction(page.gestureId, gestureRow.index, gestureRow.index + 1) }
                Button { iconName: "trash"; small: true; variant: "danger"; tooltip: "Remove"; onClicked: page.controller.removeCenterGestureAction(page.gestureId, gestureRow.index) }
            }
        }
        ListRow {
            title: page.actions.length ? "Add another" : "Add the first action"
            Button {
                objectName: "addGestureAction"
                text: "Add app or action"; iconName: "plus"; variant: "primary"
                enabled: page.actions.length < 16
                onClicked: page.editAction(page.gestureId, -1, {})
            }
        }
    }
    ListSection {
        width: parent.width
        title: "Timing"
        note: "Releasing " + page.triggerKey + " after a setup runs never launches anything twice. Move out of the middle to abandon a gesture. Without a click setup, a click closes the wheel."
        WheelSlider {
            controller: page.controller; section: "centerGestures"; settingKey: "clickIntervalMs"
            title: "Time between clicks"
            subtitle: "A click waits this long when double- or triple-click is on; a double-click waits only when triple-click is on. Release the trigger to run a waiting setup at once."
            from: 160; to: 500; stepSize: 10; unit: "ms"
        }
        WheelSlider {
            controller: page.controller; section: "centerGestures"; settingKey: "longPressMs"
            title: "Long press duration"
            subtitle: "Hold the left mouse button in the middle for this long."
            from: 250; to: 1500; stepSize: 50; unit: "ms"
        }
    }
}
