/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>

/**
 * The person's own speaker setup, as a PipeWire drop-in.
 *
 * One file, 50-speaker-page.conf in pipewire/pipewire.conf.d under a base directory (~/.config for one account, /etc for
 * every account), beside ORDER 20's 50-sound-page.conf and the edition's 00-sparky-stereo-audio.conf. It holds one
 * filter-chain sink per device the person set up. Nothing is written for a device whose speakers are all at their
 * defaults (levels at 0 dB, equal distances, every speaker large): the device then has no filter in its signal path.
 * The class never computes a default; a key that is absent is the default.
 *
 * State, as a map: "devices" { nodeName { "positions" [ch], "label" text, "levels" {ch: dB}, "distances" {ch: metres},
 * "small" [ch], "crossover" Hz } }. "positions" is the channel map of the device's current profile and the only
 * required key. "distances", when present, names every position; the delays are relative to the farthest speaker.
 */
class SpeakerSetupConf
{
public:
    static QString fileName();
    static QString mark();

    /** Empty when the state is acceptable, else a code naming the first value refused: "device.name", "device.levels"... */
    static QString validate(const QVariantMap &state);

    /** Relative path (under the base directory) to file content; an empty content means that file is not wanted. */
    static QMap<QString, QByteArray> render(const QVariantMap &state);

    /** The state the file under baseDir describes (only a file that carries the mark and a state this class accepts). */
    static QVariantMap read(const QString &baseDir);

    /** Make the file under baseDir match the state: write it, or remove our own file when nothing is wanted. */
    static bool write(const QString &baseDir, const QVariantMap &state, QString *error = nullptr);

    static bool validNodeName(const QString &name);
    static bool validLabel(const QString &label);
    /** The text with every character a label does not accept removed, trimmed and cut to maxCharacters; empty when it does not start with a letter or digit. */
    static QString sanitiseLabel(const QString &text, int maxCharacters);
    static bool validPosition(const QString &position);
    static QStringList knownPositions();

    /** The name of the sink a device's setup makes. */
    static QString sinkName(const QString &nodeName);
    /** A node name made by this class (a setup of a setup is not offered). */
    static bool isSetupSink(const QString &nodeName);

    /**
     * Where a position stands in the room, in degrees clockwise from the front centre (ITU-R BS.775-4, Fig. 1 and Note 4):
     * L and R at 30, C at 0, the surrounds of a 5.1 at 110 (the middle of the 100 to 120 sector), and with side and rear
     * pairs both, at equal intervals on the arc from 60 to 150: sides at 90, rears at 120. The subwoofer has no angle (180 is
     * returned for it and the page draws it apart).
     */
    static double angle(const QString &position, const QStringList &positions);

    static constexpr double speedOfSound = 343.0; // m/s, dry air at 20 C
    static constexpr double minLevel = -30.0;
    static constexpr double maxLevel = 15.0;
    static constexpr double minDistance = 0.3;
    static constexpr double maxDistance = 30.0;
    static constexpr int minCrossover = 40;
    static constexpr int maxCrossover = 250;
    static constexpr int defaultCrossover = 80;

    /** The linear gain of a level in dB, and the delay in seconds of a speaker at distance (m) when the farthest stands at farthest (m). */
    static double gain(double db);
    static double delaySeconds(double distance, double farthest);
};
