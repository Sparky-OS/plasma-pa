/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "soundformats.h"

#include "soundformatconf.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <KAuth/Action>
#include <KAuth/ExecuteJob>
#include <KLocalizedString>

using namespace Qt::StringLiterals;

namespace
{
// The rates of the standards a continuous range stands for (the same list the edition's sound defaults use for a range).
const QList<int> STANDARD_RATES = {32000, 44100, 48000, 88200, 96000, 176400, 192000};

QString root()
{
    return qEnvironmentVariable("SPARKY_STEREO_AUDIO_ROOT");
}

QString procAsound()
{
    return qEnvironmentVariable("SPARKY_STEREO_AUDIO_PROC", u"/proc/asound"_s);
}

QList<int> ints(const QString &text)
{
    QList<int> out;
    static const QRegularExpression re(u"\\d+"_s);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        out << it.next().captured().toInt();
    }
    return out;
}

QVariantList toList(const QList<int> &v)
{
    QVariantList out;
    for (int i : v) {
        out << i;
    }
    return out;
}

// PipeWire's names for sample formats, from what pw-dump prints, to the ones a person reads and the drop-in takes.
QString normaliseFormat(QString f)
{
    f = f.toUpper();
    if (f.endsWith(u"P"_s) && f != u"S24_32P"_s) { // planar twin of an interleaved format
        f.chop(1);
    }
    if (f == u"S16"_s || f == u"S24"_s || f == u"S32"_s || f == u"F32"_s || f == u"S24_32"_s) {
        f += u"LE"_s;
    }
    return f;
}

int formatBits(const QString &f)
{
    if (f.startsWith(u"S16"_s)) {
        return 16;
    }
    if (f.startsWith(u"S24_32"_s)) {
        return 24;
    }
    if (f.startsWith(u"S24"_s)) {
        return 24;
    }
    if (f.startsWith(u"S32"_s) || f.startsWith(u"F32"_s)) {
        return 32;
    }
    return 0;
}

QString formatLabel(const QString &f)
{
    if (f.startsWith(u"F32"_s)) {
        return u"32 bit float"_s;
    }
    const int bits = formatBits(f);
    return bits ? QString::number(bits) + u" bit"_s : f;
}

void collectChoice(const QJsonValue &v, QSet<int> *rates, QSet<QString> *formats)
{
    if (v.isDouble()) {
        rates->insert(v.toInt());
    } else if (v.isString()) {
        formats->insert(normaliseFormat(v.toString()));
    } else if (v.isObject()) {
        const auto o = v.toObject();
        if (o.contains(u"min"_s) && o.contains(u"max"_s) && o.value(u"min"_s).isDouble()) {
            const int lo = o.value(u"min"_s).toInt();
            const int hi = o.value(u"max"_s).toInt();
            for (int r : STANDARD_RATES) {
                if (r >= lo && r <= hi) {
                    rates->insert(r);
                }
            }
        }
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == u"min"_s || it.key() == u"max"_s || it.key() == u"step"_s) {
                continue;
            }
            collectChoice(it.value(), rates, formats);
        }
    } else if (v.isArray()) {
        for (const auto &e : v.toArray()) {
            collectChoice(e, rates, formats);
        }
    }
}

QString graphLabel(const QString &key)
{
    if (key == u"rate"_s) {
        return i18nd("kcm_pulseaudio", "default rate");
    }
    if (key == u"quantum"_s) {
        return i18nd("kcm_pulseaudio", "buffer size");
    }
    if (key == u"minQuantum"_s) {
        return i18nd("kcm_pulseaudio", "smallest buffer size");
    }
    return i18nd("kcm_pulseaudio", "largest buffer size");
}

QString describeWriteError(const QString &code)
{
    const int colon = code.indexOf(u": "_s);
    const QString kind = colon > 0 ? code.left(colon) : code;
    const QString path = colon > 0 ? code.mid(colon + 2) : QString();
    if (kind == u"foreign"_s) {
        return i18nd("kcm_pulseaudio", "the file %1 is not one this page wrote, so it was left alone", path);
    }
    if (kind == u"mkdir"_s || kind == u"write"_s || kind == u"remove"_s) {
        return i18nd("kcm_pulseaudio", "the file %1 could not be written", path);
    }
    return i18nd("kcm_pulseaudio", "the settings were not accepted");
}

