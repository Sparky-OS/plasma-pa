/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "soundformatconf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace
{
constexpr auto NAME = "50-sound-page.conf"_L1;
const QString PIPEWIRE_FILE = u"pipewire/pipewire.conf.d/"_s + NAME;
const QString CLIENT_FILE = u"pipewire/client.conf.d/"_s + NAME;
const QString PULSE_FILE = u"pipewire/pipewire-pulse.conf.d/"_s + NAME;
const QString WIREPLUMBER_FILE = u"wireplumber/wireplumber.conf.d/"_s + NAME;
const QStringList ALL_FILES = {PIPEWIRE_FILE, CLIENT_FILE, PULSE_FILE, WIREPLUMBER_FILE};

constexpr int MIN_RATE = 8000;
constexpr int MAX_RATE = 768000;
constexpr int MAX_RATES = 32; // PipeWire's list of allowed rates holds 32
constexpr int DEFAULT_MIN_QUANTUM = 32;
constexpr int DEFAULT_MAX_QUANTUM = 2048;

bool isPowerOfTwo(int v)
{
    return v > 0 && (v & (v - 1)) == 0;
}

bool validQuantum(int v)
{
    return isPowerOfTwo(v) && v >= 16 && v <= 8192;
}

QByteArray withMark(const QJsonObject &object)
{
    return (SoundFormatConf::mark() + u"\n"_s).toUtf8() + QJsonDocument(object).toJson(QJsonDocument::Indented);
}

// PipeWire's own file form (SPA JSON with "key = value" lines), which is also the form sparky-stereo-audio looks for when
// it asks whether the person has set a key, so its runtime `apply` leaves a key of ours alone.
QByteArray spaSection(const QString &section, const QList<QPair<QString, QString>> &values)
{
    QString text = SoundFormatConf::mark() + u"\n"_s + section + u" = {\n"_s;
    for (const auto &[k, v] : values) {
        text += u"    "_s + k + u" = "_s + v + u"\n"_s;
    }
    return (text + u"}\n"_s).toUtf8();
}

QString ratesList(const QVariantList &list)
{
    QStringList out;
    for (const auto &v : list) {
        out << QString::number(v.toInt());
    }
    return u"[ "_s + out.join(u' ') + u" ]"_s;
}

// The value of `key = value` in a section of such a file, or an empty string.
QString spaValue(const QString &body, const QString &key)
{
    const auto m = QRegularExpression(u"^\\s*"_s + QRegularExpression::escape(key) + u"\\s*=\\s*(.*?)\\s*$"_s, QRegularExpression::MultilineOption).match(body);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString stripMark(const QByteArray &raw, bool *ours);

QString readBody(const QString &path)
{
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        return {};
    }
    const QByteArray raw = f.readAll();
    bool ours = false;
    const QString body = stripMark(raw, &ours);
    return ours ? body : QString();
}

QJsonObject deviceProps(const QVariantMap &choice)
{
    QJsonObject props;
    const QString format = choice.value(u"format"_s).toString();
    const int rate = choice.value(u"rate"_s).toInt();
    if (!format.isEmpty()) {
        props.insert(u"audio.format"_s, format);
    }
    if (rate > 0) {
        props.insert(u"audio.rate"_s, rate);
        props.insert(u"audio.allowed-rates"_s, QJsonArray{rate});
    }
    if (choice.value(u"bitPerfect"_s).toBool()) {
        // Nothing may be added to the samples: no dither. The device keeps the stream's own format and rate.
        props.insert(u"dither.method"_s, u"none"_s);
    }
    return props;
}

QJsonObject rule(const QString &nodeMatch, const QJsonObject &props)
{
    return QJsonObject{{u"matches"_s, QJsonArray{QJsonObject{{u"node.name"_s, nodeMatch}}}},
                       {u"actions"_s, QJsonObject{{u"update-props"_s, props}}}};
}

QString stripMark(const QByteArray &raw, bool *ours)
{
    const QString text = QString::fromUtf8(raw);
    const auto lines = text.split(u'\n');
    *ours = !lines.isEmpty() && lines.first() == SoundFormatConf::mark();
    QStringList body;
    for (const auto &l : lines) {
        if (!l.startsWith(u'#')) {
            body << l;
        }
    }
    return body.join(u'\n');
}

QJsonObject readOurs(const QString &path)
{
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        return {};
    }
    bool ours = false;
    const QString body = stripMark(f.readAll(), &ours);
    if (!ours) {
        return {};
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(body.toUtf8(), &error);
    return error.error == QJsonParseError::NoError ? doc.object() : QJsonObject();
}

bool isOurs(const QString &path)
{
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    bool ours = false;
    stripMark(f.read(4096), &ours);
    return ours;
}
} // namespace

