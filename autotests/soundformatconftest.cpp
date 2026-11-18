// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos

#include "soundformatconf.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class SoundFormatConfTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void validateRefuses_data()
    {
        QTest::addColumn<QVariantMap>("state");
        QTest::addColumn<QString>("code");
        QTest::newRow("rate too low") << QVariantMap{{u"graph"_s, QVariantMap{{u"rate"_s, 100}}}} << u"graph.rate"_s;
        QTest::newRow("rate not allowed") << QVariantMap{{u"graph"_s, QVariantMap{{u"rate"_s, 96000}, {u"allowedRates"_s, QVariantList{44100, 48000}}}}}
                                          << u"graph.rate"_s;
        QTest::newRow("empty rates") << QVariantMap{{u"graph"_s, QVariantMap{{u"allowedRates"_s, QVariantList{}}}}} << u"graph.allowedRates"_s;
        QTest::newRow("quantum not power of two") << QVariantMap{{u"graph"_s, QVariantMap{{u"quantum"_s, 1000}}}} << u"graph.quantum"_s;
        QTest::newRow("quantum above default max") << QVariantMap{{u"graph"_s, QVariantMap{{u"quantum"_s, 4096}}}} << u"graph.quantum"_s;
        QTest::newRow("quality 15") << QVariantMap{{u"quality"_s, 15}} << u"quality"_s;
        QTest::newRow("path as node") << QVariantMap{{u"devices"_s, QVariantMap{{u"../../etc/passwd"_s, QVariantMap{{u"rate"_s, 48000}}}}}} << u"device.name"_s;
        QTest::newRow("foreign node") << QVariantMap{{u"devices"_s, QVariantMap{{u"loopback.x"_s, QVariantMap{{u"rate"_s, 48000}}}}}} << u"device.name"_s;
        QTest::newRow("unknown format") << QVariantMap{{u"devices"_s, QVariantMap{{u"alsa_output.x"_s, QVariantMap{{u"format"_s, u"S99"_s}}}}}} << u"device.format"_s;
        QTest::newRow("unknown key") << QVariantMap{{u"script"_s, u"rm -rf /"_s}} << u"state.script"_s;
    }
    void validateRefuses()
    {
        QFETCH(QVariantMap, state);
        QFETCH(QString, code);
        QCOMPARE(SoundFormatConf::validate(state), code);
    }

    void validateAccepts()
    {
        const QVariantMap state{{u"graph"_s, QVariantMap{{u"rate"_s, 96000}, {u"allowedRates"_s, QVariantList{44100, 48000, 96000}}, {u"quantum"_s, 256}}},
                                {u"quality"_s, 10},
                                {u"devices"_s, QVariantMap{{u"alsa_output.pci-0000_00_1f.3.analog-stereo"_s, QVariantMap{{u"format"_s, u"S32LE"_s}, {u"rate"_s, 96000}, {u"bitPerfect"_s, true}}}}}};
        QCOMPARE(SoundFormatConf::validate(state), QString());
    }

    void roundTrip()
    {
        QTemporaryDir dir;
        const QVariantMap state{{u"graph"_s, QVariantMap{{u"rate"_s, 96000}, {u"allowedRates"_s, QVariantList{44100, 48000, 96000}}, {u"quantum"_s, 256}}},
                                {u"quality"_s, 10},
                                {u"devices"_s, QVariantMap{{u"alsa_output.a"_s, QVariantMap{{u"format"_s, u"S32LE"_s}, {u"rate"_s, 96000}, {u"bitPerfect"_s, true}}},
                                                           {u"bluez_output.b"_s, QVariantMap{{u"rate"_s, 44100}}}}}};
        QString error;
        QVERIFY2(SoundFormatConf::write(dir.path(), state, &error), qPrintable(error));
        const QVariantMap back = SoundFormatConf::read(dir.path());
        QCOMPARE(back.value(u"quality"_s).toInt(), 10);
        QCOMPARE(back.value(u"graph"_s).toMap().value(u"rate"_s).toInt(), 96000);
        QCOMPARE(back.value(u"graph"_s).toMap().value(u"allowedRates"_s).toList().size(), 3);
        QCOMPARE(back.value(u"graph"_s).toMap().value(u"quantum"_s).toInt(), 256);
        const auto devices = back.value(u"devices"_s).toMap();
        QCOMPARE(devices.value(u"alsa_output.a"_s).toMap().value(u"format"_s).toString(), u"S32LE"_s);
        QVERIFY(devices.value(u"alsa_output.a"_s).toMap().value(u"bitPerfect"_s).toBool());
        QCOMPARE(devices.value(u"bluez_output.b"_s).toMap().value(u"rate"_s).toInt(), 44100);
        // The files carry the mark on their first line and use the name that sorts after the engine's 00-.
        QFile f(dir.filePath(u"pipewire/pipewire.conf.d/50-sound-page.conf"_s));
        QVERIFY(f.open(QFile::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readLine()).trimmed(), SoundFormatConf::mark());
        // PipeWire's own "key = value" form, so sparky-stereo-audio's `apply` sees these keys as the person's own.
        f.seek(0);
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.contains(u"\n    default.clock.rate = 96000\n"_s));
        QVERIFY(text.contains(u"\n    default.clock.allowed-rates = [ 44100 48000 96000 ]\n"_s));
    }

    void backToAutomaticRemovesOnlyOurFiles()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(u"pipewire/pipewire.conf.d"_s));
        // A file of the person's, and the engine's own, beside ours.
        for (const auto &name : {u"00-sparky-stereo-audio.conf"_s, u"99-mine.conf"_s}) {
            QFile f(dir.filePath(u"pipewire/pipewire.conf.d/"_s + name));
            QVERIFY(f.open(QFile::WriteOnly));
            f.write("context.properties = { default.clock.rate = 44100 }\n");
        }
        QString error;
        QVERIFY(SoundFormatConf::write(dir.path(), QVariantMap{{u"graph"_s, QVariantMap{{u"rate"_s, 96000}}}}, &error));
        QVERIFY(QFile::exists(dir.filePath(u"pipewire/pipewire.conf.d/50-sound-page.conf"_s)));
        QVERIFY(SoundFormatConf::write(dir.path(), QVariantMap(), &error));
        QVERIFY(!QFile::exists(dir.filePath(u"pipewire/pipewire.conf.d/50-sound-page.conf"_s)));
        QVERIFY(QFile::exists(dir.filePath(u"pipewire/pipewire.conf.d/00-sparky-stereo-audio.conf"_s)));
        QVERIFY(QFile::exists(dir.filePath(u"pipewire/pipewire.conf.d/99-mine.conf"_s)));
    }

    void aForeignFileOfOurNameIsNeverOverwritten()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath(u"pipewire/pipewire.conf.d"_s));
        const QString path = dir.filePath(u"pipewire/pipewire.conf.d/50-sound-page.conf"_s);
        QFile f(path);
        QVERIFY(f.open(QFile::WriteOnly));
        f.write("context.properties = { default.clock.rate = 44100 }\n");
        f.close();
        QString error;
        QVERIFY(!SoundFormatConf::write(dir.path(), QVariantMap{{u"graph"_s, QVariantMap{{u"rate"_s, 96000}}}}, &error));
        QVERIFY(error.startsWith(u"foreign"_s));
        QVERIFY(QFile(path).open(QFile::ReadOnly));
        QVERIFY(SoundFormatConf::write(dir.path(), QVariantMap(), &error)); // not wanted and not ours: left alone
        QVERIFY(QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(SoundFormatConfTest)
#include "soundformatconftest.moc"
