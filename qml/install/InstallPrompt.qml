pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Arcade

// The first-run install / update prompt (STANDARD §5.1): the kit dialog with
// Link's shared strings, options and button order. src/install/AppInstaller.cpp creates it
// only while it asks, then reads `clicked` and `checks`.
Window {
    id: root
    required property string heading
    required property var paragraphs   // body, then notes
    required property var options      // [{ label, checked }]
    required property var buttons      // the main action first
    property int clicked: -1
    property var checks: []
    signal finished()

    title: "Install — Arcade Wheel"
    flags: Qt.Dialog
    width: 460
    minimumWidth: width; maximumWidth: width
    // A fixed-size window that follows its content. Binding the limits to
    // `height` would pin it: the window clamps a taller height to the old
    // maximum. Raise the limit first when growing, lower it last when shrinking.
    function fit() {
        const h = Math.ceil(body.implicitHeight) + 44;
        if (h >= maximumHeight) { maximumHeight = h; minimumHeight = h; }
        else { minimumHeight = h; maximumHeight = h; }
        height = h;
    }
    color: Theme.panel
    visible: true
    // The body is built once Wheel's accent is set, so its buttons open in
    // amber instead of fading from the kit's default accent.
    Component.onCompleted: {
        Theme.appId = "arcade.wheel";
        checks = options.map(option => !!option.checked);
        body.active = true;
        fit();
    }
    onClosing: finished()
    // Runs before Wheel's settings load: follow the desktop's colour scheme,
    // or the palette where the platform does not report one.
    SystemPalette { id: systemPalette; colorGroup: SystemPalette.Active }
    Binding {
        target: Theme; property: "mode"
        value: Application.styleHints.colorScheme === Qt.ColorScheme.Dark
            || (Application.styleHints.colorScheme === Qt.ColorScheme.Unknown && systemPalette.window.hslLightness < 0.5) ? "dark" : "light"
    }

    Loader {
        id: body
        x: 22; y: 22
        width: parent.width - 44
        active: false
        onImplicitHeightChanged: root.fit()
        sourceComponent: Column {
            id: content
            width: body.width
            spacing: Theme.space10
            Row {
                spacing: Theme.space12
                GlyphTile { appId: "arcade.wheel"; size: Theme.tile }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    width: content.width - Theme.tile - Theme.space12
                    text: root.heading
                    kind: "titleSm"
                    wrapMode: Text.Wrap
                }
            }
            Repeater {
                model: root.paragraphs
                delegate: DialogBody {
                    required property string modelData
                    width: content.width
                    text: modelData
                }
            }
            Column {
                visible: root.options.length > 0
                width: content.width
                topPadding: Theme.space4
                Repeater {
                    model: root.options
                    delegate: CheckBox {
                        required property var modelData
                        required property int index
                        objectName: "installOption-" + index
                        text: modelData.label
                        checked: !!modelData.checked
                        onToggled: { const next = root.checks.slice(); next[index] = checked; root.checks = next; }
                    }
                }
            }
            // The main action is rightmost and the default, as in every Arcade prompt.
            Flow {
                width: content.width
                topPadding: Theme.space10
                layoutDirection: Qt.RightToLeft
                spacing: Theme.space8
                Repeater {
                    model: root.buttons
                    delegate: Button {
                        required property string modelData
                        required property int index
                        objectName: "installButton-" + index
                        text: modelData
                        variant: index === 0 ? "primary" : "default"
                        onClicked: { root.clicked = index; root.close(); }
                    }
                }
            }
        }
    }
    Shortcut { sequence: "Return"; onActivated: { root.clicked = 0; root.close(); } }
    Shortcut { sequence: "Esc"; onActivated: root.close() }
}
