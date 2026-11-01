pragma ComponentBehavior: Bound
import QtQuick
import Arcade

// "Arcade activity — Arcade Wheel": jobs the wheel started in other apps.
Rectangle {
    id: root
    required property WheelController controller
    color: Theme.bg
    width: 540; height: 320
    Binding { target: Theme; property: "appId"; value: "arcade.wheel" }
    Binding { target: Theme; property: "mode"; value: root.controller.config.general.theme || "system" }

    PageHead {
        id: head
        x: Theme.space20; y: Theme.space20
        width: parent.width - Theme.space20 * 2
        title: "Arcade activity"
        Button { text: "Clear finished"; small: true; onClicked: root.controller.dismissLinkJobs() }
    }
    ListView {
        id: jobs
        x: Theme.space20
        anchors.top: head.bottom; anchors.topMargin: Theme.space16
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.space20
        width: parent.width - Theme.space20 * 2
        clip: true
        spacing: Theme.space10
        model: root.controller.linkJobs
        delegate: Surface {
            id: job
            required property var modelData
            width: jobs.width
            height: content.implicitHeight + Theme.space12 * 2
            radius: Theme.radiusMd
            Column {
                id: content
                x: Theme.space12; y: Theme.space12
                width: parent.width - Theme.space12 * 2
                spacing: Theme.space6
                Item {
                    width: parent.width
                    height: Math.max(cancel.visible ? cancel.height : 0, jobTitle.implicitHeight)
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.space8
                        GlyphTile { anchors.verticalCenter: parent.verticalCenter; appId: job.modelData.app; size: Theme.tileSm }
                        Label { id: jobTitle; anchors.verticalCenter: parent.verticalCenter; text: job.modelData.title + (job.modelData.outbound ? " ↗" : ""); kind: "heading"; elide: Text.ElideRight }
                    }
                    Button { id: cancel; anchors.right: parent.right; text: "Cancel"; small: true; visible: job.modelData.running; onClicked: root.controller.cancelLinkJob(job.modelData.id) }
                }
                Label { width: parent.width; text: job.modelData.preview; kind: "small"; color: Theme.muted; wrapMode: Text.Wrap; visible: text.length > 0 }
                ProgressBar { width: parent.width; visible: job.modelData.running; indeterminate: job.modelData.fraction < 0; value: Math.max(0, job.modelData.fraction) }
                Label { width: parent.width; text: job.modelData.message; kind: "small"; wrapMode: Text.Wrap; visible: text.length > 0 }
                Repeater {
                    model: job.modelData.outputs || []
                    delegate: Label {
                        required property var modelData
                        width: content.width
                        text: modelData.path || modelData.text || JSON.stringify(modelData.data || {})
                        kind: "caption"; color: Theme.muted; wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
