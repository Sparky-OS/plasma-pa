/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QVariantList>

namespace PulseAudioQt
{
class Sink;
}

/**
 * The levels of a device's speakers: its channel volumes, the same values "Show Channels" shows, as dB.
 *
 * Nothing is written to a file and no filter is made: a change goes to the sound server at once and every view of the
 * volume (this page, Show Channels, the volume widget) follows it through the server's own notifications.
 */
class SpeakerLevels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString node READ node WRITE setNode NOTIFY nodeChanged)
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(bool available READ available NOTIFY levelsChanged)
public:
    explicit SpeakerLevels(QObject *parent = nullptr);

    QString node() const { return m_node; }
    void setNode(const QString &node);
    /** The level of each channel in dB, in the order of the device's channel map; empty when the device is not there. */
    QVariantList levels() const;
    bool available() const { return !m_sink.isNull(); }

    /** Set channel number `channel` to db, kept inside the range the Sound page allows (0 dB, or up to +10.57 dB when `raised`). */
    Q_INVOKABLE void setLevel(int channel, double db, bool raised);
    Q_INVOKABLE double minimumDb() const;
    Q_INVOKABLE double maximumDb(bool raised) const;

Q_SIGNALS:
    void nodeChanged();
    void levelsChanged();

private:
    void rebind();

    QString m_node;
    QPointer<PulseAudioQt::Sink> m_sink;
    // The volumes last sent: the server answers a moment later, and a second change before that must build on the first.
    QVector<qint64> m_pending;
    QTimer m_pendingClear;
};
