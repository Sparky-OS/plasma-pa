/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "speakersetupconf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QtMath>

using namespace Qt::StringLiterals;

namespace
{
const QString FILE_PATH = u"pipewire/pipewire.conf.d/50-speaker-page.conf"_s;
constexpr auto NAME = "50-speaker-page.conf"_L1;
constexpr char STATE_PREFIX[] = "# state: ";
constexpr int MAX_DEVICES = 8;
constexpr int MAX_POSITIONS = 8; // the builtin mixer has eight inputs
constexpr double MAX_DELAY_SECONDS = 0.5;

bool isNumber(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return true;
    case QMetaType::Double:
    case QMetaType::Float:
        return qIsFinite(v.toDouble());
    default:
        return false;
    }
}

QString num(double v)
{
    return QString::number(v, 'f', 9);
}

QStringList stringList(const QVariant &v, bool *ok)
{
    *ok = false;
    if (v.typeId() == QMetaType::QStringList) {
        *ok = true;
        return v.toStringList();
    }
    if (v.typeId() != QMetaType::QVariantList) {
        return {};
    }
    QStringList out;
    for (const auto &item : v.toList()) {
        if (item.typeId() != QMetaType::QString) {
            return {};
        }
        out << item.toString();
    }
    *ok = true;
    return out;
}

bool hasEffect(const QVariantMap &dev)
{
    const QVariantMap distances = dev.value(u"distances"_s).toMap();
    double lo = 1e9;
    double hi = -1e9;
    for (auto it = distances.begin(); it != distances.end(); ++it) {
        lo = qMin(lo, it.value().toDouble());
        hi = qMax(hi, it.value().toDouble());
    }
    if (!distances.isEmpty() && SpeakerSetupConf::delaySeconds(lo, hi) > 0.0) {
        return true;
    }
    return !dev.value(u"small"_s).toList().isEmpty() || !dev.value(u"small"_s).toStringList().isEmpty();
}

struct Graph {
    QStringList nodes;
    QStringList links;
    QMap<QString, QString> outputs; // position -> output port
    QStringList inputs;
    void node(const QString &name, const QString &label, const QString &config = {}, const QString &control = {})
    {
        QString line = u"        { name = %1 type = builtin label = %2"_s.arg(name, label);
        if (!config.isEmpty()) {
            line += u" config = { %1 }"_s.arg(config);
        }
        if (!control.isEmpty()) {
            line += u" control = { %1 }"_s.arg(control);
        }
        nodes << line + u" }"_s;
    }
    void link(const QString &from, const QString &to)
    {
        links << u"        { output = \"%1\" input = \"%2\" }"_s.arg(from, to);
    }
};

QString positionList(const QStringList &positions)
{
    return u"[ "_s + positions.join(u' ') + u" ]"_s;
}

