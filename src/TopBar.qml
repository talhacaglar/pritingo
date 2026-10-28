import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    signal help()
    implicitHeight: 56
    color: win.panel

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        spacing: 12

        // The mark: a sheet with a folded corner, in the accent.
        Item {
            width: 26; height: 30
            Rectangle { anchors.fill: parent; radius: Math.min(4, theme.radius); color: theme.accent }
            Rectangle {
                width: 9; height: 9; anchors.right: parent.right; anchors.top: parent.top
                color: win.panel
            }
            Repeater {
                model: 3
                Rectangle {
                    x: 6; y: 12 + index * 5; height: 2; radius: 1
                    width: index === 2 ? 9 : 14
                    color: theme.accentForeground
                    opacity: 0.8
                }
            }
        }

        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Label {
                text: backend.fileName || "Pritingo"
                font.pixelSize: 16
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                visible: backend.pageCount > 0 || backend.loading
                text: backend.loading ? qsTr("Opening…") : qsTr("%n page(s)", "", backend.pageCount)
                color: win.muted
                font.pixelSize: 12
            }
        }

        // Printer status: green when it is on the USB bus, red when it is not.
        Rectangle {
            id: chip
            Layout.preferredHeight: 32
            Layout.preferredWidth: chipRow.implicitWidth + 24
            radius: Math.min(16, theme.radius * 2)
            color: "#1f1f25"
            border.color: win.line
            readonly property color dot: !backend.printer ? "#e5484d"
                                       : backend.state === "printing" ? theme.accent
                                       : backend.printerConnected ? "#46a758" : "#e5484d"
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: chip.dot
                    SequentialAnimation on opacity {
                        running: backend.state === "printing"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 600 }
                        NumberAnimation { to: 1; duration: 600 }
                        onRunningChanged: if (!running) parent.opacity = 1
                    }
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 13
                    text: !backend.printer ? qsTr("No printer")
                        : backend.state === "printing" ? qsTr("%1 · printing").arg(backend.printer.replace(/_/g, " "))
                        : !backend.printerConnected ? qsTr("%1 · not connected").arg(backend.printer.replace(/_/g, " "))
                        : backend.printerQueue > 0 ? qsTr("%1 · %n job(s) waiting", "", backend.printerQueue).arg(backend.printer.replace(/_/g, " "))
                        : qsTr("%1 · ready").arg(backend.printer.replace(/_/g, " "))
                }
            }
        }

        ToolButton {
            text: "⋯"
            font.pixelSize: 20
            onClicked: menu.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("More")

            Menu {
                id: menu
                y: parent.height
                MenuItem { text: qsTr("Open a file…"); enabled: !win.busy; onTriggered: backend.pickFile() }
                MenuItem { text: qsTr("Close the file"); enabled: !win.busy && backend.pageCount > 0; onTriggered: backend.closeFile() }
                MenuSeparator {}
                MenuItem { text: qsTr("Two-sided test sheet"); enabled: !win.busy; onTriggered: backend.testDuplexOrientation() }
                Menu {
                    title: qsTr("Printer maintenance")
                    enabled: !win.busy
                    MenuItem { text: qsTr("Print a nozzle check"); onTriggered: backend.maintenance("nozzle") }
                    MenuItem { text: qsTr("Clean the print head"); onTriggered: backend.maintenance("clean") }
                    MenuItem { text: qsTr("Align the print head"); onTriggered: backend.maintenance("align") }
                }
                Menu {
                    title: qsTr("Language")
                    MenuItem { text: qsTr("Follow Omarchy"); checkable: true; checked: translator.overrideLanguage() === ""; onTriggered: translator.setOverride("") }
                    MenuItem { text: "Türkçe"; checkable: true; checked: translator.language === "tr" && translator.overrideLanguage() !== ""; onTriggered: translator.setOverride("tr") }
                    MenuItem { text: "English"; checkable: true; checked: translator.language === "en" && translator.overrideLanguage() !== ""; onTriggered: translator.setOverride("en") }
                }
                MenuItem {
                    text: qsTr("Sounds")
                    checkable: true
                    checked: backend.options.sounds
                    onTriggered: backend.setOption("sounds", checked)
                }
                MenuSeparator {}
                MenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: bar.help() }
            }
        }
    }
}
