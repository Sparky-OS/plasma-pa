// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos

#include "speakertestsignal.h"

#include <QTest>
#include <QtMath>
#include <complex>
#include <vector>

namespace
{
double rmsDb(const float *p, int stride, int frames)
{
    double sq = 0;
    for (int i = 0; i < frames; ++i) {
        const double v = p[i * stride];
        sq += v * v;
    }
    return 20 * std::log10(std::sqrt(sq / frames));
}

void fft(std::vector<std::complex<double>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

class SpeakerTestSignalTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void theBurstIsAtMinusTwentyDbfs()
    {
        const auto noise = SpeakerTestSignal::pinkNoise(96000);
        QVERIFY(qAbs(rmsDb(noise.constData(), 1, noise.size()) + 20.0) < 0.001);
        float peak = 0;
        for (float v : noise) {
            peak = qMax(peak, qAbs(v));
        }
        QVERIFY(peak < 0.99f);
    }

    void aDifferentLevelIsHonoured()
    {
        const auto noise = SpeakerTestSignal::pinkNoise(48000, -12.0);
        QVERIFY(qAbs(rmsDb(noise.constData(), 1, noise.size()) + 12.0) < 0.001);
    }

    void itIsTheSameBurstEveryTime()
    {
        QCOMPARE(SpeakerTestSignal::pinkNoise(4800), SpeakerTestSignal::pinkNoise(4800));
    }

    void itIsPinkNotWhite()
    {
        // pink noise has the same power in every octave: the level of a band 100 to 200 Hz equals the one 3200 to 6400 Hz within 1.5 dB;
        // white noise would differ by 3 dB per octave, 15 dB over these five.
        const int n = 1 << 17;
        const auto noise = SpeakerTestSignal::pinkNoise(n);
        std::vector<std::complex<double>> a(n);
        for (int i = 0; i < n; ++i) {
            a[i] = double(noise.at(i)) * (0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1)));
        }
        fft(a);
        const double binHz = double(SpeakerTestSignal::rate) / n;
        const auto band = [&](double lo, double hi) {
            double p = 0;
            for (int k = int(lo / binHz); k < int(hi / binHz); ++k) {
                p += std::norm(a[k]);
            }
            return 10 * std::log10(p);
        };
        const double low = band(100, 200);
        for (double f = 200; f < 6400; f *= 2) {
            QVERIFY2(qAbs(band(f, 2 * f) - low) < 1.5, qPrintable(QString::number(f)));
        }
    }

    void onlyTheChosenSpeakerSounds()
    {
        const QStringList positions = {"FL", "FR", "FC", "LFE", "RL", "RR"};
        const QByteArray data = SpeakerTestSignal::sequence(positions, {"FC"}, 1.0, 0.5);
        const auto *p = reinterpret_cast<const float *>(data.constData());
        const int frames = data.size() / int(sizeof(float)) / positions.size();
        QCOMPARE(frames, 48000 + 24000);
        for (int ch = 0; ch < 6; ++ch) {
            if (ch == 2) {
                QVERIFY(qAbs(rmsDb(p + ch, 6, 48000) + 20.0) < 0.001);
                continue;
            }
            for (int i = 0; i < frames; ++i) {
                QCOMPARE(p[i * 6 + ch], 0.0f);
            }
        }
        for (int i = 48000; i < frames; ++i) {
            QCOMPARE(p[i * 6 + 2], 0.0f);
        }
    }

    void theSequenceGoesOneByOne()
    {
        const QStringList positions = {"FL", "FR", "FC", "LFE", "RL", "RR"};
        const QByteArray data = SpeakerTestSignal::sequence(positions, positions, 0.5, 0.25);
        const auto *p = reinterpret_cast<const float *>(data.constData());
        const int frames = data.size() / int(sizeof(float)) / 6;
        QCOMPARE(frames, 6 * (24000 + 12000));
        for (int n = 0; n < 6; ++n) {
            const auto [first, len] = SpeakerTestSignal::burst(n, 0.5, 0.25);
            for (int ch = 0; ch < 6; ++ch) {
                if (ch == n) {
                    QVERIFY(qAbs(rmsDb(p + first * 6 + ch, 6, len) + 20.0) < 0.001);
                } else {
                    for (int i = first; i < first + len; ++i) {
                        QCOMPARE(p[i * 6 + ch], 0.0f);
                    }
                }
            }
        }
    }

    void aSpeakerTheDeviceLacksIsIgnored()
    {
        const QByteArray data = SpeakerTestSignal::sequence({"FL", "FR", "FC"}, {"SL"}, 1.0);
        QVERIFY(data.isEmpty());
    }
};

QTEST_GUILESS_MAIN(SpeakerTestSignalTest)

#include "speakertestsignaltest.moc"