QString propString(const QJsonObject &props, const QString &key)
{
    const auto v = props.value(key);
    if (v.isString()) {
        return v.toString();
    }
    if (v.isDouble()) {
        return QString::number(v.toInt());
    }
    if (v.isBool()) {
        return v.toBool() ? u"true"_s : u"false"_s;
    }
    return {};
}
} // namespace

SoundFormats::SoundFormats(QObject *parent)
    : QObject(parent)
{
    refresh();
}

QString SoundFormats::userBase() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

void SoundFormats::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void SoundFormats::setMessage(const QString &type, const QString &text)
{
    m_message = type.isEmpty() ? QVariantMap() : QVariantMap{{u"type"_s, type}, {u"text"_s, text}};
    Q_EMIT messageChanged();
}

void SoundFormats::dismissMessage()
{
    setMessage({}, {});
}

void SoundFormats::wait(int ms) const
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

SoundFormats::Run SoundFormats::run(const QString &program, const QStringList &args, int timeoutMs, const QByteArray &input) const
{
    Run r;
    const QString exe = QStandardPaths::findExecutable(program);
    if (exe.isEmpty()) {
        r.failedToStart = true;
        return r;
    }
    QProcess p;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&p, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&p, &QProcess::errorOccurred, &loop, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            r.failedToStart = true;
            loop.quit();
        }
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
        r.timedOut = true;
        loop.quit();
    });
    p.start(exe, args);
    if (!input.isEmpty()) {
        p.write(input);
    }
    if (p.state() != QProcess::NotRunning) {
        // Closed for every program: a reader of standard input must see its end.
        p.closeWriteChannel();
        timer.start(timeoutMs);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        p.waitForFinished(1000);
    }
    r.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    return r;
}

QJsonDocument SoundFormats::pwDump() const
{
    const Run r = run(u"pw-dump"_s, {}, 5000);
    if (r.failedToStart || r.code != 0) {
        return {};
    }
    return QJsonDocument::fromJson(r.out);
}

QVariantMap SoundFormats::readEngine() const
{
    QVariantMap e;
    QFile f(root() + u"/var/lib/sparky-stereo-audio/choices.json"_s);
    if (!f.open(QFile::ReadOnly)) {
        e.insert(u"available"_s, false);
    } else {
        const auto o = QJsonDocument::fromJson(f.readAll()).object();
        e.insert(u"available"_s, !o.isEmpty());
        e.insert(u"rates"_s, o.value(u"rates"_s).toVariant());
        e.insert(u"defaultRate"_s, o.value(u"default"_s).toInt());
        e.insert(u"quality"_s, o.value(u"quality"_s).toInt());
        e.insert(u"cpu"_s, o.value(u"cpu"_s).toString());
        e.insert(u"sixteenOnly"_s, o.value(u"sixteen"_s).toBool());
        e.insert(u"cards"_s, o.value(u"cards"_s).toVariant());
        const auto cost = o.value(u"cost"_s).toObject();
        const auto cq = cost.value(QString::number(o.value(u"quality"_s).toInt()));
        e.insert(u"qualityCost"_s, cq.isDouble() ? cq.toDouble() : -1.0);
        e.insert(u"limit"_s, o.value(u"limit"_s).toDouble());
    }
    // The engine's own sentences, in the person's language, are the reasons the page shows.
    const Run r = run(u"sparky-stereo-audio"_s, {u"show"_s}, 8000);
    e.insert(u"reason"_s, r.failedToStart ? QString() : QString::fromUtf8(r.out).trimmed());
    e.insert(u"installed"_s, !r.failedToStart);
    return e;
}

