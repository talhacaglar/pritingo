import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root
    color: Qt.rgba(0.04, 0.04, 0.05, 0.86)
    MouseArea { anchors.fill: parent; onClicked: backend.dismiss() }

    readonly property bool failed: backend.state === "error"
    readonly property int pages: backend.jobItems.filter(i => i.state === "done").length

    onVisibleChanged: if (visible) { mark.progress = 0; draw.restart(); confetti.burst() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 60, 460)
        height: column.implicitHeight + 64
        radius: theme.radius
        color: win.panel
        border.color: root.failed ? "#e5484d" : win.line
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 32
            spacing: 14

            // A check (or a cross) that draws itself.
            Canvas {
                id: mark
                Layout.alignment: Qt.AlignHCenter
                width: 96; height: 96
                property real progress: 0
                property color tint: root.failed ? "#e5484d" : theme.accent
                onProgressChanged: requestPaint()
                onTintChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.lineWidth = 7
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.strokeStyle = tint
                    ctx.beginPath()
                    ctx.arc(48, 48, 42, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.min(1, progress * 1.6))
                    ctx.stroke()
                    const t = Math.max(0, progress * 1.6 - 0.6)
                    if (t <= 0) return
                    ctx.beginPath()
                    if (root.failed) {
                        const k = Math.min(1, t)
                        ctx.moveTo(32, 32); ctx.lineTo(32 + 32 * k, 32 + 32 * k)
                        ctx.moveTo(64, 32); ctx.lineTo(64 - 32 * k, 32 + 32 * k)
                    } else {
                        const k = Math.min(1, t)
                        ctx.moveTo(28, 50)
                        if (k < 0.4) ctx.lineTo(28 + 14 * k / 0.4, 50 + 14 * k / 0.4)
                        else { ctx.lineTo(42, 64); ctx.lineTo(42 + 28 * (k - 0.4) / 0.6, 64 - 30 * (k - 0.4) / 0.6) }
                    }
                    ctx.stroke()
                }
                NumberAnimation { id: draw; target: mark; property: "progress"; from: 0; to: 1; duration: 900; easing.type: Easing.OutCubic }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: root.failed ? qsTr("Printing stopped") : qsTr("All done")
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.failed ? "#ffd7d7" : win.muted
                text: root.failed ? backend.step
                    : qsTr("%n page(s) printed", "", root.pages) + "  ·  "
                      + qsTr("%1 min %2 s").arg(Math.floor(backend.elapsedSeconds / 60)).arg(backend.elapsedSeconds % 60)
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                spacing: 10
                AccentButton {
                    text: root.failed ? qsTr("Close") : qsTr("Great    ␣")
                    onClicked: backend.dismiss()
                }
                Button {
                    flat: true
                    text: root.failed ? qsTr("Try again") : qsTr("Print again")
                    onClicked: { backend.dismiss(); backend.print() }
                }
            }
        }
    }

    // A small burst of paper scraps for a finished job.
    Item {
        id: confetti
        anchors.fill: parent
        function burst() {
            if (root.failed) return
            for (let i = 0; i < 36; ++i)
                piece.createObject(confetti, { startX: width / 2, startY: height / 2 - 120 })
        }
        Component {
            id: piece
            Rectangle {
                id: p
                property real startX
                property real startY
                readonly property real angle: Math.random() * Math.PI * 2
                readonly property real speed: 160 + Math.random() * 260
                width: 6 + Math.random() * 6
                height: 10 + Math.random() * 8
                radius: 1
                color: [theme.accent, "#f4f4f5", "#a1a1aa", Qt.lighter(theme.accent, 1.4)][Math.floor(Math.random() * 4)]
                x: startX; y: startY
                ParallelAnimation {
                    running: true
                    NumberAnimation { target: p; property: "x"; to: p.startX + Math.cos(p.angle) * p.speed; duration: 1200; easing.type: Easing.OutCubic }
                    SequentialAnimation {
                        NumberAnimation { target: p; property: "y"; to: p.startY + Math.sin(p.angle) * p.speed - 80; duration: 500; easing.type: Easing.OutCubic }
                        NumberAnimation { target: p; property: "y"; to: p.startY + Math.sin(p.angle) * p.speed + 260; duration: 900; easing.type: Easing.InQuad }
                    }
                    NumberAnimation { target: p; property: "rotation"; to: (Math.random() - 0.5) * 720; duration: 1400 }
                    NumberAnimation { target: p; property: "opacity"; from: 1; to: 0; duration: 1400; easing.type: Easing.InQuad }
                    onFinished: p.destroy()
                }
            }
        }
    }
}
