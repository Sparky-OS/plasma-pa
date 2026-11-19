/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "speakersetup.h"

#include "speakersetupconf.h"
#include "speakertestsignal.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtMath>

#include <KAuth/Action>
#include <KAuth/ExecuteJob>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

using namespace Qt::StringLiterals;

namespace
{
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

QStringList positionTokens(const QString &text)
{
    QStringList out;
    static const QRegularExpression re(u"[A-Za-z0-9]+"_s);
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        out << it.next().captured();
    }
    return out;
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

// The controls the filter should hold once the setup is running, as PipeWire names them in the node's Props: "node:Control".
QMap<QString, double> expectedControls(const QVariantMap &dev)
{
    QMap<QString, double> want;
    const QVariantMap distances = dev.value(u"distances"_s).toMap();
    double farthest = 0;
    for (auto it = distances.begin(); it != distances.end(); ++it) {
        farthest = qMax(farthest, it.value().toDouble());
    }
    for (auto it = distances.begin(); it != distances.end(); ++it) {
        const double d = SpeakerSetupConf::delaySeconds(it.value().toDouble(), farthest);
        if (d > 0.0) {
            want.insert(u"d_%1:Delay (s)"_s.arg(it.key()), d);
        }
    }
    const double crossover = dev.value(u"crossover"_s, SpeakerSetupConf::defaultCrossover).toDouble();
    const QStringList small = dev.value(u"small"_s).toStringList();
    for (const auto &p : small) {
        for (const QString &k : {u"hp1"_s, u"hp2"_s, u"lp1"_s, u"lp2"_s}) {
            want.insert(u"%1_%2:Freq"_s.arg(k, p), crossover);
        }
    }
    return want;
}
} // namespace

SpeakerSetup::SpeakerSetup(QObject *parent)
    : QObject(parent)
{
    loadSettings();
    m_testTimer.setInterval(100);
    refresh();
}

SpeakerSetup::~SpeakerSetup()
{
    stopTest();
}

double SpeakerSetup::speedOfSound() const
{
    return SpeakerSetupConf::speedOfSound;
}
double SpeakerSetup::minDistance() const
{
    return SpeakerSetupConf::minDistance;
}
double SpeakerSetup::maxDistance() const
{
    return SpeakerSetupConf::maxDistance;
}
int SpeakerSetup::minCrossover() const
{
    return SpeakerSetupConf::minCrossover;
}
int SpeakerSetup::maxCrossover() const
{
    return SpeakerSetupConf::maxCrossover;
}
int SpeakerSetup::defaultCrossover() const
{
    return SpeakerSetupConf::defaultCrossover;
}

double SpeakerSetup::angle(const QString &position, const QStringList &positions) const
{
    return SpeakerSetupConf::angle(position, positions);
}

double SpeakerSetup::delaySeconds(double distance, double farthest) const
{
    return SpeakerSetupConf::delaySeconds(distance, farthest);
}

QString SpeakerSetup::sanitiseLabel(const QString &description) const
{
    return SpeakerSetupConf::sanitiseLabel(description, 56);
}

bool SpeakerSetup::validLabel(const QString &label) const
{
    return SpeakerSetupConf::validLabel(label);
}

void SpeakerSetup::loadSettings()
{
    const KConfigGroup g(KSharedConfig::openConfig(u"kcm_pulseaudiorc"_s), u"SpeakerSetup"_s);
    m_feet = g.readEntry("Feet", false);
    m_testSeconds = qBound(1, g.readEntry("TestSeconds", 2), 10);
}

void SpeakerSetup::setFeet(bool feet)
{
    if (m_feet == feet) {
        return;
    }
    m_feet = feet;
    KConfigGroup(KSharedConfig::openConfig(u"kcm_pulseaudiorc"_s), u"SpeakerSetup"_s).writeEntry("Feet", feet);
    KSharedConfig::openConfig(u"kcm_pulseaudiorc"_s)->sync();
    Q_EMIT settingsChanged();
}

