import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A row of mutually exclusive choices with a sliding highlight.
Rectangle {
    id: root
    property var model: []
    property var value
    property bool stretch: false
    signal picked(var value)

    implicitHeight: 36
    implicitWidth: row.implicitWidth + 6
    radius: Math.min(18, theme.radius * 2)
    color: "#1f1f25"
    border.color: win.line

    readonly property int currentIndex: {
        for (let i = 0; i < model.length; ++i)
            if (model[i].value === value)
                return i
        return -1
    }

    Rectangle {
        id: highlight
        visible: root.currentIndex >= 0
        y: 3
        height: root.height - 6
        radius: Math.max(0, root.radius - 3)
        color: theme.accent
        readonly property Item target: repeater.count > root.currentIndex && root.currentIndex >= 0 ? repeater.itemAt(root.currentIndex) : null
        x: target ? row.x + target.x : 3
        width: target ? target.width : 0
        Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    }

    Row {
        id: row
        x: 3
        anchors.verticalCenter: parent.verticalCenter
        Repeater {
            id: repeater
            model: root.model
            delegate: Item {
                width: root.stretch ? (root.width - 6) / root.model.length : label.implicitWidth + 28
                height: root.height - 6
                Label {
                    id: label
                    anchors.centerIn: parent
                    text: modelData.label
                    font.pixelSize: 13
                    font.weight: index === root.currentIndex ? Font.DemiBold : Font.Normal
                    color: index === root.currentIndex ? theme.accentForeground : "#d4d4d8"
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, parent.width - 8)
                    horizontalAlignment: Text.AlignHCenter
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: root.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(modelData.value)
                }
            }
        }
    }
}
