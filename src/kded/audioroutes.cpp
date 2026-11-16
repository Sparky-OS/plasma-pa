/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "audioroutes.h"

#include <QDBusConnection>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <functional>
#ifdef Q_OS_LINUX
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
#endif
#include <KConfigGroup>
#include <KLocalizedString>
#include <PulseAudioQt/Context>
#include <PulseAudioQt/Device>
#include <PulseAudioQt/Module>
#include <PulseAudioQt/Server>
#include <PulseAudioQt/Sink>
#include <PulseAudioQt/SinkInput>
#include <PulseAudioQt/Source>
#include <PulseAudioQt/SourceOutput>

using namespace Qt::Literals::StringLiterals;
namespace
{
const QString combinedName = u"plasma_pa_combined"_s;
QString json(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QString quoted(const QString &s)
{
    const auto a = QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(a.mid(1, a.size() - 2));
}
QString propertyArgument(QString value)
{
    value.replace(u'\'', u"\\'"_s);
    return u'\'' + value + u'\'';
}
PulseAudioQt::Source *source(const QString &name)
{
    for (auto *s : PulseAudioQt::Context::instance()->sources())
        if (s->name() == name)
            return s;
    return nullptr;
}
PulseAudioQt::Sink *sink(const QString &name)
{
    for (auto *s : PulseAudioQt::Context::instance()->sinks())
        if (s->name() == name)
            return s;
    return nullptr;
}
QString node(PulseAudioQt::Device *d)
{
    return d->pulseProperties().value(u"node.name"_s, d->name()).toString();
}
QString serial(PulseAudioQt::Device *d)
{
    const auto value = d->pulseProperties().value(u"object.serial"_s).toString();
    bool ok;
    const auto number = value.toULongLong(&ok);
    return ok ? QString::number(number) : QString();
}
bool monitor(PulseAudioQt::Device *d)
{
    return d->pulseProperties().value(u"device.class"_s) == u"monitor"_s;
}
QString identity(PulseAudioQt::Device *d)
{
    if (PulseAudioQt::Context::instance()->server()->isPipeWire())
        return node(d);
    return monitor(d) && d->name().endsWith(u".monitor"_s) ? d->name().chopped(8) : d->name();
}
} // namespace

struct AudioRoutes::Route : QObject {
    QString key, input, output, inputLabel, outputLabel, identity;
    bool enabled = false, mix = false, blocked = false;
    int reason = 0;
    QString detail;
    QPointer<QProcess> process;
    QList<uint> modules;
    QList<QStringList> commands;
    QTimer startup;
    bool running = false, stopping = false, native = false;
    QJsonObject state() const
    {
        return {{u"id"_s, key},
                {u"source"_s, input},
                {u"output"_s, output},
                {u"enabled"_s, enabled},
                {u"running"_s, running},
                {u"reason"_s, reason},
                {u"detail"_s, detail},
                {u"sourceLabel"_s, inputLabel},
                {u"outputLabel"_s, outputLabel}};
    }
};

AudioRoutes::AudioRoutes(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(u"plasmaparc"_s))
    , m_combined(std::make_shared<Route>())
{
    m_combined->key = u"CombinedRecording"_s;
    m_combined->mix = true;
    KConfigGroup group(m_config, u"AudioRoutes"_s);
    m_owner = group.readEntry("Owner", QString());
    if (QUuid(m_owner).isNull()) {
        m_owner = QUuid::createUuid().toString(QUuid::WithoutBraces);
        group.writeEntry("Owner", m_owner);
        group.sync();
    }
    const auto load = [&](Route &r) {
        KConfigGroup g(&group, r.key);
        r.input = g.readEntry("Source", QString());
        r.output = g.readEntry("Output", QString());
        r.enabled = g.readEntry("Enabled", false);
        r.inputLabel = g.readEntry("SourceLabel", r.input);
        r.outputLabel = g.readEntry("OutputLabel", r.output);
    };
    load(*m_combined);
    for (const auto &name : group.groupList()) {
        if (!name.startsWith(u"Listen-"_s))
            continue;
        auto r = std::make_shared<Route>();
        r->key = name;
        load(*r);
        m_listen.insert(r->key, r);
    }
    auto *c = PulseAudioQt::Context::instance();
    connect(c, &PulseAudioQt::Context::stateChanged, this, [this, c] {
        if (c->state() != PulseAudioQt::Context::State::Ready) {
            for (const auto &r : m_listen)
                stop(*r);
            stop(*m_combined);
        }
        schedule();
    });
    connect(c, &PulseAudioQt::Context::sourceAdded, this, &AudioRoutes::schedule);
    connect(c, &PulseAudioQt::Context::sourceRemoved, this, &AudioRoutes::schedule);
    connect(c, &PulseAudioQt::Context::sinkAdded, this, &AudioRoutes::schedule);
    connect(c, &PulseAudioQt::Context::sinkRemoved, this, &AudioRoutes::schedule);
    // Also detect a helper that started but never published its streams.
    m_timer.setInterval(250);
    connect(&m_timer, &QTimer::timeout, this, &AudioRoutes::reconcile);
    m_timer.start();
    QDBusConnection::sessionBus().registerObject(u"/org/kde/plasma/AudioRoutes"_s, this, QDBusConnection::ExportScriptableContents);
}

AudioRoutes::~AudioRoutes()
{
    m_timer.stop();
    for (auto &r : m_listen)
        stop(*r);
    stop(*m_combined);
}
void AudioRoutes::schedule()
{
    QTimer::singleShot(0, this, &AudioRoutes::reconcile);
}
void AudioRoutes::save(Route &r)
{
    KConfigGroup root(m_config, u"AudioRoutes"_s), g(&root, r.key);
    g.writeEntry("Source", r.input);
    g.writeEntry("Output", r.output);
    g.writeEntry("Enabled", r.enabled);
    g.writeEntry("SourceLabel", r.inputLabel);
    g.writeEntry("OutputLabel", r.outputLabel);
    g.sync();
}
QString AudioRoutes::snapshot() const
{
    QJsonObject listens;
    for (auto it = m_listen.cbegin(); it != m_listen.cend(); ++it) {
        QString key = it.value()->input;
        if (listens.contains(key))
            key = it.key();
        while (listens.contains(key))
            key += u'#';
        listens.insert(key, it.value()->state());
    }
    return json({{u"listen"_s, listens}, {u"combined"_s, m_combined->state()}});
}
void AudioRoutes::setListen(const QString &input, const QString &output, bool enabled)
{
    QString key;
    for (const auto &r : m_listen)
        if (r->input == input) {
            key = r->key;
            break;
        }
    setListenRoute(key, input, output, enabled);
}
void AudioRoutes::setListenRoute(const QString &id, const QString &input, const QString &output, bool enabled)
{
    const QString key = m_listen.contains(id) ? id : u"Listen-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto &r = m_listen[key];
    if (!r) {
        r = std::make_shared<Route>();
        r->key = key;
    }
    const bool stopped = stop(*r);
    r->input = input;
    r->output = output;
    r->enabled = enabled;
    r->blocked = !stopped;
    r->reason = stopped ? 0 : 10;
    if (auto *d = source(input))
        r->inputLabel = d->description();
    else
        r->inputLabel = input;
    if (auto *d = sink(output))
        r->outputLabel = d->description();
    else
        r->outputLabel = output;
    save(*r);
    if (stopped)
        reconcile();
    Q_EMIT changed();
}
void AudioRoutes::moveListen(const QString &id, bool playback, uint deviceIndex)
{
    const auto r = m_listen.value(id);
    if (!r)
        return;
    if (playback) {
        for (auto *d : PulseAudioQt::Context::instance()->sinks())
            if (d->index() == deviceIndex) {
                setListenRoute(id, r->input, d->name(), r->enabled);
                return;
            }
    } else {
        for (auto *d : PulseAudioQt::Context::instance()->sources())
            if (d->index() == deviceIndex) {
                setListenRoute(id, d->name(), r->output, r->enabled);
                return;
            }
    }
}
void AudioRoutes::setCombined(const QString &input, const QString &output, bool enabled)
{
    auto &r = *m_combined;
    const bool stopped = stop(r);
    r.input = input;
    r.output = output;
    r.enabled = enabled;
    r.blocked = !stopped;
    r.reason = stopped ? 0 : 10;
    if (auto *d = source(input))
        r.inputLabel = d->description();
    else
        r.inputLabel = input;
    if (auto *d = sink(output))
        r.outputLabel = d->description();
    else
        r.outputLabel = output;
    save(r);
    if (stopped)
        reconcile();
    Q_EMIT changed();
}
bool AudioRoutes::stop(Route &r)
{
    r.stopping = true;
    r.startup.stop();
    r.running = false;
    r.identity.clear();
    if (r.process) {
        auto *p = r.process.data();
        r.process = nullptr;
        if (r.native && p->state() != QProcess::NotRunning)
            p->waitForFinished(2000);
        p->disconnect(this);
        if (p->state() != QProcess::NotRunning) {
            p->terminate();
            if (!p->waitForFinished(1500)) {
                p->kill();
                p->waitForFinished(1500);
            }
        }
        delete p;
    }
    r.commands.clear();
    QList<uint> remaining;
    if (PulseAudioQt::Context::instance()->state() == PulseAudioQt::Context::State::Ready) {
        for (auto it = r.modules.crbegin(); it != r.modules.crend(); ++it) {
            QProcess p;
            p.start(u"pactl"_s, {u"unload-module"_s, QString::number(*it)});
            if (!p.waitForFinished(2000) || p.exitCode() != 0) {
                for (auto *m : PulseAudioQt::Context::instance()->modules())
                    if (m->index() == *it && m->argument().contains(u"plasma.pa.owner="_s + m_owner))
                        remaining.prepend(*it);
            }
        }
    }
    if (!remaining.isEmpty()) {
        QProcess query;
        query.start(u"pactl"_s, {u"--format=json"_s, u"list"_s, u"modules"_s});
        if (query.waitForFinished(2000) && query.exitCode() == 0) {
            const auto document = QJsonDocument::fromJson(query.readAllStandardOutput());
            if (document.isArray()) {
                QSet<uint> existing;
                for (const auto &value : document.array())
                    existing.insert(value.toObject().value(u"index"_s).toInt());
                remaining.removeIf([&](uint id) {
                    return !existing.contains(id);
                });
            }
        }
    }
    r.modules = remaining;
    r.stopping = false;
    if (!remaining.isEmpty()) {
        r.reason = 10;
        r.blocked = true;
        return false;
    }
    return true;
}
void AudioRoutes::failed(Route &r, int reason, const QString &detail)
{
    r.reason = stop(r) ? reason : 10;
    r.detail = detail;
    r.blocked = true;
    Q_EMIT changed();
}
bool AudioRoutes::wouldLoop(const Route &route) const
{
    QMap<QString, QStringList> edges;
    for (const auto &r : m_listen) {
        if (!r->enabled)
            continue;
        auto *a = source(r->input);
        auto *b = sink(r->output);
        if (a && b)
            edges[identity(a)].append(identity(b));
    }
    if (m_combined->enabled) {
        auto *a = source(m_combined->input);
        auto *b = sink(m_combined->output);
        if (a)
            edges[identity(a)].append(combinedName);
        if (b)
            edges[identity(b)].append(combinedName);
    }
    const auto reaches = [&](const QString &from, const QString &target) {
        QSet<QString> visited;
        std::function<bool(const QString &)> visit = [&](const QString &n) {
            if (n == target)
                return true;
            if (visited.contains(n))
                return false;
            visited.insert(n);
            for (const auto &child : edges.value(n))
                if (visit(child))
                    return true;
            return false;
        };
        return visit(from);
    };
    auto *a = source(route.input);
    auto *b = sink(route.output);
    return route.mix ? ((a && reaches(combinedName, identity(a))) || (b && reaches(combinedName, identity(b)))) : (a && b && reaches(identity(b), identity(a)));
}
void AudioRoutes::reconcile()
{
    const auto before = snapshot();
    auto routes = m_listen.values();
    routes.append(m_combined);
    cleanOrphans();
    for (const auto &ptr : routes) {
        auto &r = *ptr;
        int reason = 0;
        QString detail;
        auto *a = source(r.input);
        auto *b = sink(r.output);
        if (!r.enabled) {
            if (r.reason != 10)
                stop(r);
            if (r.modules.isEmpty())
                r.reason = 0;
            continue;
        }
        if (r.reason == 10 && r.blocked)
            continue;
        if (PulseAudioQt::Context::instance()->state() != PulseAudioQt::Context::State::Ready)
            reason = 1;
        else if ((r.mix && (r.input == combinedName || r.input.startsWith(combinedName + u"."_s))) || r.output.startsWith(combinedName) || wouldLoop(r))
            reason = 8;
        else if (!a) {
            reason = 2;
            detail = r.inputLabel;
        } else if (!b) {
            reason = 3;
            detail = r.outputLabel;
        }
        if (reason) {
            const bool stopped = stop(r);
            r.reason = stopped ? reason : 10;
            r.detail = detail;
            r.blocked = !stopped;
            continue;
        }
        const auto id = QString::number(a->index()) + u":"_s + QString::number(b->index());
        if (!r.identity.isEmpty() && r.identity != id) {
            if (!stop(r))
                continue;
            r.blocked = false;
        }
        if (!r.process && r.modules.isEmpty() && !r.blocked) {
            r.reason = 0;
            start(r);
            r.identity = id;
        }
    }
    if (snapshot() != before)
        Q_EMIT changed();
}

void AudioRoutes::start(Route &r)
{
    auto *a = source(r.input);
    auto *b = sink(r.output);
    const bool pw = PulseAudioQt::Context::instance()->server()->isPipeWire();
    r.native = !pw;
    auto *p = new QProcess(this);
    r.process = p;
    p->setProcessChannelMode(QProcess::MergedChannels);
    connect(p, &QProcess::readyReadStandardOutput, this, [p] {
        p->readAllStandardOutput();
    });
    connect(p, &QProcess::errorOccurred, this, [this, &r](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            QTimer::singleShot(0, this, [this, &r, guard = QPointer<QProcess>(r.process)] {
                if (guard && r.process == guard)
                    failed(r, 4, r.mix ? u"pw-cli"_s : u"pw-loopback"_s);
            });
    });
    if (!pw) {
        // PulseAudio's modules are owned by their returned IDs, never by name.
        const auto loopback = [&](const QString &src, const QString &dst, const QString &label) {
            return QStringList{u"load-module"_s,
                               u"module-loopback"_s,
                               u"source="_s + quoted(src),
                               u"sink="_s + quoted(dst),
                               u"source_dont_move=true"_s,
                               u"sink_dont_move=true"_s,
                               u"sink_input_properties="_s
                                   + propertyArgument(u"media.name="_s + quoted(label) + u" plasma.pa.owner="_s + m_owner + u" plasma.pa.route="_s + r.key),
                               u"source_output_properties="_s
                                   + propertyArgument(u"media.name="_s + quoted(label) + u" plasma.pa.owner="_s + m_owner + u" plasma.pa.route="_s + r.key)};
        };
        if (r.mix) {
            QProcess query;
            query.start(u"pactl"_s, {u"--format=json"_s, u"list"_s, u"sources"_s});
            if (!query.waitForFinished(2000) || query.exitCode() != 0) {
                failed(r, 9);
                return;
            }
            QString rate;
            for (const auto &value : QJsonDocument::fromJson(query.readAllStandardOutput()).array()) {
                const auto item = value.toObject();
                if (item.value(u"name"_s).toString() == r.input) {
                    rate = QRegularExpression(u"\\b([0-9]+)Hz\\b"_s).match(item.value(u"sample_specification"_s).toString()).captured(1);
                }
            }
            if (rate.isEmpty()) {
                failed(r, 9);
                return;
            }
            r.commands.append({u"load-module"_s,
                               u"module-null-sink"_s,
                               u"sink_name="_s + combinedName + u"_bus"_s,
                               u"sink_properties="_s
                                   + propertyArgument(u"device.description="_s + quoted(i18nd("kcm_pulseaudio", "Combined Recording")) + u" plasma.pa.owner="_s
                                                      + m_owner + u" plasma.pa.route="_s + r.key),
                               u"format=float32le"_s,
                               u"rate="_s + rate});
            r.commands.append(loopback(r.input, combinedName + u"_bus"_s, u"plasma-pa-combined-microphone"_s));
            r.commands.append(loopback(r.output + u".monitor"_s, combinedName + u"_bus"_s, u"plasma-pa-combined-output"_s));
            r.commands.append({u"load-module"_s,
                               u"module-remap-source"_s,
                               u"master="_s + combinedName + u"_bus.monitor"_s,
                               u"source_name="_s + combinedName,
                               u"source_properties="_s
                                   + propertyArgument(u"device.description="_s + quoted(i18nd("kcm_pulseaudio", "Combined Recording")) + u" plasma.pa.owner="_s
                                                      + m_owner + u" plasma.pa.route="_s + r.key)});
        } else
            r.commands.append(loopback(r.input, r.output, u"plasma-pa-"_s + r.key));
        p->disconnect(this);
        delete p;
        r.process = nullptr;
        nativeStep(r);
        return;
    }
    const auto sourceSerial = serial(a), sinkSerial = serial(b);
    if (sourceSerial.isEmpty() || sinkSerial.isEmpty()) {
        failed(r, 6);
        return;
    }
    const QString name = u"plasma-pa-"_s + r.key;
#ifdef Q_OS_LINUX
    const auto parentPid = getpid();
    p->setChildProcessModifier([parentPid] {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() != parentPid)
            _exit(1);
    });
#endif
    if (r.mix) {
        QJsonArray rules;
        for (const auto &[device, isMonitor] : QList<QPair<PulseAudioQt::Device *, bool>>{{a, monitor(a)}, {b, true}}) {
            QJsonObject props{{u"stream.capture.sink"_s, isMonitor},
                              {u"node.name"_s, name + (device == a ? u".microphone"_s : u".output"_s)},
                              {u"media.name"_s, i18nd("kcm_pulseaudio", "Combined Recording") + u": "_s + device->description()},
                              {u"audio.position"_s, QJsonArray{u"FL"_s, u"FR"_s}},
                              {u"combine.audio.position"_s, QJsonArray{u"FL"_s, u"FR"_s}}};
            rules.append(QJsonObject{{u"matches"_s, QJsonArray{QJsonObject{{u"object.serial"_s, serial(device)}}}},
                                     {u"actions"_s, QJsonObject{{u"create-stream"_s, props}}}});
        }
        const QJsonObject args{
            {u"combine.mode"_s, u"source"_s},
            {u"node.name"_s, combinedName},
            {u"node.description"_s, i18nd("kcm_pulseaudio", "Combined Recording")},
            {u"combine.props"_s, QJsonObject{{u"audio.position"_s, QJsonArray{u"FL"_s, u"FR"_s}}, {u"node.virtual"_s, false}, {u"priority.session"_s, 0}}},
            {u"stream.props"_s, QJsonObject{{u"node.virtual"_s, false}, {u"node.dont-fallback"_s, true}, {u"node.passive"_s, true}}},
            {u"stream.rules"_s, rules}};
        p->start(u"pw-cli"_s, {u"-m"_s, u"load-module"_s, u"libpipewire-module-combine-stream"_s, json(args)});
    } else {
        QJsonObject capture{{u"target.object"_s, sourceSerial},
                            {u"plasma.pa.owner"_s, m_owner},
                            {u"plasma.pa.route"_s, r.key},
                            {u"stream.capture.sink"_s, monitor(a)},
                            {u"node.virtual"_s, false},
                            {u"node.dont-fallback"_s, true},
                            {u"node.name"_s, name + u".capture"_s},
                            {u"media.name"_s, i18nd("kcm_pulseaudio", "Listen: %1", a->description())}};
        QJsonObject playback{{u"target.object"_s, sinkSerial},
                             {u"plasma.pa.owner"_s, m_owner},
                             {u"plasma.pa.route"_s, r.key},
                             {u"node.virtual"_s, false},
                             {u"node.dont-fallback"_s, true},
                             {u"node.name"_s, name + u".playback"_s},
                             {u"media.name"_s, i18nd("kcm_pulseaudio", "Listen: %1", a->description())}};
        const QMap<QString, QString> positions{{u"front-left"_s, u"FL"_s},
                                               {u"front-right"_s, u"FR"_s},
                                               {u"front-center"_s, u"FC"_s},
                                               {u"lfe"_s, u"LFE"_s},
                                               {u"rear-left"_s, u"RL"_s},
                                               {u"rear-right"_s, u"RR"_s},
                                               {u"side-left"_s, u"SL"_s},
                                               {u"side-right"_s, u"SR"_s},
                                               {u"rear-center"_s, u"RC"_s},
                                               {u"mono"_s, u"MONO"_s},
                                               {u"front-left-of-center"_s, u"FLC"_s},
                                               {u"front-right-of-center"_s, u"FRC"_s},
                                               {u"top-center"_s, u"TC"_s},
                                               {u"top-front-left"_s, u"TFL"_s},
                                               {u"top-front-center"_s, u"TFC"_s},
                                               {u"top-front-right"_s, u"TFR"_s},
                                               {u"top-rear-left"_s, u"TRL"_s},
                                               {u"top-rear-center"_s, u"TRC"_s},
                                               {u"top-rear-right"_s, u"TRR"_s}};
        QStringList channels;
        for (const auto &ch : a->rawChannels())
            channels.append(positions.value(ch, ch.toUpper()));
        p->start(u"pw-loopback"_s,
                 {u"--name"_s,
                  name,
                  u"--channel-map"_s,
                  u"[ "_s + channels.join(u' ') + u" ]"_s,
                  u"--capture-props"_s,
                  json(capture),
                  u"--playback-props"_s,
                  json(playback)});
    }
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, &r, p](int code, QProcess::ExitStatus) {
        QTimer::singleShot(0, this, [this, &r, code, guard = QPointer<QProcess>(p)] {
            if (guard && r.process == guard)
                failed(r, 5, QString::number(code));
        });
    });
    r.startup.setSingleShot(true);
    r.startup.setInterval(5000);
    r.startup.disconnect(this);
    connect(&r.startup, &QTimer::timeout, this, [this, &r] {
        const bool present = r.mix ? source(combinedName) != nullptr : [&] {
            bool capture = false, playback = false;
            for (auto *s : PulseAudioQt::Context::instance()->sinkInputs())
                if (s->properties().value(u"node.name"_s).toString() == u"plasma-pa-"_s + r.key + u".playback"_s)
                    playback = sink(r.output) && s->deviceIndex() == sink(r.output)->index();
            for (auto *s : PulseAudioQt::Context::instance()->sourceOutputs())
                if (s->properties().value(u"node.name"_s).toString() == u"plasma-pa-"_s + r.key + u".capture"_s)
                    capture = source(r.input) && s->deviceIndex() == source(r.input)->index();
            return capture && playback;
        }();
        if (!present)
            failed(r, 7);
        else {
            r.running = true;
            Q_EMIT changed();
        }
    });
    r.startup.start();
}