void SpeakerSetup::setTestSeconds(int seconds)
{
    seconds = qBound(1, seconds, 10);
    if (m_testSeconds == seconds) {
        return;
    }
    m_testSeconds = seconds;
    KConfigGroup(KSharedConfig::openConfig(u"kcm_pulseaudiorc"_s), u"SpeakerSetup"_s).writeEntry("TestSeconds", seconds);
    KSharedConfig::openConfig(u"kcm_pulseaudiorc"_s)->sync();
    Q_EMIT settingsChanged();
}

QString SpeakerSetup::userBase() const
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
}

void SpeakerSetup::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void SpeakerSetup::setMessage(const QString &type, const QString &text)
{
    m_message = type.isEmpty() ? QVariantMap() : QVariantMap{{u"type"_s, type}, {u"text"_s, text}};
    Q_EMIT messageChanged();
}

void SpeakerSetup::dismissMessage()
{
    setMessage({}, {});
}

void SpeakerSetup::wait(int ms) const
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

SpeakerSetup::Run SpeakerSetup::run(const QString &program, const QStringList &args, int timeoutMs) const
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
    if (p.state() != QProcess::NotRunning) {
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

QJsonDocument SpeakerSetup::pwDump() const
{
    const Run r = run(u"pw-dump"_s, {}, 5000);
    if (r.failedToStart || r.code != 0) {
        return {};
    }
    return QJsonDocument::fromJson(r.out);
}

QString SpeakerSetup::defaultSinkNow() const
{
    const Run r = run(u"pactl"_s, {u"get-default-sink"_s}, 4000);
    return r.failedToStart || r.code != 0 ? QString() : QString::fromUtf8(r.out).trimmed();
}

QVariantList SpeakerSetup::readDevices() const
{
    QVariantList out;
    const auto doc = pwDump();
    if (!doc.isArray()) {
        return out;
    }
    QSet<QString> setups;
    QSet<QString> present;
    for (const auto &item : doc.array()) {
        const auto props = item.toObject().value(u"info"_s).toObject().value(u"props"_s).toObject();
        const QString name = propString(props, u"node.name"_s);
        if (propString(props, u"media.class"_s) == u"Audio/Sink"_s) {
            present.insert(name);
            if (name.startsWith(u"speaker_setup."_s)) {
                setups.insert(name);
            }
        }
    }
    for (const auto &item : doc.array()) {
        const auto obj = item.toObject();
        if (obj.value(u"type"_s).toString() != u"PipeWire:Interface:Node"_s) {
            continue;
        }
        const auto info = obj.value(u"info"_s).toObject();
        const auto props = info.value(u"props"_s).toObject();
        if (propString(props, u"media.class"_s) != u"Audio/Sink"_s) {
            continue;
        }
        const QString name = propString(props, u"node.name"_s);
        const auto params = info.value(u"params"_s).toObject();
        QStringList tokens = positionTokens(propString(props, u"audio.position"_s));
        if (tokens.isEmpty()) {
            for (const char *key : {"Format", "EnumFormat"}) {
                const auto arr = params.value(QLatin1String(key)).toArray();
                if (!arr.isEmpty()) {
                    for (const auto &p : arr.at(0).toObject().value(u"position"_s).toArray()) {
                        tokens << p.toString();
                    }
                }
                if (!tokens.isEmpty()) {
                    break;
                }
            }
        }
        int channels = tokens.size();
        if (channels == 0) {
            channels = propString(props, u"audio.channels"_s).toInt();
        }
        QStringList known;
        QStringList unknown;
        for (const auto &t : tokens) {
            (SpeakerSetupConf::validPosition(t) ? known : unknown) << t;
        }
        QString reason;
        const bool setupSink = SpeakerSetupConf::isSetupSink(name);
        const bool deviceConnected = !setupSink || present.contains(name.mid(QStringLiteral("speaker_setup.").size()));
        if (setupSink && !deviceConnected) {
            reason = i18nd("kcm_pulseaudio", "The device of this speaker setup is not connected. The setup waits for it and plays nowhere else.");
        } else if (setupSink) {
            reason = i18nd("kcm_pulseaudio", "This is the speaker setup of another device. Set up the speakers from that device.");
        } else if (channels <= 2) {
            reason = channels == 0 ? i18nd("kcm_pulseaudio", "The channels of this device could not be read, so Speaker Setup is not offered.")
                                   : i18nd("kcm_pulseaudio",
                                           "Speaker Setup is for devices whose current profile has more than two channels. The profile of this device has %1.",
                                           channels);
        } else if (channels > 8) {
            reason = i18nd("kcm_pulseaudio", "This device has more than eight channels, which Speaker Setup does not handle.");
        } else if (!unknown.isEmpty() || known.size() != channels) {
            reason = i18nd("kcm_pulseaudio", "This device names speakers that Speaker Setup does not know: %1.", unknown.join(u", "_s));
        } else if (!SpeakerSetupConf::validNodeName(name)) {
            reason = i18nd("kcm_pulseaudio", "Speaker Setup cannot change this device yet.");
        }
        QVariantMap dev{{u"name"_s, name},
                        {u"description"_s, propString(props, u"node.description"_s)},
                        {u"channels"_s, channels},
                        {u"positions"_s, QVariant(known)},
                        {u"eligible"_s, reason.isEmpty()},
                        {u"reason"_s, reason},
                        {u"setupActive"_s, setups.contains(SpeakerSetupConf::sinkName(name))},
                        {u"setupSink"_s, SpeakerSetupConf::sinkName(name)},
                        {u"deviceConnected"_s, deviceConnected}};
        out << dev;
    }
    return out;
}

QVariantMap SpeakerSetup::deviceInfo(const QString &node) const
{
    for (const auto &d : m_devices) {
        if (d.toMap().value(u"name"_s).toString() == node) {
            return d.toMap();
        }
    }
    return {};
}

QVariantMap SpeakerSetup::deviceState(bool system, const QString &node) const
{
    return (system ? m_system : m_user).value(u"devices"_s).toMap().value(node).toMap();
}

void SpeakerSetup::refresh()
{
    m_user = SpeakerSetupConf::read(userBase());
    m_system = SpeakerSetupConf::read(u"/etc"_s);
    const QVariantList devices = readDevices();
    if (devices != m_devices) {
        m_devices = devices;
        Q_EMIT devicesChanged();
    }
    m_defaultSink = defaultSinkNow();
    Q_EMIT changed();
}

bool SpeakerSetup::writeScope(bool system, const QVariantMap &state, QString *error)
{
    if (!system) {
        QString code;
        const bool ok = SpeakerSetupConf::write(userBase(), state, &code);
        if (!ok && error) {
            *error = describeWriteError(code);
        }
        return ok;
    }
    KAuth::Action action(u"org.kde.plasma.pa.speakers.write"_s);
    action.setHelperId(u"org.kde.plasma.pa.speakers"_s);
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

bool SpeakerSetup::activate(const QString &node)
{
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
                if (propString(props, u"media.class"_s) == u"Audio/Sink"_s && propString(props, u"node.name"_s) == node) {
                    wait(700);
                    return true;
                }
            }
        }
    }
    return false;
}

