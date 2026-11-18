// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.kde.kcmutils as KCM
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.volume

KCM.ScrollViewKCM {
    id: page

    title: i18nd("kcm_pulseaudio", "Sound Quality")
    implicitHeight: Kirigami.Units.gridUnit * 28
    implicitWidth: Kirigami.Units.gridUnit * 28

    readonly property var standardRates: [32000, 44100, 48000, 88200, 96000, 176400, 192000]
    readonly property var quantums: [32, 64, 128, 256, 512, 1024, 2048]
    readonly property int textWidth: Kirigami.Units.gridUnit * 30
    property bool allAccounts: false
    readonly property var scopeState: allAccounts ? formats.systemState : formats.userState
    readonly property var scopeGraph: scopeState.graph !== undefined ? scopeState.graph : ({})
    readonly property var engineRates: formats.engine.rates !== undefined ? formats.engine.rates : []
    readonly property bool customRates: scopeGraph.allowedRates !== undefined

    SoundFormats {
        id: formats
    }

    Timer {
        interval: 3000
        running: page.visible && !formats.busy
        repeat: true
        onTriggered: formats.refresh()
    }

    function clone(o) {
        return JSON.parse(JSON.stringify(o))
    }

    function rateText(r) {
        return i18nd("kcm_pulseaudio", "%1 Hz", r)
    }

    function formatText(f) {
        const bits = {"S16LE": 16, "S24LE": 24, "S24_32LE": 24, "S32LE": 32, "F32LE": 32}
        if (f === "F32LE") {
            return i18nd("kcm_pulseaudio", "32 bit float")
        }
        return bits[f] ? i18nd("kcm_pulseaudio", "%1 bit", bits[f]) : f
    }

    function listText(list, fn) {
        let out = []
        for (let i = 0; i < list.length; ++i) {
            out.push(fn(list[i]))
        }
        return out.join(", ")
    }

    function changeGraph(key, value) {
        let st = clone(scopeState)
        st.graph = st.graph || {}
        if (value === undefined) {
            delete st.graph[key]
        } else {
            st.graph[key] = value
        }
        if (Object.keys(st.graph).length === 0) {
            delete st.graph
        }
        formats.apply(allAccounts, st, "graph")
    }

    function setQuality(value) {
        let st = clone(scopeState)
        if (value === undefined) {
            delete st.quality
        } else {
            st.quality = value
        }
        formats.apply(allAccounts, st, "quality")
    }

    function changeDevice(name, key, value) {
        let st = clone(scopeState)
        st.devices = st.devices || {}
        let d = st.devices[name] || {}
        if (value === undefined || value === false) {
            delete d[key]
        } else {
            d[key] = value
        }
        if (Object.keys(d).length === 0) {
            delete st.devices[name]
        } else {
            st.devices[name] = d
        }
        if (Object.keys(st.devices).length === 0) {
            delete st.devices
        }
        formats.apply(allAccounts, st, "device:" + name)
    }

    function personDevice(name) {
        const d = scopeState.devices
        return d !== undefined && d[name] !== undefined ? d[name] : ({})
    }

    function backToAutomatic() {
        let st = clone(scopeState)
        delete st.graph
        delete st.quality
        formats.apply(allAccounts, st, "graph")
    }

    headerPaddingEnabled: false
    header: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: formats.message.text !== undefined
            type: formats.message.type === "positive" ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            text: formats.message.text !== undefined ? formats.message.text : ""
            showCloseButton: true
            onVisibleChanged: if (!visible) formats.dismissMessage()
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !formats.engine.installed || !formats.engine.available
            type: Kirigami.MessageType.Warning
            position: Kirigami.InlineMessage.Position.Header
            text: i18nd("kcm_pulseaudio", "The sound defaults of this computer have not been written yet, so there is no automatic choice to show. Run: sudo sparky-stereo-audio detect --write")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !formats.graph.available
            type: Kirigami.MessageType.Warning
            position: Kirigami.InlineMessage.Position.Header
            text: i18nd("kcm_pulseaudio", "The sound system of this session did not answer, so its rates and devices cannot be shown.")
        }

        CheckBox {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            text: i18nd("kcm_pulseaudio", "Apply changes to every account on this computer")
            checked: page.allAccounts
            enabled: !formats.busy
            onToggled: page.allAccounts = checked
        }
    }

    view: Flickable {
        id: flickable
        contentWidth: width
        contentHeight: col.implicitHeight + Kirigami.Units.largeSpacing * 2
        clip: true
        enabled: !formats.busy

        ColumnLayout {
            id: col
            width: flickable.width

        Kirigami.FormLayout {
            id: form
            Layout.fillWidth: true

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Sound system")
            }

            Label {
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Automatic choice:")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                visible: formats.engine.available === true
                text: i18nd("kcm_pulseaudio", "Rates %1, resting at %2, resampling quality %3 of 14.",
                            page.listText(page.engineRates, r => String(r)), page.rateText(formats.engine.defaultRate), formats.engine.quality)
            }

            Label {
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Running now:")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                text: formats.graph.rate === undefined ? "" : i18nd("kcm_pulseaudio", "%1, buffer %2 (%3 ms).",
                    page.rateText(formats.graph.rate), formats.graph.quantum, (formats.graph.quantum * 1000 / formats.graph.rate).toFixed(1))
            }

            ComboBox {

                Layout.minimumWidth: Kirigami.Units.gridUnit * 16
                id: defaultRate
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Resting rate:")
                readonly property var values: [undefined].concat(page.engineRates)
                model: values.map(v => v === undefined ? i18nd("kcm_pulseaudio", "Automatic (%1)", page.rateText(formats.engine.defaultRate)) : page.rateText(v))
                currentIndex: Math.max(0, values.indexOf(page.scopeGraph.rate))
                onActivated: index => page.changeGraph("rate", values[index])
            }

            ColumnLayout {
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Rates the sound system may use:")
                Kirigami.FormData.labelAlignment: Qt.AlignTop

                RadioButton {
                    text: i18nd("kcm_pulseaudio", "Automatic (the rates of the devices in this computer)")
                    checked: !page.customRates
                    onClicked: page.changeGraph("allowedRates", undefined)
                }
                RadioButton {
                    id: customRadio
                    text: i18nd("kcm_pulseaudio", "These rates:")
                    checked: page.customRates
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.gridUnit
                    spacing: Kirigami.Units.smallSpacing
                    enabled: customRadio.checked
                    Repeater {
                        id: rateBoxes
                        model: page.engineRates
                        CheckBox {
                            required property var modelData
                            text: String(modelData)
                            checked: page.scopeGraph.allowedRates !== undefined ? page.scopeGraph.allowedRates.indexOf(modelData) >= 0 : true
                        }
                    }
                }
                Button {
                    Layout.leftMargin: Kirigami.Units.gridUnit
                    text: i18nd("kcm_pulseaudio", "Use these rates")
                    enabled: customRadio.checked
                    onClicked: {
                        let chosen = []
                        for (let i = 0; i < rateBoxes.count; ++i) {
                            if (rateBoxes.itemAt(i).checked) {
                                chosen.push(page.engineRates[i])
                            }
                        }
                        if (chosen.length > 0) {
                            page.changeGraph("allowedRates", chosen)
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    Layout.maximumWidth: page.textWidth
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18nd("kcm_pulseaudio", "With several rates the sound system follows the rate of what plays, so the file is not converted. AES5: 48 kHz is recommended for professional audio, 44.1 kHz is recognised for consumer applications, 96 kHz for higher bandwidth.")
                }
            }

            ComboBox {

                Layout.minimumWidth: Kirigami.Units.gridUnit * 16
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Buffer size:")
                readonly property var values: [undefined].concat(page.quantums)
                model: values.map(v => v === undefined ? i18nd("kcm_pulseaudio", "Automatic (PipeWire's own)") : i18nd("kcm_pulseaudio", "%1 samples", v))
                currentIndex: Math.max(0, values.indexOf(page.scopeGraph.quantum))
                onActivated: index => page.changeGraph("quantum", values[index])
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                text: i18nd("kcm_pulseaudio", "A smaller buffer means less delay and more work for the processor. No standard sets this; the delay is the buffer size divided by the rate.")
            }

            SpinBox {
                id: quality
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Resampling quality:")
                from: 0
                to: 14
                value: page.scopeState.quality !== undefined ? page.scopeState.quality : (formats.engine.quality !== undefined ? formats.engine.quality : 4)
                onValueModified: page.setQuality(value)
                textFromValue: (v, locale) => (page.scopeState.quality === undefined && v === formats.engine.quality) ? i18nd("kcm_pulseaudio", "%1 (automatic)", v) : String(v)
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                text: i18nd("kcm_pulseaudio", "Used only when a file's rate is not one the device takes. The scale is PipeWire's own, 0 to 14; no standard defines it.")
            }

            Label {
                Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Why these choices:")
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                visible: formats.engine.reason !== undefined && formats.engine.reason !== ""
                text: formats.engine.reason !== undefined ? formats.engine.reason : ""
                font: Kirigami.Theme.smallFont
            }

            Button {
                text: i18nd("kcm_pulseaudio", "Back to automatic")
                icon.name: "edit-undo-symbolic"
                visible: page.scopeGraph.rate !== undefined || page.customRates || page.scopeGraph.quantum !== undefined || page.scopeState.quality !== undefined
                onClicked: page.backToAutomatic()
            }

        }

            Repeater {
                model: formats.devices

                ColumnLayout {
                    id: dev
                    required property var modelData
                    readonly property bool isBluetooth: modelData.kind === "bluetooth"
                    readonly property bool isOutput: modelData.direction === "output"
                    readonly property var choice: page.personDevice(modelData.name)
                    readonly property int labelWidth: Kirigami.Units.gridUnit * 9
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.largeSpacing

                    Kirigami.Heading {
                        level: 3
                        Layout.fillWidth: true
                        text: dev.modelData.description !== "" ? dev.modelData.description : dev.modelData.name
                    }
                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Kirigami.Units.largeSpacing
                        rowSpacing: Kirigami.Units.smallSpacing
                        Layout.fillWidth: true

                        Label {
                            text: i18nd("kcm_pulseaudio", "In use:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.maximumWidth: page.textWidth
                            wrapMode: Text.Wrap
                            text: formats.live[dev.modelData.name] !== undefined ? formats.live[dev.modelData.name] : i18nd("kcm_pulseaudio", "Not open at the moment.")
                        }

                        Label {
                            text: i18nd("kcm_pulseaudio", "Takes:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.maximumWidth: page.textWidth
                            wrapMode: Text.Wrap
                            text: i18nd("kcm_pulseaudio", "%1; %2", page.listText(dev.modelData.rates, page.rateText), page.listText(dev.modelData.formats, page.formatText))
                        }

                        Label {
                            visible: dev.isBluetooth
                            text: i18nd("kcm_pulseaudio", "Bluetooth codec:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignTop
                        }
                        Label {
                            visible: dev.isBluetooth
                            Layout.fillWidth: true
                            Layout.maximumWidth: page.textWidth
                            wrapMode: Text.Wrap
                            text: dev.modelData.codec !== undefined && dev.modelData.codec !== "" ? i18nd("kcm_pulseaudio", "%1. The codec decides the rate and the format of a Bluetooth device.", dev.modelData.codec) : i18nd("kcm_pulseaudio", "The codec is not known until the device plays.")
                        }

                        Label {
                            visible: dev.modelData.eld !== undefined && dev.modelData.eld.length > 0
                            text: i18nd("kcm_pulseaudio", "The display says:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                            Layout.alignment: Qt.AlignTop
                        }
                        ColumnLayout {
                            visible: dev.modelData.eld !== undefined && dev.modelData.eld.length > 0
                            Layout.fillWidth: true
                            Layout.maximumWidth: page.textWidth
                            Repeater {
                                model: dev.modelData.eld !== undefined ? dev.modelData.eld : []
                                ColumnLayout {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                        text: modelData.monitor
                                        font.bold: true
                                    }
                                    Repeater {
                                        model: modelData.lpcm
                                        Label {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: i18nd("kcm_pulseaudio", "Uncompressed: %1 channels, %2 Hz, %3 bit", modelData.channels, modelData.rates.join(" "), modelData.bits.join(" "))
                                        }
                                    }
                                    Repeater {
                                        model: modelData.encoded
                                        Label {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: i18nd("kcm_pulseaudio", "Encoded, for pass-through: %1, %2 channels", modelData.coding, modelData.channels)
                                        }
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                        font: Kirigami.Theme.smallFont
                                        opacity: 0.7
                                        text: i18nd("kcm_pulseaudio", "This is the list the display sends in its CTA-861 audio data blocks.")
                                    }
                                }
                            }
                        }

                        Label {
                            visible: !dev.isBluetooth
                            text: i18nd("kcm_pulseaudio", "Format:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                        }
                        ComboBox {
                            visible: !dev.isBluetooth
                            Layout.minimumWidth: Kirigami.Units.gridUnit * 16
                            readonly property var values: [undefined].concat(dev.modelData.formats)
                            model: values.map(v => v === undefined ? i18nd("kcm_pulseaudio", "Automatic (best this device supports)") : page.formatText(v))
                            currentIndex: Math.max(0, values.indexOf(dev.choice.format))
                            onActivated: index => page.changeDevice(dev.modelData.name, "format", values[index])
                        }

                        Label {
                            visible: !dev.isBluetooth
                            text: i18nd("kcm_pulseaudio", "Rate:")
                            Layout.preferredWidth: dev.labelWidth
                            horizontalAlignment: Text.AlignRight
                        }
                        ComboBox {
                            visible: !dev.isBluetooth
                            Layout.minimumWidth: Kirigami.Units.gridUnit * 16
                            readonly property var values: [undefined].concat(dev.modelData.rates)
                            model: values.map(v => v === undefined ? i18nd("kcm_pulseaudio", "Automatic (best this device supports)") : page.rateText(v))
                            currentIndex: Math.max(0, values.indexOf(dev.choice.rate))
                            onActivated: index => page.changeDevice(dev.modelData.name, "rate", values[index])
                        }

                        Item {
                            visible: dev.isOutput && !dev.isBluetooth
                            Layout.preferredWidth: dev.labelWidth
                        }
                        ColumnLayout {
                            visible: dev.isOutput && !dev.isBluetooth
                            Layout.fillWidth: true
                            CheckBox {
                                text: i18nd("kcm_pulseaudio", "Bit-perfect: add nothing to the samples")
                                checked: dev.choice.bitPerfect === true
                                onToggled: page.changeDevice(dev.modelData.name, "bitPerfect", checked)
                            }
                            Label {
                                Layout.fillWidth: true
                                Layout.maximumWidth: page.textWidth
                                wrapMode: Text.Wrap
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                                text: i18nd("kcm_pulseaudio", "Switches off dither. The samples reach the device unchanged only while one program plays at full volume in the device's own format.")
                            }
                        }

                        Item {
                            visible: Object.keys(dev.choice).length > 0
                            Layout.preferredWidth: dev.labelWidth
                        }
                        Button {
                            visible: Object.keys(dev.choice).length > 0
                            text: i18nd("kcm_pulseaudio", "Back to automatic")
                            icon.name: "edit-undo-symbolic"
                            onClicked: {
                                let st = page.clone(page.scopeState)
                                if (st.devices !== undefined) {
                                    delete st.devices[dev.modelData.name]
                                    if (Object.keys(st.devices).length === 0) {
                                        delete st.devices
                                    }
                                }
                                formats.apply(page.allAccounts, st, "device:" + dev.modelData.name)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                Layout.maximumWidth: page.textWidth
                visible: formats.devices.length === 0 && formats.graph.available === true
                text: i18nd("kcm_pulseaudio", "No sound device is connected.")
            }
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: formats.busy
        visible: formats.busy
    }
}