QString SoundFormatConf::fileName()
{
    return NAME;
}

QString SoundFormatConf::mark()
{
    return u"# Written by the Sound page of System Settings (plasma-pa). Back to automatic removes this file. "
           "The sparky-stereo-audio defaults in 00-sparky-stereo-audio.conf are never written here."_s;
}

QStringList SoundFormatConf::knownFormats()
{
    return {u"S16LE"_s, u"S24LE"_s, u"S24_32LE"_s, u"S32LE"_s, u"F32LE"_s};
}

bool SoundFormatConf::validNodeName(const QString &name)
{
    static const QRegularExpression re(u"^(alsa|bluez)_(output|input)\\.[A-Za-z0-9_.:+-]{1,200}$"_s);
    return re.match(name).hasMatch();
}

QString SoundFormatConf::validate(const QVariantMap &state)
{
    static const QSet<QString> topKeys = {u"graph"_s, u"quality"_s, u"devices"_s};
    for (auto it = state.begin(); it != state.end(); ++it) {
        if (!topKeys.contains(it.key())) {
            return u"state."_s + it.key();
        }
    }
    const QVariantMap graph = state.value(u"graph"_s).toMap();
    static const QSet<QString> graphKeys = {u"allowedRates"_s, u"rate"_s, u"quantum"_s, u"minQuantum"_s, u"maxQuantum"_s};
    for (auto it = graph.begin(); it != graph.end(); ++it) {
        if (!graphKeys.contains(it.key())) {
            return u"graph."_s + it.key();
        }
    }
    QList<int> allowed;
    if (graph.contains(u"allowedRates"_s)) {
        const auto list = graph.value(u"allowedRates"_s).toList();
        if (list.isEmpty() || list.size() > MAX_RATES) {
            return u"graph.allowedRates"_s;
        }
        for (const auto &v : list) {
            bool ok = false;
            const int r = v.toInt(&ok);
            if (!ok || r < MIN_RATE || r > MAX_RATE || allowed.contains(r)) {
                return u"graph.allowedRates"_s;
            }
            allowed << r;
        }
    }
    if (graph.contains(u"rate"_s)) {
        bool ok = false;
        const int r = graph.value(u"rate"_s).toInt(&ok);
        if (!ok || r < MIN_RATE || r > MAX_RATE || (!allowed.isEmpty() && !allowed.contains(r))) {
            return u"graph.rate"_s;
        }
    }
    for (const auto &k : {u"quantum"_s, u"minQuantum"_s, u"maxQuantum"_s}) {
        if (graph.contains(k)) {
            bool ok = false;
            const int v = graph.value(k).toInt(&ok);
            if (!ok || !validQuantum(v)) {
                return u"graph."_s + k;
            }
        }
    }
    const int mn = graph.value(u"minQuantum"_s, DEFAULT_MIN_QUANTUM).toInt();
    const int mx = graph.value(u"maxQuantum"_s, DEFAULT_MAX_QUANTUM).toInt();
    if (mn > mx) {
        return u"graph.minQuantum"_s;
    }
    if (graph.contains(u"quantum"_s)) {
        const int q = graph.value(u"quantum"_s).toInt();
        if (q < mn || q > mx) {
            return u"graph.quantum"_s;
        }
    }
    if (state.contains(u"quality"_s)) {
        bool ok = false;
        const int q = state.value(u"quality"_s).toInt(&ok);
        if (!ok || q < 0 || q > 14) {
            return u"quality"_s;
        }
    }
    const QVariantMap devices = state.value(u"devices"_s).toMap();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (!validNodeName(it.key())) {
            return u"device.name"_s;
        }
        const QVariantMap choice = it.value().toMap();
        for (auto c = choice.begin(); c != choice.end(); ++c) {
            if (c.key() != u"format"_s && c.key() != u"rate"_s && c.key() != u"bitPerfect"_s) {
                return u"device."_s + c.key();
            }
        }
        if (choice.contains(u"format"_s) && !knownFormats().contains(choice.value(u"format"_s).toString())) {
            return u"device.format"_s;
        }
        if (choice.contains(u"rate"_s)) {
            bool ok = false;
            const int r = choice.value(u"rate"_s).toInt(&ok);
            if (!ok || r < MIN_RATE || r > MAX_RATE) {
                return u"device.rate"_s;
            }
        }
        if (choice.contains(u"bitPerfect"_s) && choice.value(u"bitPerfect"_s).typeId() != QMetaType::Bool) {
            return u"device.bitPerfect"_s;
        }
        if (choice.isEmpty()) {
            return u"device.empty"_s;
        }
    }
    return {};
}

