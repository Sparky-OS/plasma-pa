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

    title: i18nd("kcm_pulseaudio", "Speaker Setup")
    implicitHeight: Kirigami.Units.gridUnit * 34
    implicitWidth: Kirigami.Units.gridUnit * 34

    // The device this page was opened for: the node name of a playback device whose profile has more than two channels.
    property string deviceName: ""
    property bool allAccounts: false
    property bool makeDefault: true
    property string selected: ""
    property bool dirty: false
    property var work: ({levels: {}, distances: null, small: [], crossover: 0})
    readonly property int textWidth: Kirigami.Units.gridUnit * 30
    readonly property var info: {
        const list = setup.devices
        for (let i = 0; i < list.length; ++i) {
            if (list[i].name === deviceName) {
                return list[i]
            }
        }
        return null
    }
    readonly property var positions: info !== null ? info.positions : []
    readonly property bool eligible: info !== null && info.eligible
    readonly property bool hasSub: positions.indexOf("LFE") >= 0
    readonly property var scopeState: allAccounts ? setup.systemState : setup.userState
    readonly property var savedDevice: {
        const devs = scopeState.devices
        return devs !== undefined && devs[deviceName] !== undefined ? devs[deviceName] : ({})
    }
    readonly property var groups: {
        const g = []
        const pairs = [
            {id: "front", members: ["FL", "FR"], text: i18nd("kcm_pulseaudio", "Front speakers")},
            {id: "center", members: ["FC"], text: i18nd("kcm_pulseaudio", "Center speaker")},
            {id: "side", members: ["SL", "SR"], text: i18nd("kcm_pulseaudio", "Side speakers")},
            {id: "rear", members: ["RL", "RR", "RC"], text: i18nd("kcm_pulseaudio", "Rear speakers")}
        ]
        for (let i = 0; i < pairs.length; ++i) {
            const present = pairs[i].members.filter(m => positions.indexOf(m) >= 0)
            if (present.length > 0) {
                g.push({id: pairs[i].id, members: present, text: pairs[i].text})
            }
        }
        return g
    }

    SpeakerSetup {
        id: setup
    }

    Timer {
        interval: 3000
        running: page.visible && !setup.busy && !setup.testRunning
        repeat: true
        onTriggered: setup.refresh()
    }

    function clone(o) {
        return JSON.parse(JSON.stringify(o))
    }

    function speakerName(p) {
        switch (p) {
        case "FL": return i18nd("kcm_pulseaudio", "Front left")
        case "FR": return i18nd("kcm_pulseaudio", "Front right")
        case "FC": return i18nd("kcm_pulseaudio", "Center")
        case "LFE": return i18nd("kcm_pulseaudio", "Subwoofer")
        case "RL": return i18nd("kcm_pulseaudio", "Rear left")
        case "RR": return i18nd("kcm_pulseaudio", "Rear right")
        case "SL": return i18nd("kcm_pulseaudio", "Side left")
        case "SR": return i18nd("kcm_pulseaudio", "Side right")
        case "RC": return i18nd("kcm_pulseaudio", "Rear center")
        }
        return p
    }

    function levelText(db) {
        return i18nd("kcm_pulseaudio", "%1 dB", Number(db).toLocaleString(Qt.locale(), "f", 1))
    }

    function distanceText(m) {
        return setup.feet ? i18nd("kcm_pulseaudio", "%1 ft", Number(m / 0.3048).toLocaleString(Qt.locale(), "f", 1))
                          : i18nd("kcm_pulseaudio", "%1 m", Number(m).toLocaleString(Qt.locale(), "f", 2))
    }

    function load() {
        const d = clone(savedDevice)
        let w = {levels: d.levels !== undefined ? d.levels : ({}), distances: d.distances !== undefined ? d.distances : null,
                 small: d.small !== undefined ? d.small : [], crossover: d.crossover !== undefined ? d.crossover : 0}
        work = w
        dirty = false
        if (selected === "" || positions.indexOf(selected) < 0) {
            selected = positions.length > 0 ? positions[0] : ""
        }
    }

    function touch() {
        work = clone(work)
        dirty = true
    }

    function levelOf(p) {
        return work.levels[p] !== undefined ? work.levels[p] : 0
    }

    function setLevel(p, db) {
        work.levels[p] = db
        touch()
    }

    function farthest() {
        let f = 0
        if (work.distances === null) {
            return f
        }
        for (const k in work.distances) {
            f = Math.max(f, work.distances[k])
        }
        return f
    }

    function distanceOf(p) {
        return work.distances !== null && work.distances[p] !== undefined ? work.distances[p] : 0
    }

    function delayMs(p) {
        return work.distances === null ? 0 : setup.delaySeconds(distanceOf(p), farthest()) * 1000
    }

    function setDistancesOn(on) {
        if (on) {
            let d = {}
            for (let i = 0; i < positions.length; ++i) {
                d[positions[i]] = 3.0
            }
            work.distances = d
        } else {
            work.distances = null
        }
        touch()
    }

    function setDistance(p, m) {
        work.distances[p] = m
        touch()
    }

    function isSmall(group) {
        return group.members.every(m => work.small.indexOf(m) >= 0)
    }

    function setSmall(group, small) {
        let s = work.small.filter(m => group.members.indexOf(m) < 0)
        if (small) {
            s = s.concat(group.members)
        }
        work.small = s
        if (s.length === 0) {
            work.crossover = 0
        } else if (work.crossover === 0) {
            work.crossover = setup.defaultCrossover
        }
        touch()
    }

    // What the person has set for this device, as the drop-in's state; an empty object when every speaker is at its defaults.
    function composeDevice() {
        let levels = {}
        let any = false
        for (const k in work.levels) {
            if (work.levels[k] !== 0 && positions.indexOf(k) >= 0) {
                levels[k] = work.levels[k]
                any = true
            }
        }
        let d = {positions: positions}
        if (any) {
            d.levels = levels
        }
        if (work.distances !== null) {
            let all = positions.map(p => work.distances[p])
            if (Math.max.apply(null, all) > Math.min.apply(null, all)) {
                d.distances = work.distances
                any = true
            }
        }
        const small = positions.filter(p => work.small.indexOf(p) >= 0 && p !== "LFE")
        if (small.length > 0 && hasSub) {
            d.small = small
            d.crossover = work.crossover > 0 ? work.crossover : setup.defaultCrossover
            any = true
        }
        if (!any) {
            return null
        }
        // The label becomes the setup's name in the lists of devices; only characters the drop-in accepts go into it.
        const allowed = "\\p{L}\\p{N}\\p{M}\\p{Zs}._+:()\\/\\uFF1A\\uFF08\\uFF09\\-"
        const text = info.description.replace(new RegExp("[^" + allowed + "]", "gu"), "").trim().substring(0, 56)
        if (/^[\p{L}\p{N}]/u.test(text)) {
            const translated = i18nd("kcm_pulseaudio", "Speaker Setup: %1", text)
            d.label = new RegExp("^[\\p{L}\\p{N}][" + allowed + "]{0,79}$", "u").test(translated) ? translated : "Speaker Setup: " + text
        }
        return d
    }

    function composeState() {
        let st = clone(scopeState)
        st.devices = st.devices !== undefined ? st.devices : ({})
        const d = composeDevice()
        if (d === null) {
            delete st.devices[deviceName]
        } else {
            st.devices[deviceName] = d
        }
        if (Object.keys(st.devices).length === 0) {
            delete st.devices
        }
        return st
    }

    readonly property bool hasChanges: dirty && (composeDevice() !== null || Object.keys(savedDevice).length > 0)

    function applyNow() {
        setup.apply(allAccounts, composeState(), deviceName, makeDefault)
    }

    function backToDefaults() {
        let st = clone(scopeState)
        if (st.devices !== undefined) {
            delete st.devices[deviceName]
            if (Object.keys(st.devices).length === 0) {
                delete st.devices
            }
        }
        setup.apply(allAccounts, st, deviceName, makeDefault)
    }

    onDeviceNameChanged: load()
    onAllAccountsChanged: load()
    onPositionsChanged: if (!dirty) load()
    Connections {
        target: setup
        function onApplied(ok, node) {
            page.load()
        }
        function onChanged() {
            if (!page.dirty) {
                page.load()
            }
        }
    }
    Component.onCompleted: load()

    headerPaddingEnabled: false
    header: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: setup.message.text !== undefined
            type: setup.message.type === "positive" ? Kirigami.MessageType.Positive : Kirigami.MessageType.Error
            position: Kirigami.InlineMessage.Position.Header
            text: setup.message.text !== undefined ? setup.message.text : ""
            showCloseButton: true
            onVisibleChanged: if (!visible) setup.dismissMessage()
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.info === null
            type: Kirigami.MessageType.Warning
            position: Kirigami.InlineMessage.Position.Header
            text: i18nd("kcm_pulseaudio", "This device is no longer connected.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: page.info !== null && !page.eligible
            type: Kirigami.MessageType.Information
            position: Kirigami.InlineMessage.Position.Header
            text: page.info !== null ? page.info.reason : ""
        }

        RowLayout {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            Label {
                text: i18nd("kcm_pulseaudio", "Device:")
            }

            ComboBox {
                id: deviceBox
                Layout.fillWidth: true
                enabled: !setup.busy
                readonly property var eligibleDevices: setup.devices.filter(d => d.eligible)
                model: eligibleDevices.map(d => d.description !== "" ? d.description : d.name)
                currentIndex: Math.max(0, eligibleDevices.findIndex(d => d.name === page.deviceName))
                onActivated: index => {
                    page.deviceName = eligibleDevices[index].name
                }
            }
        }

        CheckBox {
            Layout.leftMargin: Kirigami.Units.largeSpacing
            text: i18nd("kcm_pulseaudio", "Apply changes to every account on this computer")
            checked: page.allAccounts
            enabled: !setup.busy
            onToggled: page.allAccounts = checked
        }
    }

    view: Flickable {
        id: flickable
        contentWidth: width
        contentHeight: col.implicitHeight + Kirigami.Units.largeSpacing * 2
        clip: true
        enabled: !setup.busy && page.eligible

        ColumnLayout {
            id: col
            width: flickable.width
            spacing: Kirigami.Units.largeSpacing

            // The room: the listener in the middle and the speakers where ITU-R BS.775-4 puts them.
            Item {
                id: room
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Kirigami.Units.gridUnit * 30
                Layout.preferredHeight: Kirigami.Units.gridUnit * 24
                readonly property real cx: width / 2
                readonly property real cy: height * 0.5
                readonly property real farMax: page.work.distances !== null ? Math.max(page.farthest(), 0.5) : 1

                Rectangle {
                    anchors.fill: parent
                    radius: Kirigami.Units.cornerRadius
                    color: "transparent"
                    border.width: 1
                    border.color: Kirigami.Theme.disabledTextColor
                }

                Kirigami.Icon {
                    x: room.cx - width / 2
                    y: room.cy - height / 2
                    width: Kirigami.Units.iconSizes.medium
                    height: width
                    source: "user-identity"
                    Accessible.name: i18nd("kcm_pulseaudio", "Listener")
                }

                Repeater {
                    model: page.positions

                    Rectangle {
                        id: spk
                        required property string modelData
                        readonly property bool isSub: modelData === "LFE"
                        readonly property real angle: page.info !== null ? setup.angle(modelData, page.positions) * Math.PI / 180 : 0
                        readonly property real orbit: {
                            const base = Math.min(room.width, room.height) * 0.38
                            if (page.work.distances === null) {
                                return base
                            }
                            return base * Math.max(0.25, page.distanceOf(modelData) / room.farMax)
                        }
                        width: Kirigami.Units.gridUnit * 4
                        height: Kirigami.Units.gridUnit * 2.4
                        x: (isSub ? room.width - width - Kirigami.Units.smallSpacing : room.cx + orbit * Math.sin(angle)) - (isSub ? 0 : width / 2)
                        y: (isSub ? room.height - height - Kirigami.Units.smallSpacing : room.cy - orbit * Math.cos(angle)) - (isSub ? 0 : height / 2)
                        color: setup.testing === modelData ? Kirigami.Theme.highlightColor
                             : (page.selected === modelData ? Qt.alpha(Kirigami.Theme.highlightColor, 0.35) : Kirigami.Theme.backgroundColor)
                        border.width: page.selected === modelData ? 2 : 1
                        border.color: page.selected === modelData ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        Accessible.role: Accessible.Button
                        Accessible.name: page.speakerName(modelData)

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 0
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: spk.modelData
                                font.bold: true
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                color: setup.testing === spk.modelData ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: page.levelText(page.levelOf(spk.modelData))
                                font: Kirigami.Theme.smallFont
                                opacity: 0.7
                                color: setup.testing === spk.modelData ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                        }

                        MouseArea {
                            id: spkArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: page.selected = spk.modelData
                        }

                        ToolTip.visible: spkArea.containsMouse
                        ToolTip.text: page.speakerName(spk.modelData)
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: page.textWidth
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                opacity: 0.7
                text: i18nd("kcm_pulseaudio", "The speakers stand at the angles of Recommendation ITU-R BS.775-4: left and right at 30 degrees, the surrounds between 100 and 120 degrees.")
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: page.selected !== "" ? page.speakerName(page.selected) : ""
                }

                RowLayout {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Level:")
                    SpinBox {
                        id: levelBox
                        from: Math.round(setup.minLevel * 10)
                        to: Math.round(setup.maxLevel * 10)
                        stepSize: 5
                        editable: true
                        value: Math.round(page.levelOf(page.selected) * 10)
                        textFromValue: (v, locale) => page.levelText(v / 10)
                        valueFromText: (t, locale) => Math.round(Number.fromLocaleString(locale, t.replace(/[^0-9,.\-−]/g, "").replace("−", "-")) * 10)
                        onValueModified: page.setLevel(page.selected, value / 10)
                    }
                    Button {
                        icon.name: "edit-undo"
                        text: i18nd("kcm_pulseaudio", "0 dB")
                        enabled: page.levelOf(page.selected) !== 0
                        onClicked: page.setLevel(page.selected, 0)
                    }
                }

                RowLayout {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Test signal:")
                    Button {
                        icon.name: "media-playback-start"
                        text: i18nd("kcm_pulseaudio", "Test this speaker")
                        enabled: page.selected !== "" && !setup.testRunning
                        onClicked: setup.testChannel(page.deviceName, page.selected)
                    }
                    Button {
                        icon.name: "media-playlist-play"
                        text: i18nd("kcm_pulseaudio", "Test all in sequence")
                        enabled: !setup.testRunning
                        onClicked: setup.testAll(page.deviceName)
                    }
                    Button {
                        icon.name: "media-playback-stop"
                        text: i18nd("kcm_pulseaudio", "Stop")
                        enabled: setup.testRunning
                        onClicked: setup.stopTest()
                    }
                }

                ComboBox {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Test signal length:")
                    Layout.minimumWidth: Kirigami.Units.gridUnit * 14
                    readonly property var values: [1, 2, 5, 10]
                    model: values.map(v => i18nd("kcm_pulseaudio", "%1 s per speaker", v))
                    currentIndex: Math.max(0, values.indexOf(setup.testSeconds))
                    onActivated: index => setup.testSeconds = values[index]
                }

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.textWidth
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18nd("kcm_pulseaudio", "Pink noise at -20 dBFS, one speaker at a time, through the speaker setup when there is one.")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Distances")
                }

                CheckBox {
                    text: i18nd("kcm_pulseaudio", "The speakers are at different distances from the listener")
                    checked: page.work.distances !== null
                    onToggled: page.setDistancesOn(checked)
                }

                RowLayout {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Distance:")
                    visible: page.work.distances !== null
                    SpinBox {
                        id: distBox
                        from: setup.feet ? 10 : Math.round(setup.minDistance * 10)
                        to: setup.feet ? Math.round(setup.maxDistance / 0.3048 * 10) : Math.round(setup.maxDistance * 10)
                        stepSize: 1
                        editable: true
                        value: Math.round((setup.feet ? page.distanceOf(page.selected) / 0.3048 : page.distanceOf(page.selected)) * 10)
                        textFromValue: (v, locale) => page.distanceText(setup.feet ? v / 10 * 0.3048 : v / 10)
                        valueFromText: (t, locale) => Math.round(Number.fromLocaleString(locale, t.replace(/[^0-9,.]/g, "")) * 10)
                        onValueModified: page.setDistance(page.selected, setup.feet ? value / 10 * 0.3048 : value / 10)
                    }
                    ComboBox {
                        model: [i18nd("kcm_pulseaudio", "Meters"), i18nd("kcm_pulseaudio", "Feet")]
                        currentIndex: setup.feet ? 1 : 0
                        onActivated: index => setup.feet = index === 1
                    }
                }

                Label {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Delay:")
                    visible: page.work.distances !== null
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.textWidth
                    wrapMode: Text.Wrap
                    text: page.delayMs(page.selected) === 0 ? i18nd("kcm_pulseaudio", "None: this is the farthest speaker.")
                        : i18nd("kcm_pulseaudio", "%1 ms, so that its sound arrives with the farthest speaker's.", Number(page.delayMs(page.selected)).toLocaleString(Qt.locale(), "f", 2))
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Bass management")
                }

                Label {
                    visible: !page.hasSub
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.textWidth
                    wrapMode: Text.Wrap
                    text: i18nd("kcm_pulseaudio", "Bass management needs a subwoofer channel, and the profile of this device has none.")
                }

                Repeater {
                    model: page.hasSub ? page.groups : []
                    delegate: ComboBox {
                        required property var modelData
                        Kirigami.FormData.label: modelData.text + ":"
                        readonly property var group: modelData
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 18
                        model: [i18nd("kcm_pulseaudio", "Large (full range)"), i18nd("kcm_pulseaudio", "Small (bass goes to the subwoofer)")]
                        currentIndex: page.isSmall(group) ? 1 : 0
                        onActivated: index => page.setSmall(group, index === 1)
                    }
                }

                RowLayout {
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Crossover:")
                    visible: page.hasSub
                    enabled: page.work.small.length > 0
                    SpinBox {
                        from: setup.minCrossover
                        to: setup.maxCrossover
                        stepSize: 10
                        editable: true
                        value: page.work.crossover > 0 ? page.work.crossover : setup.defaultCrossover
                        textFromValue: (v, locale) => i18nd("kcm_pulseaudio", "%1 Hz", v)
                        valueFromText: (t, locale) => Math.round(Number(t.replace(/[^0-9]/g, "")))
                        onValueModified: {
                            page.work.crossover = value
                            page.touch()
                        }
                    }
                }

                Label {
                    visible: page.hasSub
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.textWidth
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18nd("kcm_pulseaudio", "Small speakers play only what is above the crossover; what is below goes to the subwoofer, with a 24 dB per octave slope. With every speaker large nothing is added to the signal.")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nd("kcm_pulseaudio", "Output")
                }

                CheckBox {
                    text: i18nd("kcm_pulseaudio", "Make the speaker setup the output of this account")
                    checked: page.makeDefault
                    onToggled: page.makeDefault = checked
                }

                Label {
                    Layout.fillWidth: true
                    Layout.maximumWidth: page.textWidth
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    opacity: 0.7
                    text: i18nd("kcm_pulseaudio", "Programs reach the speakers through the setup only while it is the output they play to. With every speaker at its defaults there is no setup and nothing is added to the signal.")
                }
            }
        }
    }

    footer: RowLayout {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Item {
            Layout.fillWidth: true
        }

        Button {
            icon.name: "edit-reset"
            text: i18nd("kcm_pulseaudio", "Back to defaults")
            enabled: !setup.busy && page.eligible && Object.keys(page.savedDevice).length > 0
            onClicked: page.backToDefaults()
        }

        Button {
            icon.name: "dialog-ok-apply"
            text: i18nd("kcm_pulseaudio", "Apply")
            enabled: !setup.busy && page.eligible && page.hasChanges
            onClicked: page.applyNow()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: setup.busy
        visible: setup.busy
    }
}
