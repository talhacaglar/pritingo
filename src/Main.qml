import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: backend.fileName ? backend.fileName + " — Pritingo" : "Pritingo"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#17171b"
    readonly property color line: "#26262c"
    readonly property color muted: "#8b8b95"
    readonly property bool busy: backend.state === "printing" || backend.state === "flip"
    property bool helpVisible: false
    property bool scanOpen: false
    property int previewPage: 0
    property string view: backend.options.duplex ? "sheets" : "pages"

    function primary() {
        if (helpVisible) { helpVisible = false; return }
        if (previewPage) { previewPage = 0; return }
        if (scanOpen && !busy) { if (!scanner.busy) scanner.scan(); return }
        if (backend.state === "flip") backend.continueAfterFlip()
        else if (backend.state === "done" || backend.state === "error") backend.dismiss()
        else if (backend.state === "idle" && backend.pageCount > 0) backend.print()
        else if (backend.pageCount === 0) backend.pickFile()
    }
    function dismissTop() {
        if (helpVisible) helpVisible = false
        else if (previewPage) previewPage = 0
        else if (busy) cancelDialog.open()
        else if (scanOpen && scanner.busy) scanner.cancel()
        else if (scanOpen) scanOpen = false
        else if (backend.state !== "idle") backend.dismiss()
    }

    // Typing in the range field must not trigger single-key shortcuts.
    readonly property bool typing: activeFocusItem instanceof TextInput
    // The document's single-key shortcuts rest while the scan view is up.
    readonly property bool documentKeys: !typing && !scanOpen

    Shortcut { sequence: "?"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "F1"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.busy ? quitDialog.open() : Qt.quit() }
    Shortcut { sequence: "Space"; enabled: !win.typing; context: Qt.ApplicationShortcut; onActivated: win.primary() }
    Shortcut { sequence: "Return"; enabled: !win.typing || backend.state === "idle"; context: Qt.ApplicationShortcut; onActivated: win.primary() }
    Shortcut { sequence: "Ctrl+P"; context: Qt.ApplicationShortcut; onActivated: win.scanOpen && !win.busy ? scanner.print() : win.primary() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; onActivated: win.dismissTop() }
    Shortcut { sequences: ["O", "Ctrl+O"]; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.pickFile() }
    Shortcut { sequence: "D"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.setOption("duplex", !backend.options.duplex) }
    Shortcut { sequence: "C"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.setOption("color", !backend.options.color) }
    Shortcut { sequence: "V"; enabled: win.documentKeys; context: Qt.ApplicationShortcut; onActivated: win.view = win.view === "pages" ? "sheets" : "pages" }
    Shortcut { sequence: "+"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.setOption("copies", Math.min(99, backend.options.copies + 1)) }
    Shortcut { sequence: "-"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.setOption("copies", Math.max(1, backend.options.copies - 1)) }
    Shortcut { sequence: "A"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.selectPages("all") }
    Shortcut { sequence: "S"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: win.scanOpen = true }
    Shortcut { sequence: "Ctrl+S"; enabled: win.scanOpen; context: Qt.ApplicationShortcut; onActivated: scanner.save() }
    Shortcut { sequence: "C"; enabled: win.scanOpen && !win.typing && !scanner.busy; context: Qt.ApplicationShortcut; onActivated: scanner.setOption("color", !scanner.options.color) }
    Shortcut { sequence: "W"; enabled: win.documentKeys && !win.busy; context: Qt.ApplicationShortcut; onActivated: backend.closeFile() }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TopBar {
                Layout.fillWidth: true
                onHelp: win.helpVisible = true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    DropArea {
                        id: drop
                        anchors.fill: parent
                        enabled: !win.busy
                        onDropped: (event) => {
                            if (event.hasUrls && event.urls.length > 0)
                                backend.openFile(event.urls[0].toString())
                        }
                    }

                    EmptyState {
                        anchors.fill: parent
                        visible: backend.pageCount === 0
                        hovering: drop.containsDrag
                    }

                    SheetsView {
                        anchors.fill: parent
                        visible: backend.pageCount > 0
                        mode: win.view
                        onModeRequested: (m) => win.view = m
                        onPreview: (page) => win.previewPage = page
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: drop.containsDrag && backend.pageCount > 0
                        color: Qt.rgba(0, 0, 0, 0.55)
                        border.color: theme.accent
                        border.width: 2
                        radius: theme.radius
                        Label {
                            anchors.centerIn: parent
                            text: qsTr("Drop to replace the document")
                            font.pixelSize: 22
                        }
                    }
                }

                Rectangle { Layout.fillHeight: true; width: 1; color: win.line }

                SettingsPanel {
                    Layout.fillHeight: true
                    // Narrower when Hyprland tiles the window small, so the
                    // pages keep most of the room.
                    Layout.preferredWidth: Math.round(Math.max(290, Math.min(380, win.width * 0.36)))
                }
            }
        }

        ScanView { objectName: "scanView"; anchors.fill: parent; visible: win.scanOpen; onClosed: win.scanOpen = false }
        PrintingOverlay { objectName: "printingOverlay"; anchors.fill: parent; visible: backend.state === "printing" }
        FlipOverlay { objectName: "flipOverlay"; anchors.fill: parent; visible: backend.state === "flip" }
        FinishedOverlay { objectName: "finishedOverlay"; anchors.fill: parent; visible: backend.state === "done" || backend.state === "error" }
        PagePreview { anchors.fill: parent; page: win.previewPage; onClosed: win.previewPage = 0 }
        HelpOverlay { objectName: "helpOverlay"; anchors.fill: parent; visible: win.helpVisible; onClosed: win.helpVisible = false }

        // Problems stay until the next action clears them; toasts fade.
        Rectangle {
            id: problemBar
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 20
            readonly property string message: backend.problem || scanner.problem
            visible: message !== ""
            width: Math.min(parent.width - 80, problemText.implicitWidth + 48)
            height: problemText.implicitHeight + 24
            radius: theme.radius
            color: "#3a1d1d"
            border.color: "#e5484d"
            Label {
                id: problemText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, parent.parent.width - 128)
                wrapMode: Text.WordWrap
                text: "⚠  " + problemBar.message
                color: "#ffd7d7"
            }
        }

        Rectangle {
            id: toastBox
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: problemBar.visible ? 84 : 20
            width: toastText.implicitWidth + 40
            height: 44
            radius: theme.radius
            color: "#24242a"
            border.color: win.line
            opacity: 0
            Label { id: toastText; anchors.centerIn: parent }
            SequentialAnimation {
                id: toastAnimation
                NumberAnimation { target: toastBox; property: "opacity"; to: 1; duration: 150 }
                PauseAnimation { duration: 2200 }
                NumberAnimation { target: toastBox; property: "opacity"; to: 0; duration: 400 }
            }
            Connections {
                target: backend
                function onToast(message) { toastText.text = message; toastAnimation.restart() }
            }
            Connections {
                target: scanner
                function onToast(message) { toastText.text = message; toastAnimation.restart() }
                // A copy opens like any document, ready for the print settings.
                function onReadyToPrint() { win.scanOpen = false }
            }
        }
    }

    Dialog {
        id: cancelDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Stop printing?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("Pages already sent will still come out.") }
        onAccepted: backend.cancel()
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Quit while printing?")
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: qsTr("The pages not yet sent will not be printed.") }
        onAccepted: { backend.cancel(); Qt.quit() }
    }

    onClosing: (close) => {
        if (busy) {
            close.accepted = false
            quitDialog.open()
        }
    }
}
