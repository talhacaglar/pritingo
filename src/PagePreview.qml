import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A closer look at one sheet as it will print, rendered on demand. It sits
// beside the settings, so a change there shows here at once.
Rectangle {
    id: root
    property int page: 0
    property string source: ""
    signal closed()

    visible: page > 0
    color: Qt.rgba(0.04, 0.04, 0.05, 0.92)
    function load() { source = ""; if (page > 0) backend.requestPreview(page) }
    onPageChanged: load()

    Connections {
        target: backend
        function onPreviewReady(page, url) { if (page === root.page) root.source = url }
        // Black and white has a render of its own.
        function onOptionsChanged() { if (root.page > 0) backend.requestPreview(root.page) }
    }

    MouseArea { anchors.fill: parent; onClicked: root.closed() }

    // Room for the sheet above the page controls.
    Item {
        id: room
        anchors.fill: parent
        anchors.margins: 24
        anchors.bottomMargin: 66

        SheetPaper {
            id: sheet
            anchors.centerIn: parent
            width: Math.min(room.width, room.height / placed.aspect)
            height: width * placed.aspect
            page: root.page
            source: root.source
        }
    }
    BusyIndicator { anchors.centerIn: parent; running: root.visible && sheet.status !== Image.Ready }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        spacing: 12
        Button {
            text: "‹"
            flat: true
            enabled: root.page > 1
            onClicked: root.page -= 1
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            color: sheet.placed.cut ? "#e5484d" : Material.foreground
            text: qsTr("Page %1 of %2").arg(root.page).arg(backend.pageCount)
                  + "  ·  " + qsTr("%1%").arg(sheet.placed.percent)
                  + (sheet.placed.cut ? "  ·  " + qsTr("edges cut") : "")
        }
        Button {
            text: "›"
            flat: true
            enabled: root.page < backend.pageCount
            onClicked: root.page += 1
        }
    }

    Shortcut { enabled: root.visible; sequence: "Left"; onActivated: if (root.page > 1) root.page -= 1 }
    Shortcut { enabled: root.visible; sequence: "Right"; onActivated: if (root.page < backend.pageCount) root.page += 1 }
}
