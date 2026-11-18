/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * The person's own sound format settings, as drop-ins for PipeWire and WirePlumber.
 *
 * The edition's sparky-stereo-audio writes 00-sparky-stereo-audio.conf and treats every other file as the person's.
 * This class writes 50-sound-page.conf beside it, in one folder under a base directory (~/.config for one account,
 * /etc for every account), and removes only files that carry its mark.
 * It never computes a default: a key that is absent here is the automatic choice, which is the engine's.
 *
 * State, as a map: "graph" {allowedRates [int], rate, quantum, minQuantum, maxQuantum}, "quality" int,
 * "devices" {nodeName {format, rate, bitPerfect}}. A missing key is automatic.
 */
class SoundFormatConf
{
public:
    static QString fileName();
    static QString mark();

    /** Empty when the state is acceptable, else a code naming the first value refused: "graph.rate", "device.format"... */
    static QString validate(const QVariantMap &state);

    /** Relative path (under the base directory) to file content; an empty content means that file is not wanted. */
    static QMap<QString, QByteArray> render(const QVariantMap &state);

    /** The state the files under baseDir describe (only files that carry the mark). */
    static QVariantMap read(const QString &baseDir);

    /** Make the files under baseDir match the state: write the wanted ones, remove our own that are no longer wanted. */
    static bool write(const QString &baseDir, const QVariantMap &state, QString *error = nullptr);

    /** The formats a node may be asked for, as PipeWire names them. */
    static QStringList knownFormats();
    static bool validNodeName(const QString &name);
};