QVariantMap SoundFormats::readGraph() const
{
    QVariantMap g;
    const Run r = run(u"pw-metadata"_s, {u"-n"_s, u"settings"_s}, 4000);
    g.insert(u"available"_s, !r.failedToStart && r.code == 0);
    static const QRegularExpression re(u"key:'([^']+)' value:'([^']*)'"_s);
    auto it = re.globalMatch(QString::fromUtf8(r.out));
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1);
        const QString value = m.captured(2);
        if (key == u"clock.allowed-rates"_s) {
            g.insert(u"allowedRates"_s, toList(ints(value)));
        } else if (key == u"clock.rate"_s) {
            g.insert(u"rate"_s, value.toInt());
        } else if (key == u"clock.quantum"_s) {
            g.insert(u"quantum"_s, value.toInt());
        } else if (key == u"clock.min-quantum"_s) {
            g.insert(u"minQuantum"_s, value.toInt());
        } else if (key == u"clock.max-quantum"_s) {
            g.insert(u"maxQuantum"_s, value.toInt());
        } else if (key == u"clock.force-rate"_s) {
            g.insert(u"forceRate"_s, value.toInt());
        } else if (key == u"clock.force-quantum"_s) {
            g.insert(u"forceQuantum"_s, value.toInt());
        }
    }
    return g;
}

QVariantMap SoundFormats::parseEld(const QString &text)
{
    if (!text.contains(QRegularExpression(u"^monitor_present\\s+1"_s, QRegularExpression::MultilineOption))
        || !text.contains(QRegularExpression(u"^eld_valid\\s+1"_s, QRegularExpression::MultilineOption))) {
        return {};
    }
    QVariantMap out;
    const auto mon = QRegularExpression(u"^monitor_name\\s+(.*)$"_s, QRegularExpression::MultilineOption).match(text);
    out.insert(u"monitor"_s, mon.hasMatch() ? mon.captured(1).trimmed() : QString());
    QVariantList lpcm;
    QVariantList encoded;
    for (int i = 0; i < 16; ++i) {
        const auto ct = QRegularExpression(u"^sad%1_coding_type\\s+\\[0x([0-9a-fA-F]+)\\]\\s*(.*)$"_s.arg(i), QRegularExpression::MultilineOption).match(text);
        if (!ct.hasMatch()) {
            continue;
        }
        const auto field = [&](const QString &name) {
            const auto m = QRegularExpression(u"^sad%1_%2\\s+(?:\\[[^\\]]*\\])?\\s*(.*)$"_s.arg(i).arg(name), QRegularExpression::MultilineOption).match(text);
            return m.hasMatch() ? m.captured(1) : QString();
        };
        const int channels = field(u"channels"_s).trimmed().toInt();
        const QList<int> rates = ints(field(u"rates"_s));
        QVariantMap entry{{u"coding"_s, ct.captured(2).trimmed()}, {u"channels"_s, channels}, {u"rates"_s, toList(rates)}};
        if (ct.captured(1).toInt(nullptr, 16) == 1) { // coding type 1 is LPCM (CTA-861 audio data block)
            entry.insert(u"bits"_s, toList(ints(field(u"bits"_s))));
            lpcm << entry;
        } else {
            encoded << entry;
        }
    }
    out.insert(u"lpcm"_s, lpcm);
    out.insert(u"encoded"_s, encoded);
    return out;
}

