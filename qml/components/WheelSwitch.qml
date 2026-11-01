import QtQuick
import Arcade

// A kit switch row bound to one on/off setting.
SwitchRow {
    id: row
    required property WheelController controller
    required property string section
    required property string settingKey
    checked: !!row.controller.config[row.section][row.settingKey]
    onToggled: on => row.controller.updateSetting(row.section, row.settingKey, on)
}
