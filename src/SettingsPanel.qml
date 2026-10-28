import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: panel
    color: win.panel
    enabled: !win.busy
    property bool advanced: false

    readonly property var o: backend.options

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

                Caption { text: qsTr("Sides") }
                Segmented {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    stretch: true
                    model: [{ label: qsTr("One-sided"), value: false }, { label: qsTr("Two-sided"), value: true }]
                    value: o.duplex
                    onPicked: (v) => backend.setOption("duplex", v)
                }
                Hint {
                    visible: o.duplex
                    text: qsTr("Fronts print first. Then the app tells you how to turn the stack over for the backs.")
                }

                Caption { text: qsTr("Color") }
                Segmented {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    stretch: true
                    model: [{ label: qsTr("Black & white"), value: false }, { label: qsTr("Color"), value: true }]
                    value: o.color
                    onPicked: (v) => backend.setOption("color", v)
                }

                Caption { text: qsTr("Quality") }
                Segmented {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    stretch: true
                    model: [{ label: qsTr("Best"), value: 1 }, { label: qsTr("Fine"), value: 2 },
                            { label: qsTr("Normal"), value: 3 }, { label: qsTr("Draft"), value: 4 }]
                    value: o.quality
                    onPicked: (v) => backend.setOption("quality", v)
                }

                Caption { text: qsTr("Pages") }
                TextField {
                    id: range
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    placeholderText: qsTr("All pages (e.g. 1-3, 5)")
                    text: backend.rangeText
                    enabled: backend.pageCount > 0
                    Material.accent: backend.rangeValid ? theme.accent : "#e5484d"
                    onTextEdited: backend.setRange(text)
                    Keys.onEscapePressed: focus = false
                }
                RowLayout {
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 6
                    enabled: backend.pageCount > 0
                    QuickChip { text: qsTr("All"); onClicked: backend.selectPages("all") }
                    QuickChip { text: qsTr("Odd"); onClicked: backend.selectPages("odd") }
                    QuickChip { text: qsTr("Even"); onClicked: backend.selectPages("even") }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    Label { text: qsTr("Copies"); Layout.fillWidth: true }
                    SpinBox {
                        from: 1; to: 99
                        value: o.copies
                        editable: true
                        onValueModified: backend.setOption("copies", value)
                    }
                }

                Caption { text: qsTr("Paper") }
                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    columns: 2
                    columnSpacing: 10
                    Label { text: qsTr("Size") }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "value"
                        model: [{ label: "A4", value: "A4" }, { label: "A5", value: "A5" }, { label: "B5", value: "B5" },
                                { label: "Letter", value: "Letter" }, { label: "Legal", value: "Legal" },
                                { label: qsTr("Photo 10×15"), value: "4X6" }, { label: qsTr("Photo 13×18"), value: "5X7" },
                                { label: qsTr("Envelope DL"), value: "envelopdlp" }, { label: qsTr("Envelope #10"), value: "envelop10p" }]
                        currentIndex: Math.max(0, indexOfValue(o.paperSize))
                        onActivated: backend.setOption("paperSize", currentValue)
                    }
                    Label { text: qsTr("Type") }
                    ComboBox {
                        Layout.fillWidth: true
                        textRole: "label"; valueRole: "value"
                        model: [{ label: qsTr("Plain paper"), value: "plain" },
                                { label: qsTr("Photo paper, glossy"), value: "glossystandard" },
                                { label: qsTr("Photo paper Plus Glossy II"), value: "glossygold" },
                                { label: qsTr("Envelope"), value: "envelope" }]
                        currentIndex: Math.max(0, indexOfValue(o.media))
                        onActivated: backend.setOption("media", currentValue)
                    }
                }
                SwitchRow {
                    text: qsTr("Turn landscape pages to fit")
                    checked: o.autoRotate
                    onToggled: (c) => backend.setOption("autoRotate", c)
                }

                // Everything a tinkerer may want, out of the way by default.
                ItemDelegate {
                    Layout.fillWidth: true
                    text: (panel.advanced ? "▾  " : "▸  ") + qsTr("More settings")
                    onClicked: panel.advanced = !panel.advanced
                }

                ColumnLayout {
                    visible: panel.advanced
                    Layout.fillWidth: true
                    spacing: 14

                    Caption { text: qsTr("Second pass") }
                    SwitchRow {
                        text: qsTr("Print backs in reverse order")
                        checked: o.reverseBacks
                        onToggled: (c) => backend.setOption("reverseBacks", c)
                    }
                    SwitchRow {
                        text: qsTr("Turn backs upside down")
                        checked: o.flipBacks
                        onToggled: (c) => backend.setOption("flipBacks", c)
                    }
                    Hint { text: qsTr("Not sure? Print the two-sided test sheet from the ⋯ menu.") }

                    Caption { text: qsTr("Tone") }
                    Segmented {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        stretch: true
                        model: [{ label: qsTr("Light"), value: "1.4" }, { label: qsTr("Normal"), value: "1.8" }, { label: qsTr("Dark"), value: "2.2" }]
                        value: String(o.brightness)
                        onPicked: (v) => backend.setOption("brightness", v)
                    }
                    SliderRow { text: qsTr("Intensity"); key: "density" }
                    SliderRow { text: qsTr("Contrast"); key: "contrast" }
                    SliderRow { visible: o.color; text: qsTr("Cyan"); key: "cyan" }
                    SliderRow { visible: o.color; text: qsTr("Magenta"); key: "magenta" }
                    SliderRow { visible: o.color; text: qsTr("Yellow"); key: "yellow" }

                    Caption { text: qsTr("Ink cartridges") }
                    ComboBox {
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        textRole: "label"; valueRole: "value"
                        model: [{ label: qsTr("Black and color"), value: "bkcolor" },
                                { label: qsTr("Black only"), value: "bk" },
                                { label: qsTr("Color only"), value: "color" }]
                        currentIndex: Math.max(0, indexOfValue(o.cartridges))
                        onActivated: backend.setOption("cartridges", currentValue)
                    }

                    Caption { visible: backend.printers.length > 1; text: qsTr("Printer") }
                    ComboBox {
                        visible: backend.printers.length > 1
                        Layout.fillWidth: true
                        Layout.leftMargin: 20; Layout.rightMargin: 20
                        model: backend.printers
                        currentIndex: Math.max(0, backend.printers.indexOf(backend.printer))
                        onActivated: backend.printer = currentText
                    }

                    Button {
                        Layout.leftMargin: 14
                        flat: true
                        text: qsTr("Reset these")
                        onClicked: backend.resetAdvanced()
                    }
                }

                Item { Layout.preferredHeight: 10 }
            }
        }

        // The print bar stays put while the settings scroll.
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
                    text: backend.pageCount === 0 ? qsTr("Open a file to start.")
                        : !backend.rangeValid ? qsTr("Check the page numbers.")
                        : backend.pagesToPrint === 0 ? qsTr("No page is selected. Press All, or click a page to add it back.")
                        : qsTr("%n page(s)", "", backend.pagesToPrint) + "  ·  "
                          + qsTr("%n sheet(s)", "", backend.sheetCount) + "  ·  "
                          + qsTr("about %n min", "", backend.estimateMinutes)
                }

                AccentButton {
                    id: printButton
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    enabled: backend.pageCount > 0 && backend.rangeValid && backend.pagesToPrint > 0 && !backend.loading
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    text: (o.duplex ? qsTr("Print two-sided") : qsTr("Print")) + "    ␣"
                    onClicked: backend.print()
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

    component QuickChip: Button {
        flat: true
        padding: 6
        leftPadding: 12; rightPadding: 12
        font.pixelSize: 12
        Layout.preferredHeight: 34
    }

    component SwitchRow: RowLayout {
        id: sw
        property alias text: swLabel.text
        property bool checked
        signal toggled(bool checked)
        Layout.fillWidth: true
        Layout.leftMargin: 20; Layout.rightMargin: 12
        Label { id: swLabel; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        Switch { checked: sw.checked; onToggled: sw.toggled(checked) }
    }

    component SliderRow: ColumnLayout {
        id: sr
        property alias text: srLabel.text
        property string key
        Layout.fillWidth: true
        Layout.leftMargin: 20; Layout.rightMargin: 20
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Label { id: srLabel; Layout.fillWidth: true }
            Label {
                color: win.muted
                text: (o[sr.key] > 0 ? "+" : "") + Number(o[sr.key]).toLocaleString(Qt.locale(), 'f', 0)
            }
        }
        Slider {
            Layout.fillWidth: true
            from: -50; to: 50; stepSize: 1
            value: o[sr.key]
            onMoved: backend.setOption(sr.key, Math.round(value))
        }
    }
}
