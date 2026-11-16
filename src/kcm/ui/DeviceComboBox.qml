/*
    SPDX-FileCopyrightText: 2014-2015 Harald Sitter <sitter@kde.org>
    SPDX-FileCopyrightText: 2016 David Rosca <nowrep@gmail.com>
    SPDX-FileCopyrightText: 2020 David Redondo <kde@david-redondo.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Controls
import org.kde.plasma.private.volume

ComboBox {
    textRole: "Description"
    valueRole: "Index"

    property int deviceIndex: DeviceIndex
    property var streamObject: null

    onActivated: {
        if (!GlobalService.routeStream(streamObject, currentValue)) DeviceIndex = currentValue;
    }

    onDeviceIndexChanged: {
        currentIndex = indexOfValue(DeviceIndex)
    }

    Component.onCompleted: currentIndex = indexOfValue(DeviceIndex)
}
