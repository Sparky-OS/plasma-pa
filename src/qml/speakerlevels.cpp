/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "speakerlevels.h"

#include "speakerlevelmath.h"

#include <PulseAudioQt/Context>
#include <PulseAudioQt/Sink>

SpeakerLevels::SpeakerLevels(QObject *parent)
    : QObject(parent)
{
    auto *context = PulseAudioQt::Context::instance();
    connect(context, &PulseAudioQt::Context::sinkAdded, this, &SpeakerLevels::rebind);
    connect(context, &PulseAudioQt::Context::sinkRemoved, this, &SpeakerLevels::rebind);
    m_pendingClear.setSingleShot(true);
    m_pendingClear.setInterval(1500);
    connect(&m_pendingClear, &QTimer::timeout, this, [this] {
        m_pending.clear();
    });
}

void SpeakerLevels::setNode(const QString &node)
{
    if (m_node == node) {
        return;
    }
    m_node = node;
    Q_EMIT nodeChanged();
    rebind();
}

void SpeakerLevels::rebind()
{
    PulseAudioQt::Sink *found = nullptr;
    for (auto *sink : PulseAudioQt::Context::instance()->sinks()) {
        if (sink->name() == m_node) {
            found = sink;
            break;
        }
    }
    if (found != m_sink.data()) {
        if (m_sink) {
            disconnect(m_sink, nullptr, this, nullptr);
        }
        m_sink = found;
        if (m_sink) {
            connect(m_sink, &PulseAudioQt::VolumeObject::channelVolumesChanged, this, &SpeakerLevels::levelsChanged);
        }
    }
    Q_EMIT levelsChanged();
}

QVariantList SpeakerLevels::levels() const
{
    QVariantList out;
    if (m_sink) {
        const auto volumes = m_sink->channelVolumes();
        for (const auto v : volumes) {
            out << SpeakerLevelMath::volumeToDb(v);
        }
    }
    return out;
}

void SpeakerLevels::setLevel(int channel, double db, bool raised)
{
    if (m_sink && channel >= 0 && channel < m_sink->channelVolumes().size()) {
        QVector<qint64> volumes = m_pending.isEmpty() ? m_sink->channelVolumes() : m_pending;
        volumes[channel] = SpeakerLevelMath::dbToVolume(SpeakerLevelMath::clampDb(db, raised));
        m_pending = volumes;
        m_pendingClear.start();
        m_sink->setChannelVolumes(volumes);
    }
}

double SpeakerLevels::minimumDb() const
{
    return SpeakerLevelMath::minimumDb;
}

double SpeakerLevels::maximumDb(bool raised) const
{
    return SpeakerLevelMath::maximumDb(raised);
}

#include "moc_speakerlevels.cpp"
