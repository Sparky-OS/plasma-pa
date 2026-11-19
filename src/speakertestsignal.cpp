/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "speakertestsignal.h"

#include <QtMath>
#include <cstring>
#include <random>

QList<float> SpeakerTestSignal::pinkNoise(int frames, double levelDb)
{
    // Paul Kellet's "economy" pink filter on white noise from a generator with a fixed seed: the same burst every time.
    std::mt19937 gen(20261119u);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    QList<double> x(frames);
    double b0 = 0, b1 = 0, b2 = 0;
    double sum = 0;
    for (int i = 0; i < frames; ++i) {
        const double w = uni(gen);
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        x[i] = b0 + b1 + b2 + w * 0.1848;
        sum += x[i];
    }
    const double mean = frames ? sum / frames : 0;
    double sq = 0;
    for (int i = 0; i < frames; ++i) {
        x[i] -= mean;
        sq += x[i] * x[i];
    }
    const double rms = frames ? qSqrt(sq / frames) : 0;
    const double scale = rms > 0 ? qPow(10.0, levelDb / 20.0) / rms : 0;
    QList<float> out(frames);
    for (int i = 0; i < frames; ++i) {
        out[i] = float(x[i] * scale);
    }
    return out;
}

QPair<int, int> SpeakerTestSignal::burst(int indexInWhich, double seconds, double gapSeconds)
{
    const int len = qRound(seconds * rate);
    const int gap = qRound(gapSeconds * rate);
    return {indexInWhich * (len + gap), len};
}

QByteArray SpeakerTestSignal::sequence(const QStringList &positions, const QStringList &which, double seconds, double gapSeconds, double levelDb)
{
    const int channels = positions.size();
    QStringList chosen;
    for (const auto &p : which) {
        if (positions.contains(p)) {
            chosen << p;
        }
    }
    const int len = qRound(seconds * rate);
    const int gap = qRound(gapSeconds * rate);
    const int frames = chosen.size() * (len + gap);
    QByteArray out(frames * channels * int(sizeof(float)), '\0');
    if (chosen.isEmpty() || len <= 0) {
        return out;
    }
    const QList<float> noise = pinkNoise(len, levelDb);
    auto *samples = reinterpret_cast<float *>(out.data());
    for (int n = 0; n < chosen.size(); ++n) {
        const int ch = positions.indexOf(chosen.at(n));
        const int first = burst(n, seconds, gapSeconds).first;
        for (int i = 0; i < len; ++i) {
            samples[(first + i) * channels + ch] = noise.at(i);
        }
    }
    return out;
}