QVariantList SoundFormats::readDevices(const QVariantMap &engine, QVariantMap *live) const
{
    QVariantList out;
    const auto doc = pwDump();
    if (!doc.isArray()) {
        return out;
    }
    const QVariantList cards = engine.value(u"cards"_s).toList();
    QMap<QString, QVariantMap> person; // the effective choice per node: every account's, then this account's
    for (const auto &st : {m_system, m_user}) {
        const QVariantMap devs = st.value(u"devices"_s).toMap();
        for (auto it = devs.begin(); it != devs.end(); ++it) {
            QVariantMap merged = person.value(it.key());
            merged.insert(it.value().toMap());
            person.insert(it.key(), merged);
        }
    }
    for (const auto &item : doc.array()) {
        const auto obj = item.toObject();
        if (obj.value(u"type"_s).toString() != u"PipeWire:Interface:Node"_s) {
            continue;
        }
        const auto info = obj.value(u"info"_s).toObject();
        const auto props = info.value(u"props"_s).toObject();
        const QString cls = propString(props, u"media.class"_s);
        if (cls != u"Audio/Sink"_s && cls != u"Audio/Source"_s) {
            continue;
        }
        const QString name = propString(props, u"node.name"_s);
        if (!SoundFormatConf::validNodeName(name) || propString(props, u"node.virtual"_s) == u"true"_s) {
            continue;
        }
        const auto params = info.value(u"params"_s).toObject();
        QSet<int> rates;
        QSet<QString> formats;
        for (const auto &ef : params.value(u"EnumFormat"_s).toArray()) {
            const auto o = ef.toObject();
            collectChoice(o.value(u"rate"_s), &rates, &formats);
            if (o.value(u"format"_s).isString() || o.value(u"format"_s).isObject()) {
                QSet<int> ignore;
                collectChoice(o.value(u"format"_s), &ignore, &formats);
            }
        }
        QVariantMap inUse;
        const auto fmt = params.value(u"Format"_s).toArray();
        if (!fmt.isEmpty()) {
            const auto f = fmt.at(0).toObject();
            if (f.value(u"rate"_s).isDouble()) {
                inUse.insert(u"rate"_s, f.value(u"rate"_s).toInt());
            }
            if (f.value(u"format"_s).isString()) {
                inUse.insert(u"format"_s, normaliseFormat(f.value(u"format"_s).toString()));
            }
            if (f.value(u"channels"_s).isDouble()) {
                inUse.insert(u"channels"_s, f.value(u"channels"_s).toInt());
            }
        }
        // Ordered lists: planar twins and the like collapse to the interleaved names the drop-in takes.
        QList<int> rateList = rates.values();
        std::sort(rateList.begin(), rateList.end());
        QStringList formatList;
        for (const auto &f : SoundFormatConf::knownFormats()) {
            if (formats.contains(f)) {
                formatList << f;
            }
        }
        // A choice of the person's narrows what PipeWire enumerates for the device, so the full list then comes from the
        // card as the edition's sound defaults read it, kept beside what PipeWire says.
        if (!person.value(name).isEmpty()) {
            int cardNo = -1;
            for (const auto &k : {u"api.alsa.pcm.card"_s, u"alsa.card"_s, u"api.alsa.card"_s}) {
                if (!propString(props, k).isEmpty()) {
                    cardNo = propString(props, k).toInt();
                    break;
                }
            }
            for (const auto &c : cards) {
                const auto cm = c.toMap();
                if (cm.value(u"card"_s).toInt() != cardNo) {
                    continue;
                }
                for (const auto &o : cm.value(u"outputs"_s).toList()) {
                    for (const auto &r : o.toMap().value(u"rates"_s).toList()) {
                        if (!rateList.contains(r.toInt())) {
                            rateList << r.toInt();
                        }
                    }
                    for (const auto &b : o.toMap().value(u"bits"_s).toList()) {
                        const QString f = b.toInt() == 16 ? u"S16LE"_s : (b.toInt() >= 32 ? u"S32LE"_s : (b.toInt() >= 20 ? u"S24LE"_s : QString()));
                        if (!f.isEmpty() && !formatList.contains(f)) {
                            formatList << f;
                        }
                    }
                }
            }
            std::sort(rateList.begin(), rateList.end());
            QStringList ordered;
            for (const auto &f : SoundFormatConf::knownFormats()) {
                if (formatList.contains(f)) {
                    ordered << f;
                }
            }
            formatList = ordered;
        }
        const QString lower = (name + propString(props, u"api.alsa.path"_s) + propString(props, u"device.profile.name"_s)).toLower();
        QVariantMap d;
        d.insert(u"id"_s, obj.value(u"id"_s).toInt());
        d.insert(u"name"_s, name);
        d.insert(u"description"_s, propString(props, u"node.description"_s));
        d.insert(u"direction"_s, cls == u"Audio/Sink"_s ? u"output"_s : u"input"_s);
        const bool bluez = name.startsWith(u"bluez_"_s);
        d.insert(u"kind"_s, bluez ? u"bluetooth"_s : (lower.contains(u"hdmi"_s) ? u"hdmi"_s : u"alsa"_s));
        if (!inUse.isEmpty()) {
            live->insert(name, u"%1 Hz, %2"_s.arg(inUse.value(u"rate"_s).toInt()).arg(formatLabel(inUse.value(u"format"_s).toString())));
        }
        d.insert(u"rates"_s, toList(rateList));
        d.insert(u"formats"_s, QVariant(formatList));
        d.insert(u"appliedFormat"_s, propString(props, u"audio.format"_s));
        d.insert(u"appliedRate"_s, propString(props, u"audio.rate"_s).toInt());
        d.insert(u"person"_s, person.value(name));
        if (bluez) {
            d.insert(u"codec"_s, propString(props, u"api.bluez5.codec"_s));
            d.insert(u"profile"_s, propString(props, u"api.bluez5.profile"_s));
        }
        // The engine's reading of this card (its own parse of the kernel's files), kept beside what PipeWire says.
        int cardNo = -1;
        for (const auto &k : {u"api.alsa.pcm.card"_s, u"alsa.card"_s, u"api.alsa.card"_s}) {
            const QString v = propString(props, k);
            if (!v.isEmpty()) {
                cardNo = v.toInt();
                break;
            }
        }
        if (cardNo >= 0) {
            d.insert(u"card"_s, cardNo);
            for (const auto &c : cards) {
                const auto cm = c.toMap();
                if (cm.value(u"card"_s).toInt() == cardNo) {
                    d.insert(u"engineOutputs"_s, cm.value(u"outputs"_s));
                }
            }
            if (d.value(u"kind"_s) == u"hdmi"_s) {
                QVariantList elds;
                const QDir dir(procAsound() + u"/card%1"_s.arg(cardNo));
                for (const auto &fn : dir.entryList({u"eld#*"_s}, QDir::Files, QDir::Name)) {
                    QFile ef(dir.filePath(fn));
                    if (ef.open(QFile::ReadOnly)) {
                        const auto eld = parseEld(QString::fromUtf8(ef.readAll()));
                        if (!eld.isEmpty()) {
                            elds << eld;
                        }
                    }
                }
                d.insert(u"eld"_s, elds);
            }
        }
        out << d;
    }
    return out;
}

