import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string title: ""
    property string detail: ""
    property string section: "behaviour"
    property string settingKey: ""
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 3
        Text { text: row.title; color: "#e7edf8"; font.pixelSize: 14; font.weight: Font.Medium }
        Text { text: row.detail; color: "#8291a5"; font.pixelSize: 12; visible: text.length > 0; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    Switch {
        checked: !!controller.config[row.section][row.settingKey]
        onToggled: controller.updateSetting(row.section, row.settingKey, checked)
    }
}