QMap<QString, QByteArray> SoundFormatConf::render(const QVariantMap &state)
{
    QMap<QString, QByteArray> files;
    for (const auto &f : ALL_FILES) {
        files.insert(f, QByteArray());
    }

    const QVariantMap graph = state.value(u"graph"_s).toMap();
    if (!graph.isEmpty()) {
        QList<QPair<QString, QString>> values;
        if (graph.contains(u"rate"_s)) {
            values << qMakePair(u"default.clock.rate"_s, QString::number(graph.value(u"rate"_s).toInt()));
        }
        if (graph.contains(u"allowedRates"_s)) {
            values << qMakePair(u"default.clock.allowed-rates"_s, ratesList(graph.value(u"allowedRates"_s).toList()));
        }
        for (const auto &[json, key] : {std::pair{u"default.clock.quantum"_s, u"quantum"_s},
                                        std::pair{u"default.clock.min-quantum"_s, u"minQuantum"_s},
                                        std::pair{u"default.clock.max-quantum"_s, u"maxQuantum"_s}}) {
            if (graph.contains(key)) {
                values << qMakePair(json, QString::number(graph.value(key).toInt()));
            }
        }
        files[PIPEWIRE_FILE] = spaSection(u"context.properties"_s, values);
    }

    const bool hasQuality = state.contains(u"quality"_s);
    const int quality = state.value(u"quality"_s).toInt();
    if (hasQuality) {
        const QByteArray stream = spaSection(u"stream.properties"_s, {qMakePair(u"resample.quality"_s, QString::number(quality))});
        files[CLIENT_FILE] = stream;
        files[PULSE_FILE] = stream;
    }

    const QVariantMap devices = state.value(u"devices"_s).toMap();
    if (hasQuality || !devices.isEmpty()) {
        QJsonArray alsa;
        QJsonArray bluez;
        if (hasQuality) {
            const QJsonObject q{{u"resample.quality"_s, quality}};
            alsa.append(rule(u"~alsa_output.*"_s, q));
            bluez.append(rule(u"~bluez_output.*"_s, q));
        }
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            const QJsonObject props = deviceProps(it.value().toMap());
            if (props.isEmpty()) {
                continue;
            }
            (it.key().startsWith(u"bluez_"_s) ? bluez : alsa).append(rule(it.key(), props));
        }
        QJsonObject root;
        if (!alsa.isEmpty()) {
            root.insert(u"monitor.alsa.rules"_s, alsa);
        }
        if (!bluez.isEmpty()) {
            root.insert(u"monitor.bluez.rules"_s, bluez);
        }
        if (!root.isEmpty()) {
            files[WIREPLUMBER_FILE] = withMark(root);
        }
    }
    return files;
}

