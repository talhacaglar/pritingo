import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    color: Qt.rgba(0.04, 0.04, 0.05, 0.86)

    // Swallow clicks so the window behind stays still while printing.
    MouseArea { anchors.fill: parent; hoverEnabled: true }

    readonly property var items: backend.jobItems
    readonly property int done: items.filter(i => i.state === "done").length
    readonly property bool duplex: items.some(i => i.side === "back")
    readonly property var current: backend.currentItem >= 0 ? items[backend.currentItem] : null

    function clock(seconds) {
        const m = Math.floor(seconds / 60), s = seconds % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 600)
        height: content.implicitHeight + 56
        radius: theme.radius
        color: win.panel
        border.color: win.line

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 28
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                // Progress ring with the sheet being printed in the middle.
                Item {
                    width: 132; height: 132
                    Canvas {
                        id: ring
                        anchors.fill: parent
                        property real value: backend.progress
                        property color accent: theme.accent
                        Behavior on value { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
                        onValueChanged: requestPaint()
                        onAccentChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.lineWidth = 9
                            ctx.lineCap = "round"
                            ctx.strokeStyle = "#2a2a31"
                            ctx.beginPath()
                            ctx.arc(width / 2, height / 2, width / 2 - 8, 0, Math.PI * 2)
                            ctx.stroke()
                            ctx.strokeStyle = accent
                            ctx.beginPath()
                            ctx.arc(width / 2, height / 2, width / 2 - 8, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * value)
                            ctx.stroke()
                        }
                    }
                    // A page sliding down into the ring, over and over.
                    Item {
                        anchors.centerIn: parent
                        width: 40; height: 56
                        clip: true
                        Rectangle {
                            id: sheet
                            width: 40; height: 56
                            radius: 2
                            color: "white"
                            Column {
                                x: 7; y: 9; spacing: 5
                                Repeater { model: 6; Rectangle { width: index === 5 ? 14 : 26; height: 3; radius: 1.5; color: "#c4c4cc" } }
                            }
                            SequentialAnimation on y {
                                running: root.visible
                                loops: Animation.Infinite
                                NumberAnimation { from: -56; to: 0; duration: 900; easing.type: Easing.OutCubic }
                                PauseAnimation { duration: 900 }
                                NumberAnimation { from: 0; to: 56; duration: 700; easing.type: Easing.InCubic }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Label {
                        text: !root.duplex ? qsTr("Printing")
                            : backend.pass === 1 ? qsTr("Printing the fronts") : qsTr("Printing the backs")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Label {
                        text: qsTr("%1 of %2 pages").arg(root.done).arg(root.items.length)
                        color: win.muted
                    }
                    Label {
                        Layout.fillWidth: true
                        text: backend.step
                        elide: Text.ElideRight
                    }
                    Label {
                        text: qsTr("Elapsed %1").arg(root.clock(backend.elapsedSeconds))
                        color: win.muted
                        font.pixelSize: 12
                    }
                }
            }

            // Every page of the job as a chip: done, in the printer, or waiting.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.items
                    Rectangle {
                        readonly property bool active: modelData.state === "active"
                        width: chipText.implicitWidth + 18
                        height: 26
                        radius: 13
                        color: modelData.state === "done" ? Qt.darker(theme.accent, 1.6)
                             : active ? theme.accent : "#24242a"
                        border.color: modelData.side === "back" && modelData.state !== "active" ? "#4a4a55" : "transparent"
                        Label {
                            id: chipText
                            anchors.centerIn: parent
                            font.pixelSize: 12
                            font.weight: parent.active ? Font.Bold : Font.Normal
                            color: parent.active ? theme.accentForeground : modelData.state === "done" ? "white" : win.muted
                            text: (modelData.state === "done" ? "✓ " : "") + modelData.page
                                  + (modelData.side === "back" ? qsTr(" back") : "")
                        }
                        SequentialAnimation on scale {
                            running: parent.active
                            loops: Animation.Infinite
                            NumberAnimation { to: 1.08; duration: 500 }
                            NumberAnimation { to: 1; duration: 500 }
                            onRunningChanged: if (!running) parent.scale = 1
                        }
                    }
                }
            }

            // The driver cannot read paper-out, so a long wait is the only sign.
            Rectangle {
                visible: backend.stalled
                Layout.fillWidth: true
                Layout.preferredHeight: stallText.implicitHeight + 24
                radius: theme.radius
                color: "#3a2e14"
                border.color: "#f5a524"
                Label {
                    id: stallText
                    anchors.fill: parent
                    anchors.margins: 12
                    wrapMode: Text.WordWrap
                    color: "#ffe6b3"
                    text: qsTr("The printer has been quiet for a while. Is there paper in the rear tray? If a light is blinking, press the Resume/Stop button once. Printing carries on by itself.")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: win.muted
                    text: qsTr("Each page goes out on its own; this printer drops pages from longer jobs.")
                }
                Button {
                    text: qsTr("Stop  (Esc)")
                    flat: true
                    onClicked: cancelDialog.open()
                }
            }
        }
    }
}
