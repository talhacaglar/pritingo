import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    property bool hovering: false

    Rectangle {
        anchors.fill: parent
        anchors.margins: 28
        radius: theme.radius
        color: root.hovering ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
        border.width: 2
        border.color: root.hovering ? theme.accent : win.line

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 18

            // A small stack of sheets that lifts when a file hovers over it.
            Item {
                Layout.alignment: Qt.AlignHCenter
                width: 120; height: 130
                Repeater {
                    model: 3
                    Rectangle {
                        width: 84; height: 108
                        x: 18 + (index - 1) * (root.hovering ? 16 : 8)
                        y: 12 + (2 - index) * (root.hovering ? -6 : 0)
                        rotation: (index - 1) * (root.hovering ? 9 : 4)
                        radius: Math.min(4, theme.radius)
                        color: index === 2 ? "#f4f4f5" : index === 1 ? "#d4d4d8" : "#a1a1aa"
                        Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                        Behavior on rotation { NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                        Column {
                            visible: index === 2
                            x: 12; y: 16; spacing: 7
                            Repeater {
                                model: 6
                                Rectangle { width: index === 5 ? 34 : 58; height: 4; radius: 2; color: "#c4c4cc" }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: backend.loading ? qsTr("Opening…") : qsTr("Drop a file here")
                font.pixelSize: 24
                font.weight: Font.DemiBold
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                horizontalAlignment: Text.AlignHCenter
                color: win.muted
                text: qsTr("PDF, Word, Excel, PowerPoint, LibreOffice documents and pictures")
            }
            AccentButton {
                Layout.alignment: Qt.AlignHCenter
                enabled: !backend.loading
                text: qsTr("Choose a file  (O)")
                onClicked: backend.pickFile()
            }
            Button {
                Layout.alignment: Qt.AlignHCenter
                flat: true
                enabled: !backend.loading
                text: qsTr("Scan from the glass  (S)")
                onClicked: win.scanOpen = true
            }
            BusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: backend.loading
                running: visible
            }
        }
    }
}