QVariantMap SoundFormatConf::read(const QString &baseDir)
{
    QVariantMap state;

    const QString pw = readBody(baseDir + u'/' + PIPEWIRE_FILE);
    QVariantMap graph;
    if (!spaValue(pw, u"default.clock.rate"_s).isEmpty()) {
        graph.insert(u"rate"_s, spaValue(pw, u"default.clock.rate"_s).toInt());
    }
    const QString rates = spaValue(pw, u"default.clock.allowed-rates"_s);
    if (!rates.isEmpty()) {
        QVariantList list;
        static const QRegularExpression digits(u"\\d+"_s);
        auto it = digits.globalMatch(rates);
        while (it.hasNext()) {
            list << it.next().captured().toInt();
        }
        graph.insert(u"allowedRates"_s, list);
    }
    for (const auto &[json, key] : {std::pair{u"default.clock.quantum"_s, u"quantum"_s},
                                    std::pair{u"default.clock.min-quantum"_s, u"minQuantum"_s},
                                    std::pair{u"default.clock.max-quantum"_s, u"maxQuantum"_s}}) {
        if (!spaValue(pw, json).isEmpty()) {
            graph.insert(key, spaValue(pw, json).toInt());
        }
    }
    if (!graph.isEmpty()) {
        state.insert(u"graph"_s, graph);
    }

    const QString stream = readBody(baseDir + u'/' + CLIENT_FILE);
    if (!spaValue(stream, u"resample.quality"_s).isEmpty()) {
        state.insert(u"quality"_s, spaValue(stream, u"resample.quality"_s).toInt());
    }

    const QJsonObject wp = readOurs(baseDir + u'/' + WIREPLUMBER_FILE);
    QVariantMap devices;
    for (const auto &key : {u"monitor.alsa.rules"_s, u"monitor.bluez.rules"_s}) {
        for (const auto &r : wp.value(key).toArray()) {
            const QJsonObject ro = r.toObject();
            const QString name = ro.value(u"matches"_s).toArray().at(0).toObject().value(u"node.name"_s).toString();
            if (name.startsWith(u'~')) {
                continue;
            }
            const QJsonObject props = ro.value(u"actions"_s).toObject().value(u"update-props"_s).toObject();
            QVariantMap choice;
            if (props.contains(u"audio.format"_s)) {
                choice.insert(u"format"_s, props.value(u"audio.format"_s).toString());
            }
            if (props.contains(u"audio.rate"_s)) {
                choice.insert(u"rate"_s, props.value(u"audio.rate"_s).toInt());
            }
            if (props.value(u"dither.method"_s).toString() == u"none"_s) {
                choice.insert(u"bitPerfect"_s, true);
            }
            if (!choice.isEmpty()) {
                devices.insert(name, choice);
            }
        }
    }
    if (!devices.isEmpty()) {
        state.insert(u"devices"_s, devices);
    }
    return state;
}

bool SoundFormatConf::write(const QString &baseDir, const QVariantMap &state, QString *error)
{
    const QString bad = validate(state);
    if (!bad.isEmpty()) {
        if (error) {
            *error = u"invalid: "_s + bad;
        }
        return false;
    }
    const auto files = render(state);
    // Check first that nothing in the way is somebody else's file, so a refusal leaves everything as it was.
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = baseDir + u'/' + it.key();
        if (QFileInfo::exists(path) && !isOurs(path)) {
            if (it.value().isEmpty()) {
                continue; // not ours and not wanted: left alone
            }
            if (error) {
                *error = u"foreign: "_s + path;
            }
            return false;
        }
    }
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString path = baseDir + u'/' + it.key();
        if (it.value().isEmpty()) {
            if (QFileInfo::exists(path) && isOurs(path) && !QFile::remove(path)) {
                if (error) {
                    *error = u"remove: "_s + path;
                }
                return false;
            }
            continue;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            if (error) {
                *error = u"mkdir: "_s + path;
            }
            return false;
        }
        QSaveFile f(path);
        if (!f.open(QFile::WriteOnly) || f.write(it.value()) != it.value().size() || !f.commit()) {
            if (error) {
                *error = u"write: "_s + path;
            }
            return false;
        }
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
    return true;
}