void SoundFormats::refresh()
{
    m_user = SoundFormatConf::read(userBase());
    m_system = SoundFormatConf::read(u"/etc"_s);
    m_engine = readEngine();
    m_graph = readGraph();
    QVariantMap live;
    const QVariantList devices = readDevices(m_engine, &live);
    // Only what really changed is announced, so a page that is open is not rebuilt under the person's hand.
    if (devices != m_devices) {
        m_devices = devices;
        Q_EMIT devicesChanged();
    }
    if (live != m_live) {
        m_live = live;
        Q_EMIT liveChanged();
    }
    Q_EMIT changed();
}

QString SoundFormats::inUse(const QString &nodeName) const
{
    return m_live.value(nodeName).toString();
}

bool SoundFormats::writeScope(bool system, const QVariantMap &state, QString *error)
{
    if (!system) {
        QString code;
        const bool ok = SoundFormatConf::write(userBase(), state, &code);
        if (!ok && error) {
            *error = describeWriteError(code);
        }
        return ok;
    }
    KAuth::Action action(u"org.kde.plasma.pa.soundformats.write"_s);
    action.setHelperId(u"org.kde.plasma.pa.soundformats"_s);
    action.setArguments({{u"state"_s, QString::fromUtf8(QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact))}});
    auto *job = action.execute();
    if (!job->exec()) {
        if (error) {
            const int code = job->error();
            if (code == KAuth::ActionReply::AuthorizationDeniedError || code == KAuth::ActionReply::UserCancelledError || code == KAuth::ActionReply::BackendError) {
                *error = i18nd("kcm_pulseaudio", "the system did not allow the change for every account");
            } else {
                *error = describeWriteError(job->errorText());
            }
        }
        return false;
    }
    return true;
}

