/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QByteArray>
#include <QStringList>

/**
 * The test signal of the Speaker Setup page: pink noise, one speaker at a time, as interleaved 32 bit float samples.
 *
 * Every speaker gets the same noise (fixed seed), scaled so that its root mean square level over the burst is exactly
 * levelDb dBFS (-20 by default); the speakers not being tested carry exact zeros.
 */
class SpeakerTestSignal
{
public:
    static constexpr double defaultLevelDb = -20.0;
    static constexpr int rate = 48000;

    /** One burst of pink noise of the given length in frames, mono, root mean square levelDb dBFS. */
    static QList<float> pinkNoise(int frames, double levelDb = defaultLevelDb);

    /**
     * The bursts of the speakers in `which` one after the other (each seconds long, then gapSeconds of silence) on the
     * channels named by `positions`, interleaved; a speaker that is not in positions is ignored.
     */
    static QByteArray sequence(const QStringList &positions, const QStringList &which, double seconds, double gapSeconds = 0.5, double levelDb = defaultLevelDb);

    /** The span of speakerIndex's burst in the sequence, in frames: first frame and frame count. */
    static QPair<int, int> burst(int indexInWhich, double seconds, double gapSeconds = 0.5);
};
