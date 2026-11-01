import QtQuick
import Arcade

// A kit slider row bound to one setting: shows the saved value and writes
// each step back through the controller (saved together a moment later).
SliderRow {
    id: row
    required property WheelController controller
    required property string section
    required property string settingKey
    property int decimals: 0
    function formatValue(v: real): string { return Number(v).toFixed(row.decimals) }
    value: Number(row.controller.config[row.section][row.settingKey])
    onMoved: v => row.controller.updateSetting(row.section, row.settingKey, Math.round(v / row.stepSize) * row.stepSize)
}