bool SoundFormats::activate(const QString &what, bool removal)
{
    // A graph change that only sets values goes into the running graph at once; everything else, and every removal (the
    // automatic value then comes from the files again), is read when PipeWire and WirePlumber start.
    if (what == u"graph"_s && !removal) {
        return true; // the values are set by check() through pw-metadata, which also reads them back
    }
    const Run r = run(u"systemctl"_s, {u"--user"_s, u"restart"_s, u"wireplumber.service"_s, u"pipewire.service"_s}, 20000);
    if (r.failedToStart || r.code != 0) {
        return false;
    }
    // The nodes come back a moment after the services do.
    for (int i = 0; i < 50; ++i) {
        wait(200);
        const auto doc = pwDump();
        if (doc.isArray()) {
            for (const auto &item : doc.array()) {
                const auto props = item.toObject().value(u"info"_s).toObject().value(u"props"_s).toObject();
                if (propString(props, u"media.class"_s) == u"Audio/Sink"_s && SoundFormatConf::validNodeName(propString(props, u"node.name"_s))) {
                    wait(500);
                    if (removal || what == u"graph"_s) {
                        run(u"sparky-stereo-audio"_s, {u"apply"_s}, 8000); // the engine gives the running graph its own answer again
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

QList<QPair<int, QString>> SoundFormats::pinnedStreams() const
{
    QList<QPair<int, QString>> out;
    const auto doc = pwDump();
    for (const auto &item : doc.array()) {
        const auto info = item.toObject().value(u"info"_s).toObject();
        const auto props = info.value(u"props"_s).toObject();
        if (info.value(u"state"_s).toString() != u"running"_s || !propString(props, u"media.class"_s).startsWith(u"Stream/"_s)) {
            continue;
        }
        const int rate = propString(props, u"node.force-rate"_s).toInt();
        if (rate > 0) {
            const QString n = propString(props, u"application.name"_s);
            out << qMakePair(rate, n.isEmpty() ? propString(props, u"node.name"_s) : n);
        }
    }
    return out;
}

QString SoundFormats::probe(const QString &node, const QVariantMap &choice)
{
    // A short stretch of silence sent to the device in the chosen format shows what the device really opens.
    const QString format = choice.value(u"format"_s).toString();
    const int rate = choice.value(u"rate"_s).toInt();
    QString pwFormat = u"s32"_s;
    if (format.startsWith(u"S16"_s)) {
        pwFormat = u"s16"_s;
    } else if (format.startsWith(u"S24"_s) && !format.startsWith(u"S24_32"_s)) {
        pwFormat = u"s24"_s;
    } else if (format.startsWith(u"F32"_s)) {
        pwFormat = u"f32"_s;
    }
    const int useRate = rate > 0 ? rate : 48000;
    const int bytes = useRate * 2 * 4 / 2; // half a second, stereo, the widest sample
    QProcess cat;
    const QString exe = QStandardPaths::findExecutable(u"pw-cat"_s);
    if (exe.isEmpty()) {
        return i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the choice could not be tried.", u"pw-cat"_s);
    }
    cat.start(exe, {u"--playback"_s, u"--raw"_s, u"--target"_s, node, u"--rate"_s, QString::number(useRate), u"--channels"_s, u"2"_s, u"--format"_s, pwFormat, u"-"_s});
    if (!cat.waitForStarted(3000)) {
        return i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the choice could not be tried.", u"pw-cat"_s);
    }
    cat.write(QByteArray(bytes, '\0'));
    cat.closeWriteChannel();
    QString problem;
    bool seen = false;
    for (int i = 0; i < 25 && cat.state() != QProcess::NotRunning; ++i) {
        wait(100);
        const auto doc = pwDump();
        for (const auto &item : doc.array()) {
            const auto o = item.toObject();
            const auto props = o.value(u"info"_s).toObject().value(u"props"_s).toObject();
            if (propString(props, u"node.name"_s) != node) {
                continue;
            }
            const QString state = o.value(u"info"_s).toObject().value(u"state"_s).toString();
            if (state == u"error"_s) {
                problem = i18nd("kcm_pulseaudio", "The device went into an error state.");
            }
            const auto fmt = o.value(u"info"_s).toObject().value(u"params"_s).toObject().value(u"Format"_s).toArray();
            if (state == u"running"_s && !fmt.isEmpty()) {
                seen = true;
                const auto f = fmt.at(0).toObject();
                const int gotRate = f.value(u"rate"_s).toInt();
                const QString gotFormat = normaliseFormat(f.value(u"format"_s).toString());
                if (rate > 0 && gotRate != rate) {
                    problem = i18nd("kcm_pulseaudio", "The device ran at %1 Hz, not the %2 Hz chosen.", gotRate, rate);
                } else if (!format.isEmpty() && gotFormat != format) {
                    problem = i18nd("kcm_pulseaudio", "The device ran in %1, not in the format chosen.", formatLabel(gotFormat));
                }
            }
        }
        if (seen || !problem.isEmpty()) {
            break;
        }
    }
    cat.kill();
    cat.waitForFinished(1000);
    if (problem.isEmpty() && !seen) {
        problem = i18nd("kcm_pulseaudio", "The device did not start playing in the chosen format.");
    }
    return problem;
}

QString SoundFormats::check(const QString &what, const QVariantMap &state)
{
    if (what == u"graph"_s) {
        const QVariantMap want = state.value(u"graph"_s).toMap();
        // Set the running graph now; the files carry the same values for the next start.
        const auto set = [&](const QString &key, const QString &value) {
            run(u"pw-metadata"_s, {u"-n"_s, u"settings"_s, u"0"_s, key, value}, 4000);
        };
        if (want.contains(u"allowedRates"_s)) {
            QStringList rs;
            for (const auto &r : want.value(u"allowedRates"_s).toList()) {
                rs << r.toString();
            }
            set(u"clock.allowed-rates"_s, u"[ %1 ]"_s.arg(rs.join(u' ')));
        }
        for (const auto &[k, mk] : {std::pair{u"rate"_s, u"clock.rate"_s}, std::pair{u"quantum"_s, u"clock.quantum"_s},
                                    std::pair{u"minQuantum"_s, u"clock.min-quantum"_s}, std::pair{u"maxQuantum"_s, u"clock.max-quantum"_s}}) {
            if (want.contains(k)) {
                set(mk, want.value(k).toString());
            }
        }
        wait(800);
        const QVariantMap got = readGraph();
        QStringList wrong;
        for (const auto &k : {u"rate"_s, u"quantum"_s, u"minQuantum"_s, u"maxQuantum"_s}) {
            if (want.contains(k) && got.value(k).toInt() != want.value(k).toInt()) {
                wrong << i18nd("kcm_pulseaudio", "The sound system kept the %1 at %2, not %3.", graphLabel(k), got.value(k).toInt(), want.value(k).toInt());
            }
        }
        if (want.contains(u"allowedRates"_s)) {
            QList<int> wl;
            for (const auto &r : want.value(u"allowedRates"_s).toList()) {
                wl << r.toInt();
            }
            QList<int> gl;
            for (const auto &r : got.value(u"allowedRates"_s).toList()) {
                gl << r.toInt();
            }
            if (wl != gl) {
                wrong << i18nd("kcm_pulseaudio", "The allowed rates stay as they were.");
            }
        }
        if (want.contains(u"allowedRates"_s)) {
            // A program that pins the graph to one rate keeps it there whatever the list says.
            for (const auto &[rate, who] : pinnedStreams()) {
                bool listed = false;
                for (const auto &r : want.value(u"allowedRates"_s).toList()) {
                    listed = listed || r.toInt() == rate;
                }
                if (!listed) {
                    wrong << i18nd("kcm_pulseaudio", "%1 holds the sound system at %2 Hz, which is not among the rates chosen.", who, rate);
                }
            }
        }
        return wrong.join(u" "_s);
    }
    if (what == u"quality"_s) {
        const int want = state.value(u"quality"_s).toInt();
        const auto doc = pwDump();
        for (const auto &item : doc.array()) {
            const auto props = item.toObject().value(u"info"_s).toObject().value(u"props"_s).toObject();
            const QString name = propString(props, u"node.name"_s);
            if (name.startsWith(u"alsa_output."_s) || name.startsWith(u"bluez_output."_s)) {
                const QString got = propString(props, u"resample.quality"_s);
                if (got.toInt() != want || got.isEmpty()) {
                    return i18nd("kcm_pulseaudio", "%1 still has resampling quality %2.", propString(props, u"node.description"_s), got.isEmpty() ? QStringLiteral("-") : got);
                }
            }
        }
        return {};
    }
    if (what.startsWith(u"device:"_s)) {
        const QString node = what.mid(7);
        const QVariantMap choice = state.value(u"devices"_s).toMap().value(node).toMap();
        const auto doc = pwDump();
        bool found = false;
        for (const auto &item : doc.array()) {
            const auto props = item.toObject().value(u"info"_s).toObject().value(u"props"_s).toObject();
            if (propString(props, u"node.name"_s) != node) {
                continue;
            }
            found = true;
            if (choice.isEmpty()) {
                return {};
            }
            if (choice.contains(u"format"_s) && propString(props, u"audio.format"_s) != choice.value(u"format"_s).toString()) {
                return i18nd("kcm_pulseaudio", "The device did not take the chosen format or rate.");
            }
            if (choice.contains(u"rate"_s) && propString(props, u"audio.rate"_s).toInt() != choice.value(u"rate"_s).toInt()) {
                return i18nd("kcm_pulseaudio", "The device did not take the chosen format or rate.");
            }
        }
        if (!found) {
            return i18nd("kcm_pulseaudio", "This device is no longer connected.");
        }
        if (!choice.isEmpty() && node.contains(u"_output."_s) && (choice.contains(u"format"_s) || choice.contains(u"rate"_s))) {
            return probe(node, choice);
        }
        return {};
    }
    return {};
}

void SoundFormats::apply(bool system, const QVariantMap &newState, const QString &what)
{
    if (m_busy) {
        return;
    }
    setBusy(true);
    setMessage({}, {});
    const auto finish = [this, &what](bool ok, const QString &type, const QString &text) {
        setMessage(type, text);
        setBusy(false);
        refresh();
        Q_EMIT applied(ok, what);
    };

    const QString bad = SoundFormatConf::validate(newState);
    if (!bad.isEmpty()) {
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "Nothing was changed: the value %1 is not one the sound system accepts.", bad));
        return;
    }
    // PipeWire's own reader looks at every file before it is put in place.
    const QString dumper = QStandardPaths::findExecutable(u"spa-json-dump"_s);
    if (!dumper.isEmpty()) {
        QTemporaryDir tmp;
        const auto files = SoundFormatConf::render(newState);
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (it.value().isEmpty()) {
                continue;
            }
            QFile f(tmp.filePath(u"check.conf"_s));
            f.open(QFile::WriteOnly | QFile::Truncate);
            f.write(it.value());
            f.close();
            const Run r = run(u"spa-json-dump"_s, {f.fileName()}, 4000);
            if (r.code != 0) {
                finish(false, u"error"_s,
                       i18nd("kcm_pulseaudio", "Nothing was changed: PipeWire cannot read the settings file (%1).", QString::fromUtf8(r.err).trimmed()));
                return;
            }
        }
    }

    const QVariantMap previous = system ? m_system : m_user;
    QString error;
    if (!writeScope(system, newState, &error)) {
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "Nothing was changed: the settings could not be written (%1).", error));
        return;
    }
    const bool removal = [&] {
        // A key that was set before and is not now: its automatic value comes back from the files, so the services restart.
        const auto gone = [](const QVariantMap &a, const QVariantMap &b) {
            for (auto it = a.begin(); it != a.end(); ++it) {
                if (!b.contains(it.key())) {
                    return true;
                }
            }
            return false;
        };
        return gone(previous.value(u"graph"_s).toMap(), newState.value(u"graph"_s).toMap()) || (previous.contains(u"quality"_s) && !newState.contains(u"quality"_s))
            || gone(previous.value(u"devices"_s).toMap(), newState.value(u"devices"_s).toMap());
    }();
    bool ok = activate(what, removal);
    QString problem = ok ? check(what, newState) : i18nd("kcm_pulseaudio", "The sound system did not come back after the restart.");
    if (!problem.isEmpty()) {
        // The earlier settings are written back, and the sound system is given them again.
        QString restoreError;
        const bool restored = writeScope(system, previous, &restoreError);
        activate(what, true);
        const QString note = restored ? i18nd("kcm_pulseaudio", "The earlier settings stay.")
                                      : i18nd("kcm_pulseaudio", "The earlier settings could not be put back (%1); check the files named 50-sound-page.conf.", restoreError);
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "The change was not kept. %1 %2", problem, note));
        return;
    }
    finish(true, u"positive"_s, i18nd("kcm_pulseaudio", "The change is in use."));
}

#include "moc_soundformats.cpp"
