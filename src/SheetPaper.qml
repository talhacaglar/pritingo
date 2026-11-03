import QtQuick

// One sheet as it will come out: the page where the scale puts it, cut off
// where the printer cannot reach, with that unreachable rim tinted.
Rectangle {
    id: sheet
    property int page: 0
    property string source: ""
    readonly property var placed: { backend.layoutRevision; return backend.placement(page) }
    readonly property alias status: image.status

    color: "#dcdce4"

    Rectangle {
        id: area
        x: sheet.placed.areaX * sheet.width
        y: sheet.placed.areaY * sheet.height
        width: sheet.placed.areaWidth * sheet.width
        height: sheet.placed.areaHeight * sheet.height
        color: "white"
        clip: true

        Image {
            id: image
            // The render shows the page as authored. Turned, its sides swap
            // around the same center.
            readonly property real wide: sheet.placed.width * sheet.width
            readonly property real tall: sheet.placed.height * sheet.height
            width: sheet.placed.turned ? tall : wide
            height: sheet.placed.turned ? wide : tall
            x: sheet.placed.x * sheet.width - area.x + (wide - width) / 2
            y: sheet.placed.y * sheet.height - area.y + (tall - height) / 2
            rotation: sheet.placed.turned ? 90 : 0
            asynchronous: true
            mipmap: true
            source: sheet.source
        }
    }

    // Marks the printer's reach when something falls outside it.
    Rectangle {
        anchors.fill: area
        visible: sheet.placed.cut
        color: "transparent"
        border.color: "#e5484d"
        border.width: Math.max(1, sheet.width / 200)
    }
}