void AudioRoutes::nativeStep(Route &r)
{
    if (r.commands.isEmpty()) {
        r.running = true;
        Q_EMIT changed();
        return;
    }
    auto *p = new QProcess(this);
    r.process = p;
    const auto command = r.commands.takeFirst();
    connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, &r, p](int code, QProcess::ExitStatus status) {
        bool ok;
        const auto id = QString::fromUtf8(p->readAllStandardOutput()).trimmed().toUInt(&ok);
        if (!ok || code != 0 || status != QProcess::NormalExit) {
            QTimer::singleShot(0, this, [this, &r, guard = QPointer<QProcess>(p)] {
                if (guard && r.process == guard)
                    failed(r, 9);
            });
            return;
        }
        r.modules.append(id);
        if (r.stopping)
            return;
        r.process = nullptr;
        p->deleteLater();
        nativeStep(r);
    });
    connect(p, &QProcess::errorOccurred, this, [this, &r](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            QTimer::singleShot(0, this, [this, &r, guard = QPointer<QProcess>(r.process)] {
                if (guard && r.process == guard)
                    failed(r, 4, u"pactl"_s);
            });
    });
    p->start(u"pactl"_s, command);
    QTimer::singleShot(5000, this, [this, &r, guard = QPointer<QProcess>(p)] {
        if (guard && r.process == guard && guard->state() != QProcess::NotRunning)
            failed(r, 9);
    });
}

void AudioRoutes::cleanOrphans()
{
    auto *c = PulseAudioQt::Context::instance();
    if (c->state() != PulseAudioQt::Context::State::Ready || c->server()->isPipeWire())
        return;
    auto routes = m_listen.values();
    routes.append(m_combined);
    QSet<uint> live;
    for (const auto &r : routes) {
        if (r->process || !r->commands.isEmpty())
            return;
        for (auto id : r->modules)
            live.insert(id);
    }
    for (auto *m : c->modules()) {
        if (!live.contains(m->index()) && m->argument().contains(u"plasma.pa.owner="_s + m_owner)) {
            QProcess p;
            p.start(u"pactl"_s, {u"unload-module"_s, QString::number(m->index())});
            p.waitForFinished(2000);
        }
    }
}

#include "moc_audioroutes.cpp"
