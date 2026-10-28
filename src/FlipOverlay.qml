import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Between the two passes: an animated sheet turning over, and the exact steps
// for putting the printed sheets back.
Rectangle {
    id: root
    color: Qt.rgba(0.04, 0.04, 0.05, 0.9)
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    readonly property var backs: backend.jobItems.filter(i => i.side === "back")
    readonly property int sheets: backs.length
    readonly property int blank: backend.lastBlankPage

    // A gentle pulse on the whole card when it appears, so it is noticed even
    // when the window was in the background.
    onVisibleChanged: if (visible) entrance.restart()

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 760)
        height: layout.implicitHeight + 64
        radius: theme.radius
        color: win.panel
        border.color: theme.accent
        border.width: 2

        ParallelAnimation {
            id: entrance
            NumberAnimation { target: card; property: "scale"; from: 0.92; to: 1; duration: 380; easing.type: Easing.OutBack }
            NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: 250 }
        }

        RowLayout {
            id: layout
            anchors.fill: parent
            anchors.margins: 32
            spacing: 36

            // The illustration: a printed sheet flipping over to its blank back,
            // then dropping into the rear tray.
            Item {
                Layout.alignment: Qt.AlignTop
                width: 210; height: 290

                // The printer's rear tray.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: 190; height: 56
                    radius: Math.min(8, theme.radius + 4)
                    color: "#2a2a31"
                    border.color: "#3a3a42"
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: 10; width: 150; height: 6; radius: 3
                        color: "#0e0e10"
                    }
                    Rectangle {
                        anchors.right: parent.right; anchors.rightMargin: 14
                        anchors.bottom: parent.bottom; anchors.bottomMargin: 12
                        width: 10; height: 10; radius: 5
                        color: "#46a758"
                    }
                }

                Flipable {
                    id: flip
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 120; height: 168
                    y: 10

                    front: Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: "white"
                        Column {
                            x: 14; y: 18; spacing: 9
                            Repeater { model: 9; Rectangle { width: index % 4 === 3 ? 50 : 90; height: 5; radius: 2.5; color: "#b4b4bc" } }
                        }
                        Label {
                            anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.margins: 10
                            text: "1"; color: "#555"; font.pixelSize: 18; font.weight: Font.Bold
                        }
                    }
                    back: Rectangle {
                        anchors.fill: parent
                        radius: 3
                        color: "#f4f4f5"
                        border.color: theme.accent
                        border.width: 2
                        Label {
                            anchors.centerIn: parent
                            text: "2"
                            color: theme.accent
                            font.pixelSize: 44
                            font.weight: Font.Bold
                            opacity: 0.85
                        }
                    }

                    transform: Rotation {
                        id: turn
                        origin.x: flip.width / 2
                        origin.y: flip.height / 2
                        axis { x: 0; y: 1; z: 0 }
                        angle: 0
                    }

                    SequentialAnimation {
                        running: root.visible
                        loops: Animation.Infinite
                        PropertyAction { target: flip; property: "y"; value: 10 }
                        PropertyAction { target: flip; property: "opacity"; value: 1 }
                        PauseAnimation { duration: 700 }
                        NumberAnimation { target: turn; property: "angle"; from: 0; to: 180; duration: 900; easing.type: Easing.InOutCubic }
                        PauseAnimation { duration: 600 }
                        ParallelAnimation {
                            NumberAnimation { target: flip; property: "y"; to: 120; duration: 700; easing.type: Easing.InCubic }
                            NumberAnimation { target: flip; property: "opacity"; to: 0; duration: 700; easing.type: Easing.InQuad }
                        }
                        PropertyAction { target: turn; property: "angle"; value: 0 }
                        PauseAnimation { duration: 300 }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14

                Label {
                    text: qsTr("Fronts done — turn them over")
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                Step {
                    number: 1
                    text: qsTr("Take <b>all blank paper</b> out of the rear tray. If any stays in, the printer grabs it instead of your pages.")
                }
                Step {
                    number: 2
                    text: qsTr("Put back <b>%n printed sheet(s)</b>, printed side facing away from you, blank side towards you.", "", root.sheets)
                }
                Step {
                    number: 3
                    text: qsTr("Keep them in the order they came out: the sheet with page 1 goes in first.")
                }
                Step {
                    visible: root.blank > 0
                    number: 4
                    text: qsTr("Keep the sheet with page %1 out; its back stays blank.").arg(root.blank)
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: win.muted
                    font.pixelSize: 12
                    text: qsTr("If the backs come out upside down or on the wrong sheets, change “Second pass” under More settings.")
                }

                RowLayout {
                    Layout.topMargin: 6
                    spacing: 10
                    AccentButton {
                        Layout.preferredHeight: 50
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        text: qsTr("Continue with the backs    ␣")
                        onClicked: backend.continueAfterFlip()
                    }
                    Button {
                        flat: true
                        text: qsTr("Stop")
                        onClicked: cancelDialog.open()
                    }
                }
            }
        }
    }

    component Step: RowLayout {
        id: step
        property int number
        property alias text: stepText.text
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 26; height: 26; radius: 13
            color: theme.accent
            Label {
                anchors.centerIn: parent
                text: step.number
                color: theme.accentForeground
                font.weight: Font.Bold
            }
        }
        Label {
            id: stepText
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            font.pixelSize: 15
        }
    }
}