QString SpeakerSetup::check(const QString &node, const QVariantMap &state, bool makeDefault)
{
    const QVariantMap dev = state.value(u"devices"_s).toMap().value(node).toMap();
    const QString sink = SpeakerSetupConf::sinkName(node);
    QJsonObject sinkObject;
    QJsonObject deviceObject;
    // The setup's output is made when PipeWire loads the filter, which can be a moment after the device is back.
    for (int tries = 0; tries < 25; ++tries) {
        sinkObject = {};
        deviceObject = {};
        for (const auto &item : pwDump().array()) {
            const auto o = item.toObject();
            const auto props = o.value(u"info"_s).toObject().value(u"props"_s).toObject();
            const QString name = propString(props, u"node.name"_s);
            if (name == sink) {
                sinkObject = o;
            } else if (name == node) {
                deviceObject = o;
            }
        }
        if (dev.isEmpty() || !sinkObject.isEmpty()) {
            break;
        }
        wait(200);
    }
    if (deviceObject.isEmpty()) {
        return i18nd("kcm_pulseaudio", "This device is no longer connected.");
    }
    if (dev.isEmpty()) {
        return sinkObject.isEmpty() ? QString() : i18nd("kcm_pulseaudio", "The speaker setup of this device is still there.");
    }
    if (sinkObject.isEmpty()) {
        return i18nd("kcm_pulseaudio", "PipeWire did not make the output of the speaker setup.");
    }
    const QStringList positions = dev.value(u"positions"_s).toStringList();
    const auto sinkProps = sinkObject.value(u"info"_s).toObject().value(u"props"_s).toObject();
    if (propString(sinkProps, u"audio.channels"_s).toInt() != positions.size()) {
        return i18nd("kcm_pulseaudio", "The speaker setup came up with %1 channels, not the %2 of the device.", propString(sinkProps, u"audio.channels"_s).toInt(), positions.size());
    }
    // The filter's own controls, as the running graph holds them.
    QMap<QString, double> got;
    for (const auto &pv : sinkObject.value(u"info"_s).toObject().value(u"params"_s).toObject().value(u"Props"_s).toArray()) {
        const auto arr = pv.toObject().value(u"params"_s).toArray();
        for (int i = 0; i + 1 < arr.size(); i += 2) {
            if (arr.at(i).isString() && arr.at(i + 1).isDouble()) {
                got.insert(arr.at(i).toString(), arr.at(i + 1).toDouble());
            }
        }
    }
    const auto want = expectedControls(dev);
    for (auto it = want.begin(); it != want.end(); ++it) {
        if (!got.contains(it.key()) || qAbs(got.value(it.key()) - it.value()) > 1e-4 * qMax(1.0, qAbs(it.value())) + 1e-6) {
            return i18nd("kcm_pulseaudio", "The sound system did not take the value %1 of the speaker setup.", it.key().section(u':', 0, 0).section(u'_', 1));
        }
    }
    // The setup does not pin the graph to a rate.
    const Run meta = run(u"pw-metadata"_s, {u"-n"_s, u"settings"_s}, 4000);
    const QString forceRate = QRegularExpression(u"key:'clock.force-rate' value:'([^']*)'"_s).match(QString::fromUtf8(meta.out)).captured(1);
    if (!forceRate.isEmpty() && forceRate != u"0"_s) {
        return i18nd("kcm_pulseaudio", "The speaker setup holds the sound system at %1 Hz.", forceRate);
    }
    if (makeDefault) {
        run(u"pactl"_s, {u"set-default-sink"_s, sink}, 4000);
        if (defaultSinkNow() != sink) {
            return i18nd("kcm_pulseaudio", "The speaker setup could not be made the default output.");
        }
    }
    // A short stretch of silence through the setup shows that the device takes all its channels.
    const QString exe = QStandardPaths::findExecutable(u"pw-cat"_s);
    if (exe.isEmpty()) {
        return i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the speaker setup could not be tried.", u"pw-cat"_s);
    }
    QProcess cat;
    cat.start(exe,
              {u"--playback"_s, u"--raw"_s, u"--target"_s, sink, u"--rate"_s, u"48000"_s, u"--channels"_s, QString::number(positions.size()), u"--channel-map"_s,
               positions.join(u','), u"--format"_s, u"f32"_s, u"-"_s});
    if (!cat.waitForStarted(3000)) {
        return i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the speaker setup could not be tried.", u"pw-cat"_s);
    }
    cat.write(QByteArray(48000 * positions.size() * int(sizeof(float)) / 2, '\0'));
    cat.closeWriteChannel();
    QString problem;
    bool seen = false;
    for (int i = 0; i < 25 && cat.state() != QProcess::NotRunning; ++i) {
        wait(100);
        for (const auto &item : pwDump().array()) {
            const auto o = item.toObject();
            const auto props = o.value(u"info"_s).toObject().value(u"props"_s).toObject();
            if (propString(props, u"node.name"_s) != node) {
                continue;
            }
            const auto info = o.value(u"info"_s).toObject();
            if (info.value(u"state"_s).toString() == u"error"_s) {
                problem = i18nd("kcm_pulseaudio", "The device went into an error state.");
            }
            const auto fmt = info.value(u"params"_s).toObject().value(u"Format"_s).toArray();
            if (info.value(u"state"_s).toString() == u"running"_s && !fmt.isEmpty()) {
                seen = true;
                const int ch = fmt.at(0).toObject().value(u"channels"_s).toInt();
                if (ch != positions.size()) {
                    problem = i18nd("kcm_pulseaudio", "The device ran with %1 channels, not the %2 of the speaker setup.", ch, positions.size());
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
        problem = i18nd("kcm_pulseaudio", "The device did not start playing through the speaker setup.");
    }
    return problem;
}

void SpeakerSetup::apply(bool system, const QVariantMap &newState, const QString &node, bool makeDefault)
{
    if (m_busy) {
        return;
    }
    stopTest();
    setBusy(true);
    setMessage({}, {});
    const auto finish = [this, &node](bool ok, const QString &type, const QString &text) {
        setMessage(type, text);
        setBusy(false);
        refresh();
        Q_EMIT applied(ok, node);
    };

    const QString bad = SpeakerSetupConf::validate(newState);
    if (!bad.isEmpty()) {
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "Nothing was changed: the value %1 is not one the speaker setup accepts.", bad));
        return;
    }
    if (!SpeakerSetupConf::validNodeName(node) || deviceInfo(node).isEmpty()) {
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "Nothing was changed: the device is no longer connected."));
        return;
    }
    // PipeWire's own reader looks at the file before it is put in place.
    const QString dumper = QStandardPaths::findExecutable(u"spa-json-dump"_s);
    if (!dumper.isEmpty()) {
        QTemporaryDir tmp;
        const auto files = SpeakerSetupConf::render(newState);
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
    const QString previousDefault = defaultSinkNow();
    QString error;
    if (!writeScope(system, newState, &error)) {
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "Nothing was changed: the settings could not be written (%1).", error));
        return;
    }
    const bool wanted = !newState.value(u"devices"_s).toMap().value(node).toMap().isEmpty();
    // When the setup goes away its output stops being the default: the device itself is, again.
    const bool wasDefault = previousDefault == SpeakerSetupConf::sinkName(node);
    const bool ok = activate(node);
    QString problem = ok ? check(node, newState, wanted && makeDefault) : i18nd("kcm_pulseaudio", "The sound system did not come back after the restart.");
    if (problem.isEmpty() && !wanted && wasDefault) {
        run(u"pactl"_s, {u"set-default-sink"_s, node}, 4000);
        if (defaultSinkNow() != node) {
            problem = i18nd("kcm_pulseaudio", "The device could not be made the default output again.");
        }
    }
    if (!problem.isEmpty()) {
        // The earlier settings are written back, and the sound system is given them again.
        QString restoreError;
        const bool restored = writeScope(system, previous, &restoreError);
        activate(node);
        if (!previousDefault.isEmpty()) {
            run(u"pactl"_s, {u"set-default-sink"_s, previousDefault}, 4000);
        }
        const QString note = restored ? i18nd("kcm_pulseaudio", "The earlier settings stay.")
                                      : i18nd("kcm_pulseaudio", "The earlier settings could not be put back (%1); check the files named 50-speaker-page.conf.", restoreError);
        finish(false, u"error"_s, i18nd("kcm_pulseaudio", "The change was not kept. %1 %2", problem, note));
        return;
    }
    if (wanted && !makeDefault) {
        finish(true, u"positive"_s, i18nd("kcm_pulseaudio", "The change is in use. Programs that play to the device itself do not pass through the speaker setup."));
        return;
    }
    finish(true, u"positive"_s, i18nd("kcm_pulseaudio", "The change is in use."));
}

