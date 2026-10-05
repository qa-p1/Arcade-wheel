import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    color: "#14171C"
    width: 540; height: 320
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 18; spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { text: "Arcade activity"; color: "#E7EAF0"; font.pixelSize: 19; Layout.fillWidth: true }
            UiButton { text: "Clear finished"; compact: true; onClicked: controller.dismissLinkJobs() }
        }
        ListView {
            id: jobs
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10
            model: controller.linkJobs
            delegate: Rectangle {
                required property var modelData
                width: jobs.width; implicitHeight: content.implicitHeight + 24
                color: "#1C2027"; radius: 12
                ColumnLayout {
                    id: content
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 12
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Image { source: "image://icons/" + encodeURIComponent("qrc:/assets/arcade/" + modelData.app + ".svg"); Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                        Text { text: modelData.title + (modelData.outbound ? " ↗" : ""); color: "#E7EAF0"; Layout.fillWidth: true; elide: Text.ElideRight }
                        UiButton { text: "Cancel"; compact: true; visible: modelData.running; onClicked: controller.cancelLinkJob(modelData.id) }
                    }
                    Text { text: modelData.preview; color: "#9AA3B2"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    ProgressBar { Layout.fillWidth: true; visible: modelData.running; indeterminate: modelData.fraction < 0; value: Math.max(0, modelData.fraction) }
                    Text { text: modelData.message; color: "#E7EAF0"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Repeater {
                        model: modelData.outputs || []
                        Text {
                            required property var modelData
                            text: modelData.path || modelData.text || JSON.stringify(modelData.data || {})
                            color: "#9AA3B2"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
