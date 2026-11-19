// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos

#include "speakerlevelmath.h"

#include <QTest>

using namespace SpeakerLevelMath;

class SpeakerLevelMathTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void normalVolumeIsZeroDb()
    {
        QCOMPARE(volumeToDb(65536), 0.0);
        QCOMPARE(dbToVolume(0.0), qint64(65536));
    }

    void theScaleIsCubic()
    {
        // pa_sw_volume_to_dB: the amplitude is (volume / 65536)^3, so 50 % of the slider is -18.06 dB, not -6.02 dB
        QVERIFY(qAbs(volumeToDb(32768) + 18.0618) < 1e-3);
        // -6.0206 dB halves the amplitude: the slider stands at 79.37 %
        QVERIFY(qAbs(double(dbToVolume(-6.0206)) / 65536.0 - 0.7937) < 1e-3);
    }

    void roundTrip_data()
    {
        QTest::addColumn<double>("db");
        for (const double db : {-30.0, -12.0, -6.0, -3.5, -0.5, 0.0, 3.0, 10.5}) {
            QTest::newRow(qPrintable(QString::number(db))) << db;
        }
    }
    void roundTrip()
    {
        QFETCH(double, db);
        QVERIFY(qAbs(volumeToDb(dbToVolume(db)) - db) < 0.001);
    }

    void theSoundPagesRange()
    {
        QCOMPARE(minimumDb, -30.0);
        QCOMPARE(maximumDb(false), 0.0);
        // "Raise maximum volume" lets a slider go to 150 %, which is +10.57 dB
        QVERIFY(qAbs(maximumDb(true) - 10.5655) < 1e-3);
        QVERIFY(qAbs(volumeToDb(qint64(65536 * 1.5)) - maximumDb(true)) < 1e-3);
        QCOMPARE(clampDb(-45.0, false), -30.0);
        QCOMPARE(clampDb(6.0, false), 0.0);
        QCOMPARE(clampDb(6.0, true), 6.0);
        QCOMPARE(clampDb(20.0, true), maximumDb(true));
    }

    void silenceHasNoDb()
    {
        QVERIFY(std::isinf(volumeToDb(0)));
        QVERIFY(volumeToDb(0) < 0);
    }
};

QTEST_GUILESS_MAIN(SpeakerLevelMathTest)

#include "speakerlevelmathtest.moc"