void SpeakerSetup::testChannel(const QString &node, const QString &position)
{
    startTest(node, {position});
}

void SpeakerSetup::testAll(const QString &node)
{
    const QVariantMap dev = deviceInfo(node);
    startTest(node, dev.value(u"positions"_s).toStringList());
}

void SpeakerSetup::startTest(const QString &node, const QStringList &which)
{
    stopTest();
    const QVariantMap dev = deviceInfo(node);
    if (dev.isEmpty() || !dev.value(u"eligible"_s).toBool()) {
        setMessage(u"error"_s, i18nd("kcm_pulseaudio", "This device is no longer connected."));
        return;
    }
    const QStringList positions = dev.value(u"positions"_s).toStringList();
    // Through the speaker setup when there is one, so that what the person hears has the delays and the bass management; else to the device itself.
    const QString target = dev.value(u"setupActive"_s).toBool() ? dev.value(u"setupSink"_s).toString() : node;
    const QString exe = QStandardPaths::findExecutable(u"pw-cat"_s);
    if (exe.isEmpty()) {
        setMessage(u"error"_s, i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the test signal cannot be played.", u"pw-cat"_s));
        return;
    }
    m_testOrder.clear();
    for (const auto &p : which) {
        if (positions.contains(p)) {
            m_testOrder << p;
        }
    }
    if (m_testOrder.isEmpty()) {
        return;
    }
    const QByteArray data = SpeakerTestSignal::sequence(positions, m_testOrder, m_testSeconds);
    m_cat = new QProcess(this);
    connect(m_cat, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString err = QString::fromUtf8(m_cat->readAllStandardError()).trimmed();
        m_cat->deleteLater();
        m_cat = nullptr;
        m_testTimer.stop();
        m_testing.clear();
        Q_EMIT testingChanged();
        if (status != QProcess::NormalExit || code != 0) {
            setMessage(u"error"_s, i18nd("kcm_pulseaudio", "The test signal could not be played (%1).", err.isEmpty() ? i18nd("kcm_pulseaudio", "the player stopped") : err));
        }
    });
    m_cat->start(exe,
                 {u"--playback"_s, u"--raw"_s, u"--target"_s, target, u"--rate"_s, QString::number(SpeakerTestSignal::rate), u"--channels"_s,
                  QString::number(positions.size()), u"--channel-map"_s, positions.join(u','), u"--format"_s, u"f32"_s, u"-"_s});
    if (!m_cat->waitForStarted(3000)) {
        m_cat->deleteLater();
        m_cat = nullptr;
        setMessage(u"error"_s, i18nd("kcm_pulseaudio", "The tool %1 is not installed, so the test signal cannot be played.", u"pw-cat"_s));
        return;
    }
    m_cat->write(data);
    m_cat->closeWriteChannel();
    m_testStep = 0;
    m_clock.start();
    disconnect(&m_testTimer, nullptr, this, nullptr);
    connect(&m_testTimer, &QTimer::timeout, this, [this] {
        const double stepMs = 1000.0 * SpeakerTestSignal::burst(1, m_testSeconds).first / SpeakerTestSignal::rate;
        const int step = int(m_clock.elapsed() / stepMs);
        const double within = m_clock.elapsed() - step * stepMs;
        QString now;
        if (step < m_testOrder.size() && within < 1000.0 * m_testSeconds) {
            now = m_testOrder.at(step);
        }
        if (now != m_testing) {
            m_testing = now;
            Q_EMIT testingChanged();
        }
    });
    m_testTimer.start();
    m_testing = m_testOrder.first();
    Q_EMIT testingChanged();
}

void SpeakerSetup::stopTest()
{
    if (!m_cat) {
        return;
    }
    m_cat->disconnect(this);
    m_cat->kill();
    m_cat->waitForFinished(1000);
    m_cat->deleteLater();
    m_cat = nullptr;
    m_testTimer.stop();
    m_testing.clear();
    Q_EMIT testingChanged();
}

#include "moc_speakersetup.cpp"
