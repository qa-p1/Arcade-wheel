import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: row
    property string title: ""
    property string detail: ""
    property string section: "appearance"
    property string settingKey: ""
    property real fromValue: 0
    property real toValue: 100
    property real stepValue: 1
    property string unit: ""
    spacing: 4

    RowLayout {
        Layout.fillWidth: true
        Text { text: row.title; color: "#e7edf8"; font.pixelSize: 14; font.weight: Font.Medium; Layout.fillWidth: true }
        Text { text: Number((Math.round(slider.value / row.stepValue) * row.stepValue).toFixed(2)) + row.unit; color: "#a9bedb"; font.pixelSize: 13 }
    }
    Text { text: row.detail; color: "#8291a5"; font.pixelSize: 12; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    Slider {
        id: slider
        Layout.fillWidth: true
        from: row.fromValue
        to: row.toValue
        stepSize: row.stepValue
        value: Number(controller.config[row.section][row.settingKey])
        onMoved: controller.updateSetting(row.section, row.settingKey, Math.round(value / row.stepValue) * row.stepValue)
    }
}
