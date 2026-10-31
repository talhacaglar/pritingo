import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Scanning from the glass: the scans so far on the left, the choices and the
// three ways out (scan more, save a PDF, print a copy) on the right.
Rectangle {
    id: root
    signal closed()
    color: "#0e0e10"

    readonly property var o: scanner.options
    readonly property bool card: o.layout === "card"
    readonly property int count: scanner.pages.length
    // Card scans come in pairs; an odd count means the back is next.
    readonly property bool backNext: card && count % 2 === 1

    // Nothing behind the view takes clicks or drops while it is open.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }
    DropArea { anchors.fill: parent }

    onVisibleChanged: if (visible && !scanner.device && !scanner.searching) scanner.findDevice()

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12
                Label {
                    text: qsTr("Scan")
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    color: win.muted
                    elide: Text.ElideRight
                    text: scanner.searching ? qsTr("Looking for the scanner…")
                        : scanner.deviceName ? scanner.deviceName
                        : ""
                }
                Button {
                    flat: true
                    text: qsTr("Back to printing") + "   Esc"
                    onClicked: root.closed()
                }
            }

            // The glass, empty or waiting: a lid-white bed with what goes on
            // it, and a light bar that sweeps while the scanner runs.
            ColumnLayout {
                anchors.centerIn: parent
                visible: root.count === 0
                spacing: 22
                width: Math.min(parent.width - 60, 420)

                Rectangle {
                    id: glass
                    Layout.alignment: Qt.AlignHCenter
                    width: 170; height: 230
                    radius: Math.min(6, theme.radius)
                    color: "#1c2230"
                    border.color: "#34405a"
                    clip: true

                    Rectangle {
                        visible: !root.card
                        x: 6; y: 6; width: 150; height: 206
                        radius: 2
                        color: "#f4f4f5"
                        Column {
                            x: 16; y: 22; spacing: 9
                            Repeater {
                                model: 9
                                Rectangle { width: index % 4 === 3 ? 60 : 112; height: 5; radius: 2; color: "#c4c4cc" }
                            }
                        }
                    }
                    Rectangle {
                        visible: root.card
                        anchors.centerIn: parent
                        width: 60; height: 94
                        radius: 6
                        color: "#9fb4c8"
                        Rectangle { x: 10; y: 10; width: 26; height: 32; radius: 3; color: "#5d7590" }
                        Column {
                            x: 10; y: 52; spacing: 6
                            Repeater { model: 4; Rectangle { width: index === 3 ? 22 : 40; height: 4; radius: 2; color: "#e8eef4" } }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 6
                        visible: scanner.busy
                        y: scanner.progress * (parent.height - height)
                        gradient: Gradient {
                            orientation: Gradient.Vertical
                            GradientStop { position: 0; color: "transparent" }
                            GradientStop { position: 0.5; color: theme.accent }
                            GradientStop { position: 1; color: "transparent" }
                        }
                        Behavior on y { NumberAnimation { duration: 300 } }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    text: scanner.busy ? qsTr("Scanning…")
                        : root.card ? qsTr("Put the card on the glass")
                        : qsTr("Put the page on the glass")
                }
                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: win.muted
                    text: root.card ? qsTr("Face down, anywhere on the glass. Close the lid and press Space.")
                        : qsTr("Face down, against the corner with the arrow. Close the lid and press Space.")
                }
            }

            GridView {
                id: grid
                visible: root.count > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: turnBanner.visible ? turnBanner.top : parent.bottom
                anchors.margins: 20
                anchors.topMargin: 16
                clip: true
                model: scanner.pages
                readonly property int columns: Math.max(1, Math.floor(width / (root.card ? 300 : 210)))
                cellWidth: Math.floor(width / columns)
                cellHeight: Math.round((cellWidth - 20) * (root.card ? 0.66 : 1.41)) + 46
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: tile
                    width: grid.cellWidth
                    height: grid.cellHeight
                    readonly property int number: index

                    HoverHandler { id: hover }

                    Rectangle {
                        id: paper
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 10
                        height: parent.height - 46
                        radius: Math.min(4, theme.radius)
                        color: "#f4f4f5"
                        border.color: hover.hovered ? theme.accent : "transparent"
                        border.width: 2

                        Image {
                            anchors.fill: parent
                            anchors.margins: 4
                            source: modelData.thumbnail
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        BusyIndicator {
                            anchors.centerIn: parent
                            running: !modelData.thumbnail
                            visible: running
                        }

                        Row {
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 6
                            spacing: 4
                            visible: hover.hovered && !scanner.busy
                            TileButton { text: "↻"; tip: qsTr("Turn"); onClicked: scanner.rotatePage(tile.number) }
                            TileButton { text: "✕"; tip: qsTr("Remove"); onClicked: scanner.removePage(tile.number) }
                        }
                    }

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: paper.bottom
                        anchors.topMargin: 8
                        color: win.muted
                        text: root.card
                            ? (tile.number % 2 === 0 ? qsTr("Card %1 · front") : qsTr("Card %1 · back")).arg(Math.floor(tile.number / 2) + 1)
                            : qsTr("Page %1").arg(tile.number + 1)
                    }
                }
            }

            // After a card's front, the one thing to do is turn it over.
            Rectangle {
                id: turnBanner
                visible: root.backNext && !scanner.busy
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20
                height: turnText.implicitHeight + 28
                radius: theme.radius
                color: Qt.rgba(1, 1, 1, 0.04)
                border.color: theme.accent
                Label {
                    id: turnText
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 18
                    wrapMode: Text.WordWrap
                    text: "⟲   " + qsTr("Now turn the card over, put it back on the glass and press Space for the back.")
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: win.line }

        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.round(Math.max(290, Math.min(380, win.width * 0.36)))
            color: win.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                ScrollView {
                    id: scroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: scroll.availableWidth
                        spacing: 18

                        Item { Layout.preferredHeight: 2 }

                        Caption { text: qsTr("On the glass") }
                        Segmented {
                            Layout.fillWidth: true
                            Layout.leftMargin: 20; Layout.rightMargin: 20
                            stretch: true
                            enabled: root.count === 0 && !scanner.busy
                            opacity: enabled ? 1 : 0.6
                            model: [{ label: qsTr("Pages"), value: "document" }, { label: qsTr("ID card"), value: "card" }]
                            value: root.o.layout
                            onPicked: (v) => scanner.setOption("layout", v)
                        }
                        Hint {
                            text: root.count > 0 ? qsTr("Clear the scans to switch.")
                                : root.card ? qsTr("Scan the front, turn the card over, scan the back. Both sides go on one A4 page at their real size, like a photocopy.")
                                : qsTr("Each scan becomes one A4 page.")
                        }

                        Caption { text: qsTr("Color") }
                        Segmented {
                            Layout.fillWidth: true
                            Layout.leftMargin: 20; Layout.rightMargin: 20
                            stretch: true
                            enabled: !scanner.busy
                            model: [{ label: qsTr("Black & white"), value: false }, { label: qsTr("Color"), value: true }]
                            value: root.o.color
                            onPicked: (v) => scanner.setOption("color", v)
                        }

                        Caption { text: qsTr("Resolution") }
                        Segmented {
                            Layout.fillWidth: true
                            Layout.leftMargin: 20; Layout.rightMargin: 20
                            stretch: true
                            enabled: !scanner.busy
                            model: [{ label: "150 dpi", value: 150 }, { label: "300 dpi", value: 300 }, { label: "600 dpi", value: 600 }]
                            value: root.o.resolution
                            onPicked: (v) => scanner.setOption("resolution", v)
                        }
                        Hint { text: qsTr("300 dpi suits documents and cards. 600 dpi is for small print and photos, and takes longer.") }

                        Button {
                            Layout.leftMargin: 14
                            visible: root.count > 0
                            enabled: !scanner.busy
                            flat: true
                            text: qsTr("Clear the scans")
                            onClicked: scanner.clear()
                        }

                        Item { Layout.preferredHeight: 10 }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: footer.implicitHeight + 32
                    color: "#141418"
                    Rectangle { width: parent.width; height: 1; color: win.line }

                    ColumnLayout {
                        id: footer
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            color: win.muted
                            wrapMode: Text.WordWrap
                            text: scanner.busy ? scanner.step + (scanner.progress > 0 && scanner.progress < 1
                                                                 ? "  ·  " + Math.round(scanner.progress * 100) + "%" : "")
                                : root.count === 0 ? qsTr("Nothing scanned yet.")
                                : root.card ? qsTr("%n card(s)", "", Math.ceil(root.count / 2))
                                : qsTr("%n page(s)", "", root.count)
                        }
                        ProgressBar {
                            Layout.fillWidth: true
                            visible: scanner.busy
                            value: scanner.progress
                            indeterminate: scanner.progress <= 0 || scanner.progress >= 1
                        }

                        AccentButton {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            enabled: !win.busy && !scanner.searching
                            font.pixelSize: 16
                            text: scanner.busy ? qsTr("Stop") + "    Esc"
                                : (!root.card ? (root.count === 0 ? qsTr("Scan") : qsTr("Scan another page"))
                                   : root.backNext ? qsTr("Scan the back") : qsTr("Scan the front")) + "    ␣"
                            onClicked: scanner.busy ? scanner.cancel() : scanner.scan()
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Button {
                                Layout.fillWidth: true
                                enabled: root.count > 0 && !scanner.busy
                                text: qsTr("Save as PDF")
                                onClicked: scanner.save()
                            }
                            Button {
                                Layout.fillWidth: true
                                enabled: root.count > 0 && !scanner.busy && !win.busy
                                text: qsTr("Print a copy")
                                onClicked: scanner.print()
                            }
                        }
                    }
                }
            }
        }
    }

    component Caption: Label {
        Layout.leftMargin: 20
        font.pixelSize: 11
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1.2
        color: win.muted
    }

    component Hint: Label {
        Layout.fillWidth: true
        Layout.leftMargin: 20; Layout.rightMargin: 20
        Layout.topMargin: -8
        wrapMode: Text.WordWrap
        font.pixelSize: 12
        color: win.muted
    }

    component TileButton: RoundButton {
        property string tip
        width: 34; height: 34
        padding: 0
        font.pixelSize: 15
        Material.background: "#24242a"
        Material.foreground: "white"
        ToolTip.visible: hovered
        ToolTip.text: tip
    }
}
