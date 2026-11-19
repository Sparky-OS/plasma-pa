// SPDX-License-Identifier: BSD-2-Clause
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos

#include "speakersetupconf.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <limits>

using namespace Qt::StringLiterals;

namespace
{
const QString NODE = u"alsa_output.pci-0000_00_1f.3.analog-surround-51"_s;
const QVariantList POS51 = {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s};

QVariantMap withDevice(const QString &node, const QVariantMap &device)
{
    return QVariantMap{{u"devices"_s, QVariantMap{{node, device}}}};
}

QVariantMap dev(const QVariantMap &more)
{
    QVariantMap d{{u"positions"_s, POS51}};
    for (auto it = more.begin(); it != more.end(); ++it) {
        d.insert(it.key(), it.value());
    }
    return d;
}

QVariantMap oneLevel(const QVariant &value)
{
    return withDevice(NODE, dev({{u"levels"_s, QVariantMap{{u"FL"_s, value}}}}));
}

QVariantMap allDistances(double v)
{
    QVariantMap m;
    for (const auto &p : POS51) {
        m.insert(p.toString(), v);
    }
    return m;
}

QByteArray fileText(const QString &baseDir)
{
    QFile f(baseDir + u"/pipewire/pipewire.conf.d/50-speaker-page.conf"_s);
    return f.open(QFile::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

class SpeakerSetupConfTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void validateRefuses_data()
    {
        QTest::addColumn<QVariantMap>("state");
        QTest::addColumn<QString>("code");

        const auto named = [](const QString &name) {
            return withDevice(name, dev({{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        };
        QTest::newRow("unknown top key") << QVariantMap{{u"script"_s, u"rm -rf /"_s}} << u"state.key"_s;
        QTest::newRow("devices not a map") << QVariantMap{{u"devices"_s, u"x"_s}} << u"devices"_s;
        QTest::newRow("path as node") << named(u"../../etc/passwd"_s) << u"device.name"_s;
        QTest::newRow("an input node") << named(u"alsa_input.pci-0000_00_1f.3.analog-stereo"_s) << u"device.name"_s;
        QTest::newRow("foreign node") << named(u"loopback.x"_s) << u"device.name"_s;
        QTest::newRow("the setup of a setup") << named(u"speaker_setup.alsa_output.x"_s) << u"device.name"_s;
        QTest::newRow("name with a double quote") << named(u"alsa_output.x\"y"_s) << u"device.name"_s;
        QTest::newRow("name with a newline in it") << named(u"alsa_output.x\ny"_s) << u"device.name"_s;
        QTest::newRow("name with a final newline") << named(u"alsa_output.x\n"_s) << u"device.name"_s;
        QTest::newRow("name with a carriage return") << named(u"alsa_output.x\ry"_s) << u"device.name"_s;
        QTest::newRow("name with an opening brace") << named(u"alsa_output.x{y"_s) << u"device.name"_s;
        QTest::newRow("name with a closing brace") << named(u"alsa_output.x}"_s) << u"device.name"_s;
        QTest::newRow("name with an opening bracket") << named(u"alsa_output.x[y"_s) << u"device.name"_s;
        QTest::newRow("name with a closing bracket") << named(u"alsa_output.x]"_s) << u"device.name"_s;
        QTest::newRow("name with a space") << named(u"alsa_output.x y"_s) << u"device.name"_s;
        QTest::newRow("name with a hash") << named(u"alsa_output.x#y"_s) << u"device.name"_s;
        QTest::newRow("name with a backslash") << named(u"alsa_output.x\\y"_s) << u"device.name"_s;
        QTest::newRow("name with an equals sign") << named(u"alsa_output.x=y"_s) << u"device.name"_s;
        QTest::newRow("name with a comma") << named(u"alsa_output.x,y"_s) << u"device.name"_s;
        QTest::newRow("name with a NUL") << named(QString(u"alsa_output.x") + QChar(0) + u"y"_s) << u"device.name"_s;
        QTest::newRow("name with a tab") << named(u"alsa_output.x\ty"_s) << u"device.name"_s;
        QTest::newRow("name with a non-ASCII letter") << named(u"alsa_output.xé"_s) << u"device.name"_s;
        QTest::newRow("name at the limit plus one") << named(u"alsa_output."_s + QString(201, u'a')) << u"device.name"_s;
        QTest::newRow("name with nothing after the prefix") << named(u"alsa_output."_s) << u"device.name"_s;

        QTest::newRow("device not a map") << QVariantMap{{u"devices"_s, QVariantMap{{NODE, u"x"_s}}}} << u"device"_s;
        QTest::newRow("unknown device key") << withDevice(NODE, dev({{u"exec"_s, u"x"_s}})) << u"device.key"_s;
        QTest::newRow("no positions") << withDevice(NODE, QVariantMap{{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}) << u"device.positions"_s;
        QTest::newRow("two positions") << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FR"_s}}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})
                                       << u"device.positions"_s;
        QTest::newRow("nine positions")
            << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s, u"SL"_s, u"SR"_s, u"RC"_s}},
                                            {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})
            << u"device.positions"_s;
        QTest::newRow("a repeated position") << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FL"_s, u"FC"_s}}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})
                                             << u"device.positions"_s;
        QTest::newRow("a position that is a node name") << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FR"_s, u"in_FL:In\"] }"_s}}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})
                                                        << u"device.positions"_s;
        QTest::newRow("a position that is a number") << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FR"_s, 3}}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})
                                                     << u"device.positions"_s;
        QTest::newRow("a lower case position") << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"fl"_s, u"FR"_s, u"FC"_s}}, {u"levels"_s, QVariantMap{{u"FR"_s, -3}}}})
                                               << u"device.positions"_s;

        for (const QString &bad : {u"a\u2028b"_s, u"a\u0085b"_s, u"a\u200b" "b"_s, u"a\u00a0\u2029"_s, u"a\uff02b"_s, u"a\uff3cb"_s, u"a\"b"_s, u"a\nb"_s, u"a\n"_s, u"a\\b"_s, u"a{b"_s, u"a}b"_s, u"a[b"_s, u"a]b"_s, u"a#b"_s, u"a=b"_s, u"a,b"_s, u"a\tb"_s, u"a\rb"_s,
                                   u" a"_s, u""_s, u"a;b"_s, u"a$b"_s, u"a`b"_s, u"a<b"_s}) {
            QTest::newRow(qPrintable(u"label "_s + QString::fromLatin1(QByteArray(bad.toUtf8()).toPercentEncoding()))) << withDevice(NODE, dev({{u"label"_s, bad}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}))
                                                                                                                        << u"device.label"_s;
        }
        QTest::newRow("label over the limit") << withDevice(NODE, dev({{u"label"_s, QString(81, u'a')}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})) << u"device.label"_s;
        QTest::newRow("label not a string") << withDevice(NODE, dev({{u"label"_s, 5}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})) << u"device.label"_s;

        QTest::newRow("level below the range") << oneLevel(-30.5) << u"device.levels"_s;
        QTest::newRow("level above the range") << oneLevel(15.5) << u"device.levels"_s;
        QTest::newRow("level as text") << oneLevel(u"-3"_s) << u"device.levels"_s;
        QTest::newRow("level as a bool") << oneLevel(true) << u"device.levels"_s;
        QTest::newRow("level not a number") << oneLevel(std::numeric_limits<double>::quiet_NaN()) << u"device.levels"_s;
        QTest::newRow("level infinite") << oneLevel(std::numeric_limits<double>::infinity()) << u"device.levels"_s;
        QTest::newRow("level for a position the device lacks") << withDevice(NODE, dev({{u"levels"_s, QVariantMap{{u"SL"_s, -3}}}})) << u"device.levels"_s;
        QTest::newRow("level for an unsafe position") << withDevice(NODE, dev({{u"levels"_s, QVariantMap{{u"FL\"x"_s, -3}}}})) << u"device.levels"_s;
        QTest::newRow("levels not a map") << withDevice(NODE, dev({{u"levels"_s, -3}})) << u"device.levels"_s;

        QVariantMap d = allDistances(3);
        d.insert(u"FL"_s, 0.29);
        QTest::newRow("distance below the range") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d.insert(u"FL"_s, 30.1);
        QTest::newRow("distance above the range") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d.insert(u"FL"_s, u"3"_s);
        QTest::newRow("distance as text") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d.insert(u"FL"_s, std::numeric_limits<double>::quiet_NaN());
        QTest::newRow("distance not a number") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d = allDistances(3);
        d.remove(u"RR"_s);
        QTest::newRow("a distance missing") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d = allDistances(3);
        d.insert(u"SL"_s, 3);
        QTest::newRow("a distance too many") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;
        d = allDistances(3);
        d.remove(u"RR"_s);
        d.insert(u"RR\"]"_s, 3);
        QTest::newRow("a distance for an unsafe position") << withDevice(NODE, dev({{u"distances"_s, d}})) << u"device.distances"_s;

        QTest::newRow("small without a subwoofer")
            << withDevice(NODE, QVariantMap{{u"positions"_s, QVariantList{u"FL"_s, u"FR"_s, u"FC"_s}}, {u"small"_s, QVariantList{u"FL"_s}}}) << u"device.small"_s;
        QTest::newRow("the subwoofer small") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"LFE"_s}}})) << u"device.small"_s;
        QTest::newRow("small speaker the device lacks") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"SL"_s}}})) << u"device.small"_s;
        QTest::newRow("small twice") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s, u"FL"_s}}})) << u"device.small"_s;
        QTest::newRow("small empty") << withDevice(NODE, dev({{u"small"_s, QVariantList{}}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})) << u"device.small"_s;
        QTest::newRow("small as text") << withDevice(NODE, dev({{u"small"_s, u"FL"_s}})) << u"device.small"_s;
        QTest::newRow("small unsafe") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL\"] x"_s}}})) << u"device.small"_s;

        QTest::newRow("crossover without small speakers") << withDevice(NODE, dev({{u"crossover"_s, 80}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})) << u"device.crossover"_s;
        QTest::newRow("crossover too low") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, 39}})) << u"device.crossover"_s;
        QTest::newRow("crossover too high") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, 251}})) << u"device.crossover"_s;
        QTest::newRow("crossover not whole") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, 80.5}})) << u"device.crossover"_s;
        QTest::newRow("crossover as text") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, u"80"_s}})) << u"device.crossover"_s;
        QTest::newRow("crossover not a number")
            << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, std::numeric_limits<double>::quiet_NaN()}})) << u"device.crossover"_s;

        QTest::newRow("a device with nothing set") << withDevice(NODE, dev({})) << u"device.empty"_s;
        QTest::newRow("only zero levels") << withDevice(NODE, dev({{u"levels"_s, QVariantMap{{u"FL"_s, 0}}}})) << u"device.empty"_s;
        QTest::newRow("only equal distances") << withDevice(NODE, dev({{u"distances"_s, allDistances(2.5)}})) << u"device.empty"_s;
    }
    void validateRefuses()
    {
        QFETCH(QVariantMap, state);
        QFETCH(QString, code);
        QCOMPARE(SpeakerSetupConf::validate(state), code);
    }

    void tooManyDevices()
    {
        QVariantMap devices;
        for (int i = 0; i < 9; ++i) {
            devices.insert(u"alsa_output.card%1"_s.arg(i), dev({{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        }
        QCOMPARE(SpeakerSetupConf::validate(QVariantMap{{u"devices"_s, devices}}), u"devices"_s);
    }

    void validateAccepts_data()
    {
        QTest::addColumn<QVariantMap>("state");
        QTest::newRow("nothing at all") << QVariantMap{};
        QTest::newRow("a level") << oneLevel(-3.5);
        QTest::newRow("the largest level") << oneLevel(15);
        QTest::newRow("the smallest level") << oneLevel(-30);
        QTest::newRow("distances that differ") << withDevice(NODE, dev({{u"distances"_s, [] {
                                                                           QVariantMap m = allDistances(3);
                                                                           m.insert(u"RL"_s, 1.8);
                                                                           return m;
                                                                       }()}}));
        QTest::newRow("small speakers and the crossover") << withDevice(NODE, dev({{u"small"_s, QVariantList{u"FL"_s, u"FR"_s, u"FC"_s}}, {u"crossover"_s, 120}}));
        QTest::newRow("a label with letters of another script") << withDevice(NODE, dev({{u"label"_s, u"Alto-falantes (áudio 5.1) 標準"_s}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        QTest::newRow("a label with the fullwidth colon of a Chinese translation") << withDevice(NODE, dev({{u"label"_s, u"\u626c\u58f0\u5668\u8bbe\u7f6e\uff1arig 71"_s}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        QTest::newRow("a label with fullwidth parentheses and an ideographic space") << withDevice(NODE, dev({{u"label"_s, u"\u30b9\u30d4\u30fc\u30ab\u30fc\uff08rig\u3000\u0035\uff09"_s}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        QTest::newRow("a label with a combining mark") << withDevice(NODE, dev({{u"label"_s, u"Cafe\u0301 audio"_s}, {u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        QTest::newRow("a real bluez name") << withDevice(u"bluez_output.AA_BB_CC_DD_EE_FF.1"_s, dev({{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
        QTest::newRow("a usb name with plus and colon") << withDevice(u"alsa_output.usb-Rig_DAC+Dock-00.analog-surround-71:1"_s, dev({{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}}));
    }
    void validateAccepts()
    {
        QFETCH(QVariantMap, state);
        QCOMPARE(SpeakerSetupConf::validate(state), QString());
    }

    void defaultsWriteNothing()
    {
        QCOMPARE(SpeakerSetupConf::render(QVariantMap{}).value(u"pipewire/pipewire.conf.d/50-speaker-page.conf"_s), QByteArray());
        QTemporaryDir dir;
        QVERIFY(SpeakerSetupConf::write(dir.path(), QVariantMap{}));
        QVERIFY(fileText(dir.path()).isEmpty());
    }

    void thePredictedNumbers()
    {
        QCOMPARE(SpeakerSetupConf::gain(0), 1.0);
        QVERIFY(qAbs(SpeakerSetupConf::gain(-6.0206) - 0.5) < 1e-4);
        QVERIFY(qAbs(SpeakerSetupConf::gain(20) - 10.0) < 1e-12);
        // 3.43 m nearer than the farthest speaker is 10 ms
        QVERIFY(qAbs(SpeakerSetupConf::delaySeconds(2.0, 5.43) - 0.01) < 1e-12);
        QCOMPARE(SpeakerSetupConf::delaySeconds(5.0, 5.0), 0.0);
        QCOMPARE(SpeakerSetupConf::delaySeconds(6.0, 5.0), 0.0);
    }

    void theFileHoldsWhatTheStateNames()
    {
        QVariantMap d = allDistances(3.0);
        d.insert(u"FL"_s, 2.0);
        d.insert(u"RR"_s, 3.686);
        const QVariantMap state = withDevice(NODE,
                                             dev({{u"label"_s, u"Surround 5.1"_s},
                                                  {u"levels"_s, QVariantMap{{u"FC"_s, 6}, {u"LFE"_s, -4}}},
                                                  {u"distances"_s, d},
                                                  {u"small"_s, QVariantList{u"FL"_s, u"FR"_s}},
                                                  {u"crossover"_s, 100}}));
        QTemporaryDir dir;
        QString error;
        QVERIFY2(SpeakerSetupConf::write(dir.path(), state, &error), qPrintable(error));
        const QString text = QString::fromUtf8(fileText(dir.path()));
        QVERIFY(text.startsWith(SpeakerSetupConf::mark() + u"\n"_s));
        QVERIFY(text.contains(u"node.name = \"speaker_setup.%1\""_s.arg(NODE)));
        QVERIFY(text.contains(u"target.object = \"%1\""_s.arg(NODE)));
        QVERIFY(text.contains(u"audio.position = [ FL FR FC LFE RL RR ]"_s));
        // FC +6 dB, LFE -4 dB
        QVERIFY(text.contains(u"Mult = %1"_s.arg(QString::number(SpeakerSetupConf::gain(6), 'f', 9))));
        QVERIFY(text.contains(u"Mult = %1"_s.arg(QString::number(SpeakerSetupConf::gain(-4), 'f', 9))));
        // the farthest speaker, RR, has no delay node; FL is 1.686 m nearer
        QVERIFY(!text.contains(u"name = d_RR "_s));
        QVERIFY(text.contains(u"\"Delay (s)\" = %1"_s.arg(QString::number(1.686 / 343.0, 'f', 9))));
        // the crossover, twice in each direction for each small speaker, at 100 Hz
        QCOMPARE(text.count(u"label = bq_highpass"_s), 4);
        QCOMPARE(text.count(u"label = bq_lowpass"_s), 4);
        QCOMPARE(text.count(u"Freq = 100.000000000"_s), 8);
        QVERIFY(text.contains(u"{ output = \"lp2_FL:Out\" input = \"mix_LFE:In 2\" }"_s));
        QVERIFY(text.contains(u"{ output = \"lp2_FR:Out\" input = \"mix_LFE:In 3\" }"_s));
        QVERIFY(!text.contains(u"hp1_FC"_s));
        // the person's words cannot have reached anything but the description
        QVERIFY(text.contains(u"node.description = \"Surround 5.1\""_s));
    }

    void everyKeyInTheFileIsOneWeKnow()
    {
        QVariantMap d = allDistances(3.0);
        d.insert(u"FL"_s, 2.0);
        const QVariantMap state = withDevice(NODE,
                                             dev({{u"levels"_s, QVariantMap{{u"FC"_s, 6}, {u"LFE"_s, -4}, {u"FL"_s, 1}}},
                                                  {u"distances"_s, d},
                                                  {u"small"_s, QVariantList{u"FL"_s, u"FR"_s, u"FC"_s, u"RL"_s, u"RR"_s}}}));
        QTemporaryDir dir;
        QVERIFY(SpeakerSetupConf::write(dir.path(), state));
        QString text = QString::fromUtf8(fileText(dir.path()));
        // drop the two comment lines, then every word that stands before an equals sign must be one PipeWire's filter chain takes
        text = text.mid(text.indexOf(u"context.modules"_s));
        static const QSet<QString> known = {u"context.modules"_s, u"name"_s, u"flags"_s, u"args"_s, u"node.description"_s, u"media.name"_s, u"filter.graph"_s,
                                            u"nodes"_s, u"type"_s, u"label"_s, u"config"_s, u"control"_s, u"links"_s, u"output"_s, u"input"_s, u"inputs"_s,
                                            u"outputs"_s, u"capture.props"_s, u"playback.props"_s, u"node.name"_s, u"media.class"_s, u"audio.channels"_s,
                                            u"audio.position"_s, u"target.object"_s, u"node.passive"_s, u"stream.dont-remix"_s,
                                            u"Mult"_s, u"Add"_s, u"Freq"_s, u"Q"_s, u"max-delay"_s, u"Delay (s)"_s, u"Gain 1"_s, u"Gain 2"_s, u"Gain 3"_s,
                                            u"Gain 4"_s, u"Gain 5"_s, u"Gain 6"_s, u"Gain 7"_s, u"Gain 8"_s};
        static const QRegularExpression key(u"(\"[^\"]+\"|[A-Za-z][A-Za-z0-9_.-]*)\\s*=\\s"_s);
        auto it = key.globalMatch(text);
        int n = 0;
        while (it.hasNext()) {
            QString k = it.next().captured(1);
            if (k.startsWith(u'"')) {
                k = k.mid(1, k.size() - 2);
            }
            QVERIFY2(known.contains(k), qPrintable(k));
            ++n;
        }
        QVERIFY(n > 40);
        // braces and brackets balance, so no text of the person's closed a block early
        QCOMPARE(text.count(u'{'), text.count(u'}'));
        QCOMPARE(text.count(u'['), text.count(u']'));
    }

    void roundTrip()
    {
        const QVariantMap state = withDevice(NODE, dev({{u"levels"_s, QVariantMap{{u"FC"_s, 2.5}}}, {u"small"_s, QVariantList{u"FL"_s}}, {u"crossover"_s, 80}}));
        QTemporaryDir dir;
        QVERIFY(SpeakerSetupConf::write(dir.path(), state));
        const QVariantMap got = SpeakerSetupConf::read(dir.path());
        QCOMPARE(SpeakerSetupConf::validate(got), QString());
        QCOMPARE(QJsonDocument::fromVariant(got).toJson(QJsonDocument::Compact), QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact));
    }

    void anUnacceptableStateIsNeverWritten()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY(!SpeakerSetupConf::write(dir.path(), withDevice(u"alsa_output.x\n"_s, dev({{u"levels"_s, QVariantMap{{u"FL"_s, -3}}}})), &error));
        QVERIFY(error.startsWith(u"invalid: device.name"_s));
        QVERIFY(fileText(dir.path()).isEmpty());
    }

    void backToDefaultsRemovesOnlyOurFile()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + u"/pipewire/pipewire.conf.d"_s));
        QFile mine(dir.path() + u"/pipewire/pipewire.conf.d/50-sound-page.conf"_s);
        QVERIFY(mine.open(QFile::WriteOnly));
        mine.write("# the Sound Quality page's file\ncontext.properties = { default.clock.rate = 96000 }\n");
        mine.close();
        QVERIFY(SpeakerSetupConf::write(dir.path(), oneLevel(-3)));
        QVERIFY(!fileText(dir.path()).isEmpty());
        QVERIFY(SpeakerSetupConf::write(dir.path(), QVariantMap{}));
        QVERIFY(fileText(dir.path()).isEmpty());
        QVERIFY(QFile::exists(mine.fileName()));
    }

    void aForeignFileOfOurNameIsNeverOverwritten()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.path() + u"/pipewire/pipewire.conf.d"_s));
        QFile foreign(dir.path() + u"/pipewire/pipewire.conf.d/50-speaker-page.conf"_s);
        QVERIFY(foreign.open(QFile::WriteOnly));
        foreign.write("# somebody else's file\n");
        foreign.close();
        QString error;
        QVERIFY(!SpeakerSetupConf::write(dir.path(), oneLevel(-3), &error));
        QVERIFY(error.startsWith(u"foreign: "_s));
        QVERIFY(SpeakerSetupConf::write(dir.path(), QVariantMap{}));
        QCOMPARE(fileText(dir.path()), QByteArray("# somebody else's file\n"));
    }

    void whatTheHelperReadsIsTheSameState_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("code");
        const QByteArray head = "{\"devices\":{\"alsa_output.x\":{\"positions\":[\"FL\",\"FR\",\"FC\",\"LFE\",\"RL\",\"RR\"],";
        QTest::newRow("numbers as JSON doubles") << head + "\"levels\":{\"FL\":-3.5},\"small\":[\"FL\"],\"crossover\":80.0}}}" << QString();
        QTest::newRow("a quote in a label escaped in JSON") << head + "\"label\":\"a\\\"b\",\"levels\":{\"FL\":-3}}}}" << u"device.label"_s;
        QTest::newRow("a newline in a label escaped in JSON") << head + "\"label\":\"a\\nb\",\"levels\":{\"FL\":-3}}}}" << u"device.label"_s;
        QTest::newRow("a final newline in a node name escaped in JSON")
            << QByteArray("{\"devices\":{\"alsa_output.x\\n\":{\"positions\":[\"FL\",\"FR\",\"FC\"],\"levels\":{\"FL\":-3}}}}") << u"device.name"_s;
        QTest::newRow("a unicode escape of a quote") << head + "\"label\":\"a\\u0022b\",\"levels\":{\"FL\":-3}}}}" << u"device.label"_s;
        QTest::newRow("a level as a string") << head + "\"levels\":{\"FL\":\"-3\"}}}}" << u"device.levels"_s;
        QTest::newRow("a level out of range") << head + "\"levels\":{\"FL\":1e300}}}}" << u"device.levels"_s;
        QTest::newRow("a crossover with a fraction") << head + "\"small\":[\"FL\"],\"crossover\":80.5}}}" << u"device.crossover"_s;
        QTest::newRow("an extra top level key") << QByteArray("{\"devices\":{},\"path\":\"/etc/shadow\"}") << u"state.key"_s;
    }
    void whatTheHelperReadsIsTheSameState()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, code);
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(json, &parse);
        QCOMPARE(parse.error, QJsonParseError::NoError);
        QCOMPARE(SpeakerSetupConf::validate(doc.object().toVariantMap()), code);
    }

    void theAnglesAreTheStandards()
    {
        const QStringList p51 = {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s};
        const QStringList p71 = {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s, u"SL"_s, u"SR"_s};
        QCOMPARE(SpeakerSetupConf::angle(u"FC"_s, p51), 0.0);
        QCOMPARE(SpeakerSetupConf::angle(u"FL"_s, p51), -30.0);
        QCOMPARE(SpeakerSetupConf::angle(u"FR"_s, p51), 30.0);
        QCOMPARE(SpeakerSetupConf::angle(u"RL"_s, p51), -110.0);
        QCOMPARE(SpeakerSetupConf::angle(u"RR"_s, p51), 110.0);
        QCOMPARE(SpeakerSetupConf::angle(u"SL"_s, p71), -90.0);
        QCOMPARE(SpeakerSetupConf::angle(u"RL"_s, p71), -120.0);
        QCOMPARE(SpeakerSetupConf::angle(u"RR"_s, p71), 120.0);
    }
};

QTEST_GUILESS_MAIN(SpeakerSetupConfTest)

#include "speakersetupconftest.moc"
