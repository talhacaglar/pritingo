import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The one filled button per screen, in the theme accent. Material's own
// highlighted button does not take the accent in every Qt release.
Button {
    id: control
    font.weight: Font.DemiBold
    leftPadding: 22
    rightPadding: 22

    contentItem: Label {
        text: control.text
        font: control.font
        color: control.enabled ? theme.accentForeground : "#8b8b95"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: 44
        implicitWidth: 120
        radius: theme.radius
        color: !control.enabled ? "#2a2a31"
             : control.down ? Qt.darker(theme.accent, 1.2)
             : control.hovered ? Qt.lighter(theme.accent, 1.1) : theme.accent
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
