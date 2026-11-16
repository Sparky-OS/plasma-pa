/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root
    required property var controller
    property string sourceName: ""
    property bool combined: false
    property var routeState: ({})
    readonly property var state: combined ? controller.combined : routeState
    readonly property string input: combined ? (state.source || "") : sourceName
    readonly property string output: state.output || ""
    readonly property bool wanted: state.enabled || false
    function configure(source, output, enabled) {
        if (combined) {
            controller.setCombined(source, output, enabled);
        } else {
            controller.setListenRoute(state.id || "", source, output, enabled);
        }
    }

    CheckBox {
        objectName: root.combined ? "combinedRecording" : "listen-" + root.sourceName
        text: root.combined ? i18nd("kcm_pulseaudio", "Combine output and microphone for recording") : i18nd("kcm_pulseaudio", "Listen to this device")
        checked: root.wanted
        enabled: controller.serviceError.length === 0 && (root.wanted || (root.input.length > 0 && root.output.length > 0))
        onClicked: root.configure(root.input, root.output, checked)
    }

    Label {
        visible: root.combined
        text: i18nd("kcm_pulseaudio", "Recording apps can select Combined Recording as one input.")
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    GridLayout {
        columns: 2
        Layout.fillWidth: true
        Label {
            visible: root.combined
            text: i18nd("kcm_pulseaudio", "Input:")
        }
        ComboBox {
            visible: root.combined
            objectName: "combinedInput"
            Layout.fillWidth: true
            model: root.controller.inputs
            textRole: "description"
            valueRole: "name"
            currentIndex: model.findIndex(item => item.name === root.input)
            displayText: currentIndex < 0 ? (root.input.length > 0 ? root.state.sourceLabel : i18nd("kcm_pulseaudio", "Choose an input")) : currentText
            Accessible.name: i18nd("kcm_pulseaudio", "Combined recording input")
            onActivated: root.configure(currentValue, root.output, root.wanted)
        }
        Label {
            text: i18nd("kcm_pulseaudio", "Output:")
        }
        ComboBox {
            objectName: root.combined ? "combinedOutput" : "listenOutput-" + root.sourceName
            Layout.fillWidth: true
            model: root.controller.outputs
            textRole: "description"
            valueRole: "name"
            currentIndex: model.findIndex(item => item.name === root.output)
            displayText: currentIndex < 0 ? (root.output.length > 0 ? root.state.outputLabel : i18nd("kcm_pulseaudio", "Choose an output")) : currentText
            Accessible.name: root.combined ? i18nd("kcm_pulseaudio", "Combined recording output") : i18nd("kcm_pulseaudio", "Listening output")
            onActivated: root.configure(root.input, currentValue, root.wanted)
        }
    }

    Kirigami.InlineMessage {
        Layout.fillWidth: true
        visible: text.length > 0
        type: (root.state.reason || 0) > 0 || root.controller.serviceError.length > 0 ? Kirigami.MessageType.Error : Kirigami.MessageType.Information
        text: root.controller.serviceError || root.controller.message(root.state)
        actions: [
            Kirigami.Action {
                text: i18nd("kcm_pulseaudio", "Retry")
                visible: (root.state.reason || 0) > 0
                onTriggered: root.configure(root.input, root.output, root.wanted)
            }
        ]
    }
}
