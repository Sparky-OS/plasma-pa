/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#include "routingcontroller.h"

#include <KLocalizedString>
#include <PulseAudioQt/Context>
#include <PulseAudioQt/Sink>
#include <PulseAudioQt/Source>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>

using namespace Qt::Literals::StringLiterals;

namespace
{
QDBusMessage call(const QString &method)
{
    return QDBusMessage::createMethodCall(u"org.kde.kded6"_s, u"/org/kde/plasma/AudioRoutes"_s, u"org.kde.plasma.AudioRoutes"_s, method);
}
QString unavailable()
{
    return i18nd("kcm_pulseaudio",
                 "The audio routing service is unavailable. Restart the KDE "
                 "background service and try again.");
}
} // namespace

RoutingController::RoutingController(QObject *parent)
    : QObject(parent)
{
    auto load = QDBusMessage::createMethodCall(u"org.kde.kded6"_s, u"/kded"_s, u"org.kde.kded6"_s, u"loadModule"_s);
    load.setArguments({u"audioshortcutsservice"_s});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(load), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        refresh();
    });
    QDBusConnection::sessionBus()
        .connect(u"org.kde.kded6"_s, u"/org/kde/plasma/AudioRoutes"_s, u"org.kde.plasma.AudioRoutes"_s, u"changed"_s, this, SLOT(refresh()));
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &RoutingController::refresh);
    m_timer.start();
}

QVariantMap RoutingController::listens() const
{
    return m_snapshot.value(u"listen"_s).toMap();
}
QVariantMap RoutingController::combined() const
{
    return m_snapshot.value(u"combined"_s).toMap();
}
QString RoutingController::serviceError() const
{
    return m_error;
}
QVariantList RoutingController::inputs() const
{
    return m_inputs;
}
QVariantList RoutingController::outputs() const
{
    return m_outputs;
}
namespace
{
QVariantList inputDevices()
{
    QVariantList result;
    for (auto *s : PulseAudioQt::Context::instance()->sources()) {
        if (!s->name().startsWith(u"plasma_pa_combined"_s)) {
            result.append(QVariantMap{{u"name"_s, s->name()}, {u"description"_s, s->description()}});
        }
    }
    return result;
}
QVariantList outputDevices()
{
    QVariantList result;
    for (auto *s : PulseAudioQt::Context::instance()->sinks()) {
        if (!s->name().startsWith(u"plasma_pa_combined"_s)) {
            result.append(QVariantMap{{u"name"_s, s->name()}, {u"description"_s, s->description()}});
        }
    }
    return result;
}
}

void RoutingController::refresh()
{
    if (m_pending) {
        return;
    }
    m_pending = true;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call(u"snapshot"_s)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        m_pending = false;
        const auto oldSnapshot = m_snapshot;
        const auto oldError = m_error;
        const auto oldInputs = m_inputs, oldOutputs = m_outputs;
        m_inputs = inputDevices();
        m_outputs = outputDevices();
        if (reply.isError()) {
            m_error = unavailable();
        } else {
            m_error.clear();
            m_snapshot = QJsonDocument::fromJson(reply.value().toUtf8()).toVariant().toMap();
        }
        if (m_snapshot != oldSnapshot || m_error != oldError || m_inputs != oldInputs || m_outputs != oldOutputs)
            Q_EMIT changed();
    });
}

void RoutingController::configure(const QString &method, const QString &source, const QString &output, bool enabled, const QString &id)
{
    auto request = call(method);
    request.setArguments(method == u"setListenRoute"_s ? QVariantList{id, source, output, enabled} : QVariantList{source, output, enabled});
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        const QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            m_error = unavailable();
            Q_EMIT changed();
        } else {
            refresh();
        }
    });
}
void RoutingController::setListen(const QString &source, const QString &output, bool enabled)
{
    configure(u"setListen"_s, source, output, enabled);
}
void RoutingController::setListenRoute(const QString &id, const QString &source, const QString &output, bool enabled)
{
    configure(u"setListenRoute"_s, source, output, enabled, id);
}
void RoutingController::setCombined(const QString &source, const QString &output, bool enabled)
{
    configure(u"setCombined"_s, source, output, enabled);
}

QString RoutingController::message(const QVariantMap &state) const
{
    if (!state.value(u"enabled"_s).toBool() && state.value(u"reason"_s).toInt() == 0) {
        return {};
    }
    const QString detail = state.value(u"detail"_s).toString();
    switch (state.value(u"reason"_s).toInt()) {
    case 1:
        return i18nd("kcm_pulseaudio",
                     "The sound server is unavailable. This "
                     "route will resume when it returns.");
    case 2:
        return i18nd("kcm_pulseaudio", "The input %1 is unavailable. This route will resume when it returns.", detail);
    case 3:
        return i18nd("kcm_pulseaudio", "The output %1 is unavailable. This route will resume when it returns.", detail);
    case 4:
        return i18nd("kcm_pulseaudio", "Could not start %1. Install the audio backend tools and try again.", detail);
    case 5:
        return i18nd("kcm_pulseaudio",
                     "The audio helper stopped (exit code %1). Check the sound "
                     "server and try again.",
                     detail);
    case 6:
        return i18nd("kcm_pulseaudio",
                     "The sound server did not provide a stable device identifier. "
                     "Reconnect the device and try again.");
    case 7:
        return i18nd("kcm_pulseaudio",
                     "The audio helper did not create the requested route. Check "
                     "the sound server and try again.");
    case 8:
        return i18nd("kcm_pulseaudio",
                     "This selection would feed audio back into its own input. "
                     "Choose another input or output.");
    case 9:
        return i18nd("kcm_pulseaudio",
                     "PulseAudio could not create this route. "
                     "Check the selected devices and try again.");
    case 10:
        return i18nd("kcm_pulseaudio", "Could not stop this route. Restore the audio backend tools and try again.");
    default:
        return state.value(u"running"_s).toBool() ? i18nd("kcm_pulseaudio", "Active") : i18nd("kcm_pulseaudio", "Starting…");
    }
}

#include "moc_routingcontroller.cpp"
