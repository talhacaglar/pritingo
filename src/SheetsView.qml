import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root
    property string mode: "pages"
    signal modeRequested(string mode)
    signal preview(int page)

    readonly property var selected: backend.selectedPages
    readonly property var landscape: backend.landscapePages

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Segmented {
                model: [{ label: qsTr("Pages"), value: "pages" }, { label: qsTr("Sheets"), value: "sheets" }]
                value: root.mode
                onPicked: (v) => root.modeRequested(v)
            }

            // Takes whatever room is left when the window is tiled narrow:
            // wraps to two lines, then elides, and never runs under the panel.
            Label {
                Layout.fillWidth: true
                visible: root.width > 420
                color: win.muted
                font.pixelSize: 13
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                text: root.mode === "pages"
                      ? qsTr("Click a page to leave it out · double-click to look closer")
                      : backend.options.duplex ? qsTr("How the pages land on paper") : qsTr("One page on each sheet")
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            readonly property int columns: Math.max(1, Math.floor(width / (root.mode === "pages" ? 170 : 310)))
            cellWidth: Math.floor(width / columns)
            cellHeight: root.mode === "pages" ? cellWidth * 1.42 + 34 : (cellWidth - 40) / 2 * 1.42 + 74

            model: root.mode === "pages" ? backend.pageCount : backend.sheetModel

            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight

                Loader {
                    anchors.fill: parent
                    anchors.margins: 8
                    sourceComponent: root.mode === "pages" ? pageTile : sheetCard
                    property int pageNumber: root.mode === "pages" ? index + 1 : 0
                    property var sheet: root.mode === "pages" ? null : modelData
                    property int sheetNumber: index + 1
                }
            }
        }
    }

    // A page as it will print: greyed when color is off, dimmed and crossed
    // when left out, with a badge when it will turn to fit portrait paper.
    component Thumb: Item {
        id: thumb
        property int page: 0
        property bool included: true
        property bool showNumber: true

        Rectangle {
            id: paper
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height / 1.414)
            height: width * 1.414
            color: "white"
            radius: Math.min(3, theme.radius)
            opacity: thumb.included ? 1 : 0.28
            Behavior on opacity { NumberAnimation { duration: 140 } }

            Image {
                id: image
                anchors.fill: parent
                anchors.margins: 1
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                source: thumb.page > 0
                        ? ((backend.options.color ? backend.thumbnails : backend.grayThumbnails)[thumb.page - 1] || "")
                        : ""
                // The thumbnail renders the page as authored; a landscape page
                // that will turn shows turned, so this is the sheet as printed.
                rotation: root.landscape.indexOf(thumb.page) >= 0 && backend.options.autoRotate ? 90 : 0
                // Turned, the fitted landscape page is as tall as the paper is
                // wide; growing it by the A-series ratio fills the sheet again.
                scale: rotation ? 1.414 : 1
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: image.status !== Image.Ready
                visible: running
                Material.accent: "#999"
            }
        }

        Label {
            visible: !thumb.included
            anchors.centerIn: paper
            text: "✕"
            font.pixelSize: 40
            color: "#e5484d"
        }

        Rectangle {
            visible: thumb.showNumber
            anchors.horizontalCenter: paper.horizontalCenter
            anchors.bottom: paper.bottom
            anchors.bottomMargin: 6
            width: numberLabel.implicitWidth + 14
            height: 22
            radius: 11
            color: thumb.included ? theme.accent : "#3a3a42"
            Label {
                id: numberLabel
                anchors.centerIn: parent
                text: thumb.page
                font.pixelSize: 12
                font.weight: Font.Bold
                color: thumb.included ? theme.accentForeground : "white"
            }
        }
    }

    Component {
        id: pageTile
        Item {
            readonly property int page: parent ? parent.pageNumber : 0
            readonly property bool included: root.selected.indexOf(page) >= 0

            Thumb {
                anchors.fill: parent
                anchors.bottomMargin: 26
                page: parent.page
                included: parent.included
            }
            Label {
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                color: win.muted
                font.pixelSize: 12
                text: !parent.included ? qsTr("left out · click to add back")
                    : root.landscape.indexOf(parent.page) >= 0 && backend.options.autoRotate ? qsTr("turns to fit")
                    : ""
            }
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.togglePage(parent.page)
                onDoubleClicked: root.preview(parent.page)
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    radius: theme.radius
                    border.color: parent.containsMouse ? theme.accent : "transparent"
                    border.width: 1
                }
            }
        }
    }

    Component {
        id: sheetCard
        Rectangle {
            readonly property var sheet: parent ? parent.sheet : null
            readonly property int number: parent ? parent.sheetNumber : 0
            radius: theme.radius
            color: win.panel
            border.color: win.line

            Label {
                id: sheetLabel
                x: 14; y: 10
                text: qsTr("Sheet %1").arg(number)
                font.weight: Font.DemiBold
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 38
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 100
                    spacing: 4
                    Thumb {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        page: sheet ? sheet.front : 0
                        MouseArea { anchors.fill: parent; onDoubleClicked: root.preview(parent.page) }
                    }
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("front"); color: win.muted; font.pixelSize: 12 }
                }

                ColumnLayout {
                    visible: backend.options.duplex
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 100
                    spacing: 4
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Thumb {
                            anchors.fill: parent
                            visible: sheet && sheet.back > 0
                            page: sheet ? sheet.back : 0
                            MouseArea { anchors.fill: parent; onDoubleClicked: root.preview(parent.page) }
                        }
                        Rectangle {
                            visible: sheet && sheet.back === 0
                            anchors.centerIn: parent
                            width: Math.min(parent.width, parent.height / 1.414)
                            height: width * 1.414
                            color: "transparent"
                            radius: Math.min(3, theme.radius)
                            border.color: "#3a3a42"
                            border.width: 2
                            Label {
                                anchors.centerIn: parent
                                text: qsTr("blank")
                                color: win.muted
                            }
                        }
                    }
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("back"); color: win.muted; font.pixelSize: 12 }
                }
            }
        }
    }
}