QString renderDevice(const QString &node, const QVariantMap &dev)
{
    const QStringList positions = dev.value(u"positions"_s).toStringList();
    const QVariantMap distances = dev.value(u"distances"_s).toMap();
    const QStringList small = dev.value(u"small"_s).toStringList();
    const double crossover = dev.value(u"crossover"_s, SpeakerSetupConf::defaultCrossover).toDouble();
    double farthest = 0;
    for (auto it = distances.begin(); it != distances.end(); ++it) {
        farthest = qMax(farthest, it.value().toDouble());
    }

    Graph g;
    QStringList subFeeds;
    for (const QString &p : positions) {
        g.node(u"in_"_s + p, u"copy"_s);
        g.inputs << u"\"in_%1:In\""_s.arg(p);
    }
    const auto delayAfter = [&](const QString &p, QString head) {
        const double d = distances.contains(p) ? SpeakerSetupConf::delaySeconds(distances.value(p).toDouble(), farthest) : 0.0;
        if (d > 0.0) {
            g.node(u"d_"_s + p, u"delay"_s, u"\"max-delay\" = %1"_s.arg(num(MAX_DELAY_SECONDS)), u"\"Delay (s)\" = %1"_s.arg(num(d)));
            g.link(head, u"d_%1:In"_s.arg(p));
            head = u"d_%1:Out"_s.arg(p);
        }
        return head;
    };
    for (const QString &p : positions) {
        if (p == u"LFE"_s) {
            continue;
        }
        QString head = u"in_%1:Out"_s.arg(p);
        if (small.contains(p)) {
            const QString control = u"Freq = %1 Q = 0.707106781"_s.arg(num(crossover));
            for (const QString &k : {u"1"_s, u"2"_s}) {
                g.node(u"hp%1_%2"_s.arg(k, p), u"bq_highpass"_s, {}, control);
                g.node(u"lp%1_%2"_s.arg(k, p), u"bq_lowpass"_s, {}, control);
            }
            g.link(head, u"hp1_%1:In"_s.arg(p));
            g.link(u"hp1_%1:Out"_s.arg(p), u"hp2_%1:In"_s.arg(p));
            g.link(head, u"lp1_%1:In"_s.arg(p));
            g.link(u"lp1_%1:Out"_s.arg(p), u"lp2_%1:In"_s.arg(p));
            subFeeds << u"lp2_%1:Out"_s.arg(p);
            head = u"hp2_%1:Out"_s.arg(p);
        }
        g.outputs.insert(p, delayAfter(p, head));
    }
    if (positions.contains(u"LFE"_s)) {
        QString head = u"in_LFE:Out"_s;
        if (!subFeeds.isEmpty()) {
            QString gains;
            for (int i = 1; i <= subFeeds.size() + 1; ++i) {
                gains += u"\"Gain %1\" = 1.000000000 "_s.arg(i);
            }
            g.node(u"mix_LFE"_s, u"mixer"_s, {}, gains.trimmed());
            g.link(head, u"mix_LFE:In 1"_s);
            for (int i = 0; i < subFeeds.size(); ++i) {
                g.link(subFeeds.at(i), u"mix_LFE:In %1"_s.arg(i + 2));
            }
            head = u"mix_LFE:Out"_s;
        }
        g.outputs.insert(u"LFE"_s, delayAfter(u"LFE"_s, head));
    }
    QStringList outs;
    for (const QString &p : positions) {
        outs << u"\"%1\""_s.arg(g.outputs.value(p));
    }

    const QString label = dev.contains(u"label"_s) ? dev.value(u"label"_s).toString() : u"Speaker Setup"_s;
    const QString pos = positionList(positions);
    QString text;
    text += u"    { name = libpipewire-module-filter-chain\n"_s;
    text += u"      flags = [ nofail ]\n"_s;
    text += u"      args = {\n"_s;
    text += u"        node.description = \"%1\"\n"_s.arg(label);
    text += u"        media.name = \"%1\"\n"_s.arg(label);
    text += u"        filter.graph = {\n"_s;
    text += u"          nodes = [\n%1\n          ]\n"_s.arg(g.nodes.join(u'\n'));
    text += u"          links = [\n%1\n          ]\n"_s.arg(g.links.join(u'\n'));
    text += u"          inputs = [ %1 ]\n"_s.arg(g.inputs.join(u' '));
    text += u"          outputs = [ %1 ]\n"_s.arg(outs.join(u' '));
    text += u"        }\n"_s;
    text += u"        capture.props = { node.name = \"%1\" media.class = Audio/Sink audio.channels = %2 audio.position = %3 }\n"_s.arg(
        SpeakerSetupConf::sinkName(node), QString::number(positions.size()), pos);
    text += u"        playback.props = { node.name = \"speaker_setup_out.%1\" target.object = \"%1\" node.passive = true stream.dont-remix = true node.dont-fallback = true node.linger = true "
            "audio.channels = %2 audio.position = %3 }\n"_s.arg(node, QString::number(positions.size()), pos);
    text += u"      }\n"_s;
    text += u"    }\n"_s;
    return text;
}

bool isOurs(const QString &path)
{
    QFile f(path);
    if (!f.open(QFile::ReadOnly)) {
        return false;
    }
    return f.readLine(4096).trimmed() == SpeakerSetupConf::mark().toUtf8();
}
} // namespace

QString SpeakerSetupConf::fileName()
{
    return NAME;
}

QString SpeakerSetupConf::mark()
{
    return u"# Written by the Speaker Setup page of System Settings (plasma-pa). Back to defaults removes this file. "
           "The sparky-stereo-audio defaults in 00-sparky-stereo-audio.conf are never written here."_s;
}

QStringList SpeakerSetupConf::knownPositions()
{
    return {u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s, u"SL"_s, u"SR"_s, u"RC"_s};
}

bool SpeakerSetupConf::validPosition(const QString &position)
{
    return knownPositions().contains(position);
}

