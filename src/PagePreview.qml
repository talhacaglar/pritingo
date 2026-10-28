import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A closer look at one page, rendered on demand.
Rectangle {
    id: root
    property int page: 0
    property string source: ""
    signal closed()

    visible: page > 0
    color: Qt.rgba(0.04, 0.04, 0.05, 0.92)
    onPageChanged: { source = ""; if (page > 0) backend.requestPreview(page) }

    Connections {
        target: backend
        function onPreviewReady(page, url) { if (page === root.page) root.source = url }
    }

    MouseArea { anchors.fill: parent; onClicked: root.closed() }

    Image {
        id: image
        anchors.fill: parent
        anchors.margins: 40
        anchors.bottomMargin: 70
        fillMode: Image.PreserveAspectFit
        source: root.source
    }
    BusyIndicator { anchors.centerIn: parent; running: root.visible && image.status !== Image.Ready }

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
            text: qsTr("Page %1 of %2").arg(root.page).arg(backend.pageCount)
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
