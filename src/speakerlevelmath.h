/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QtMath>

/**
 * A speaker's level in dB and the channel volume the sound server holds for it.
 *
 * The Sound page's sliders and PulseAudio count a channel's volume in 65536ths (65536 is 100 %, "0 dB") with a cubic scale:
 * the amplitude is (volume / 65536) ^ 3, so a volume of 65536 * 10^(dB / 60) is a gain of `dB` decibels
 * (pa_sw_volume_to_dB in PulseAudio's volume.c). The Sound page lets a slider go to 100 %, or to 150 % when "Raise maximum
 * volume" is on, which is 60 * log10(1.5) = +10.57 dB.
 */
namespace SpeakerLevelMath
{
constexpr double normalVolume = 65536.0;
constexpr double minimumDb = -30.0;
inline double raisedMaximumDb()
{
    return 60.0 * std::log10(1.5);
}
inline double maximumDb(bool raised)
{
    return raised ? raisedMaximumDb() : 0.0;
}
inline double clampDb(double db, bool raised)
{
    return qBound(minimumDb, db, maximumDb(raised));
}
inline double volumeToDb(qint64 volume)
{
    return volume <= 0 ? -std::numeric_limits<double>::infinity() : 60.0 * std::log10(double(volume) / normalVolume);
}
inline qint64 dbToVolume(double db)
{
    return qint64(std::llround(normalVolume * std::pow(10.0, db / 60.0)));
}
}