bool SpeakerSetupConf::validNodeName(const QString &name)
{
    // \\z, not $: $ also matches before a final newline
    static const QRegularExpression re(u"^(alsa|bluez)_output\\.[A-Za-z0-9_.:+-]{1,200}\\z"_s);
    return re.match(name).hasMatch();
}

bool SpeakerSetupConf::validLabel(const QString &label)
{
    // Letters and digits of any script, combining marks, spaces, and the marks . _ + : ( ) / - and the fullwidth colon and
    // parentheses CJK translations use; no quote, backslash, brace, bracket, hash, comma, equals sign or control character.
    static const QRegularExpression re(u"^[\\p{L}\\p{N}][\\p{L}\\p{N}\\p{M}\\p{Zs}._+:()/\\x{FF1A}\\x{FF08}\\x{FF09}-]{0,79}\\z"_s);
    return re.match(label).hasMatch();
}

QString SpeakerSetupConf::sanitiseLabel(const QString &text, int maxCharacters)
{
    static const QRegularExpression keep(u"^[\\p{L}\\p{N}\\p{M}\\p{Zs}._+:()/\\x{FF1A}\\x{FF08}\\x{FF09}-]$"_s);
    QString out;
    int count = 0;
    for (const char32_t c : text.toUcs4()) {
        if (count >= maxCharacters) {
            break;
        }
        const QString one = QString::fromUcs4(&c, 1);
        if (keep.match(one).hasMatch()) {
            out += one;
            ++count;
        }
    }
    out = out.trimmed();
    return validLabel(out) ? out : QString();
}

QString SpeakerSetupConf::sinkName(const QString &nodeName)
{
    return u"speaker_setup."_s + nodeName;
}

bool SpeakerSetupConf::isSetupSink(const QString &nodeName)
{
    return nodeName.startsWith(u"speaker_setup."_s) || nodeName.startsWith(u"speaker_setup_out."_s);
}

double SpeakerSetupConf::angle(const QString &position, const QStringList &positions)
{
    const bool sides = positions.contains(u"SL"_s) && positions.contains(u"RL"_s);
    if (position == u"FC"_s) {
        return 0;
    }
    if (position == u"FL"_s) {
        return -30;
    }
    if (position == u"FR"_s) {
        return 30;
    }
    if (position == u"SL"_s) {
        return -90;
    }
    if (position == u"SR"_s) {
        return 90;
    }
    if (position == u"RL"_s) {
        return sides ? -120 : -110;
    }
    if (position == u"RR"_s) {
        return sides ? 120 : 110;
    }
    return 180; // RC, and LFE
}

double SpeakerSetupConf::delaySeconds(double distance, double farthest)
{
    const double d = (farthest - distance) / speedOfSound;
    return d > 0.0 ? d : 0.0;
}

