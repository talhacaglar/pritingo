import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    signal closed()
    color: Qt.rgba(0.04, 0.04, 0.05, 0.9)
    MouseArea { anchors.fill: parent; onClicked: root.closed() }

    readonly property var keys: [
        ["O", qsTr("Open a file")],
        ["Space", qsTr("Print, or continue with the backs")],
        ["D", qsTr("One-sided / two-sided")],
        ["C", qsTr("Black & white / color")],
        ["V", qsTr("Pages / sheets view")],
        ["A", qsTr("Select every page")],
        ["+  −", qsTr("More / fewer copies")],
        ["W", qsTr("Close the file")],
        ["Esc", qsTr("Stop printing, close a panel")],
        ["?", qsTr("Show these shortcuts")],
        ["Q", qsTr("Quit")]
    ]

    Rectangle {
        anchors.centerIn: parent
        width: 460
        height: grid.implicitHeight + 90
        radius: theme.radius
        color: win.panel
        border.color: win.line

        Label {
            x: 28; y: 22
            text: qsTr("Keyboard shortcuts")
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }

        GridLayout {
            id: grid
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            anchors.topMargin: 64
            columns: 2
            rowSpacing: 10
            columnSpacing: 18
            Repeater {
                model: root.keys.length * 2
                delegate: Item {
                    readonly property var entry: root.keys[Math.floor(index / 2)]
                    implicitWidth: index % 2 ? desc.implicitWidth : 70
                    implicitHeight: 28
                    Layout.fillWidth: index % 2 === 1
                    Rectangle {
                        visible: index % 2 === 0
                        height: 26
                        width: keyLabel.implicitWidth + 16
                        radius: Math.min(4, theme.radius)
                        color: "#24242a"
                        border.color: "#3a3a42"
                        Label { id: keyLabel; anchors.centerIn: parent; text: entry[0]; font.family: "monospace"; font.pixelSize: 13 }
                    }
                    Label {
                        id: desc
                        visible: index % 2 === 1
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry[1]
                    }
                }
            }
        }
    }
}