QString SpeakerSetupConf::validate(const QVariantMap &state)
{
    for (auto it = state.begin(); it != state.end(); ++it) {
        if (it.key() != u"devices"_s) {
            return u"state.key"_s;
        }
    }
    if (state.contains(u"devices"_s) && state.value(u"devices"_s).typeId() != QMetaType::QVariantMap) {
        return u"devices"_s;
    }
    const QVariantMap devices = state.value(u"devices"_s).toMap();
    if (devices.size() > MAX_DEVICES) {
        return u"devices"_s;
    }
    static const QSet<QString> deviceKeys = {u"positions"_s, u"label"_s, u"distances"_s, u"small"_s, u"crossover"_s};
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (!validNodeName(it.key())) {
            return u"device.name"_s;
        }
        if (it.value().typeId() != QMetaType::QVariantMap) {
            return u"device"_s;
        }
        const QVariantMap dev = it.value().toMap();
        for (auto k = dev.begin(); k != dev.end(); ++k) {
            if (!deviceKeys.contains(k.key())) {
                return u"device.key"_s;
            }
        }
        bool ok = false;
        const QStringList positions = stringList(dev.value(u"positions"_s), &ok);
        if (!ok || positions.size() < 3 || positions.size() > MAX_POSITIONS || QSet<QString>(positions.begin(), positions.end()).size() != positions.size()) {
            return u"device.positions"_s;
        }
        for (const auto &p : positions) {
            if (!validPosition(p)) {
                return u"device.positions"_s;
            }
        }
        if (dev.contains(u"label"_s) && (dev.value(u"label"_s).typeId() != QMetaType::QString || !validLabel(dev.value(u"label"_s).toString()))) {
            return u"device.label"_s;
        }
        if (dev.contains(u"distances"_s)) {
            if (dev.value(u"distances"_s).typeId() != QMetaType::QVariantMap) {
                return u"device.distances"_s;
            }
            const QVariantMap distances = dev.value(u"distances"_s).toMap();
            if (distances.size() != positions.size()) {
                return u"device.distances"_s;
            }
            for (const auto &p : positions) {
                const QVariant v = distances.value(p);
                if (!distances.contains(p) || !isNumber(v) || v.toDouble() < minDistance || v.toDouble() > maxDistance) {
                    return u"device.distances"_s;
                }
            }
        }
        QStringList small;
        if (dev.contains(u"small"_s)) {
            small = stringList(dev.value(u"small"_s), &ok);
            if (!ok || small.isEmpty() || !positions.contains(u"LFE"_s) || QSet<QString>(small.begin(), small.end()).size() != small.size()) {
                return u"device.small"_s;
            }
            for (const auto &p : small) {
                if (!positions.contains(p) || p == u"LFE"_s) {
                    return u"device.small"_s;
                }
            }
        }
        if (dev.contains(u"crossover"_s)) {
            const QVariant v = dev.value(u"crossover"_s);
            if (small.isEmpty() || !isNumber(v) || v.toDouble() != qFloor(v.toDouble()) || v.toDouble() < minCrossover || v.toDouble() > maxCrossover) {
                return u"device.crossover"_s;
            }
        }
        if (!hasEffect(dev)) {
            return u"device.empty"_s;
        }
    }
    return {};
}

QMap<QString, QByteArray> SpeakerSetupConf::render(const QVariantMap &state)
{
    QMap<QString, QByteArray> files;
    const QVariantMap devices = state.value(u"devices"_s).toMap();
    if (devices.isEmpty()) {
        files.insert(FILE_PATH, QByteArray());
        return files;
    }
    QString text = mark() + u"\n"_s;
    text += QString::fromLatin1(STATE_PREFIX) + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact)) + u"\n"_s;
    text += u"context.modules = [\n"_s;
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        text += renderDevice(it.key(), it.value().toMap());
    }
    text += u"]\n"_s;
    files.insert(FILE_PATH, text.toUtf8());
    return files;
}

QVariantMap SpeakerSetupConf::read(const QString &baseDir)
{
    QFile f(baseDir + u'/' + FILE_PATH);
    if (!f.open(QFile::ReadOnly)) {
        return {};
    }
    const QList<QByteArray> lines = f.readAll().split('\n');
    if (lines.size() < 2 || lines.at(0).trimmed() != mark().toUtf8()) {
        return {};
    }
    const QByteArray second = lines.at(1);
    if (!second.startsWith(STATE_PREFIX)) {
        return {};
    }
    const auto doc = QJsonDocument::fromJson(second.mid(int(sizeof(STATE_PREFIX)) - 1));
    if (!doc.isObject()) {
        return {};
    }
    const QVariantMap state = doc.object().toVariantMap();
    return validate(state).isEmpty() ? state : QVariantMap();
}

bool SpeakerSetupConf::write(const QString &baseDir, const QVariantMap &state, QString *error)
{
    const QString bad = validate(state);
    if (!bad.isEmpty()) {
        if (error) {
            *error = u"invalid: "_s + bad;
        }
        return false;
    }
    const auto files = render(state);
    const QString path = baseDir + u'/' + FILE_PATH;
    const QByteArray content = files.value(FILE_PATH);
    if (QFileInfo::exists(path) && !isOurs(path)) {
        if (content.isEmpty()) {
            return true; // not ours and not wanted: left alone
        }
        if (error) {
            *error = u"foreign: "_s + path;
        }
        return false;
    }
    if (content.isEmpty()) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            if (error) {
                *error = u"remove: "_s + path;
            }
            return false;
        }
        return true;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) {
            *error = u"mkdir: "_s + path;
        }
        return false;
    }
    QSaveFile f(path);
    if (!f.open(QFile::WriteOnly) || f.write(content) != content.size() || !f.commit()) {
        if (error) {
            *error = u"write: "_s + path;
        }
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    return true;
}
