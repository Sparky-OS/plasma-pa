/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QJsonDocument>
#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>

/**
 * What the Sound page shows and sets about formats and rates.
 *
 * It reads three things and decides nothing of its own:
 *  - what the edition's sparky-stereo-audio chose and why (its choices.json and its `show` text, in the person's language),
 *  - what PipeWire reports for each device and for the graph (pw-dump, pw-metadata) and what the kernel reads from a display (ELD),
 *  - the person's own drop-ins (SoundFormatConf), for this account or, through KAuth, for every account.
 *
 * A change is validated, written, applied and then checked against the running graph or device. If the check fails the earlier
 * files are written back and the reason is said; nothing stays half applied.
 */
class SoundFormats : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap engine READ engine NOTIFY changed)
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY changed)
    Q_PROPERTY(QVariantMap userState READ userState NOTIFY changed)
    Q_PROPERTY(QVariantMap systemState READ systemState NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantMap live READ live NOTIFY liveChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap message READ message NOTIFY messageChanged)
public:
    explicit SoundFormats(QObject *parent = nullptr);

    QVariantMap engine() const { return m_engine; }
    QVariantMap graph() const { return m_graph; }
    QVariantMap userState() const { return m_user; }
    QVariantMap systemState() const { return m_system; }
    QVariantList devices() const { return m_devices; }
    QVariantMap live() const { return m_live; }
    bool busy() const { return m_busy; }
    QVariantMap message() const { return m_message; }

    Q_INVOKABLE void refresh();
    /** The format a device runs in now, "48000 Hz, 24 bit" style, or an empty string when it is not open. */
    Q_INVOKABLE QString inUse(const QString &nodeName) const;
    /**
     * Replace the person's settings for one scope with newState and check the result.
     * what is "graph", "quality" or "device:NODE"; it chooses how the change is applied and checked.
     */
    Q_INVOKABLE void apply(bool system, const QVariantMap &newState, const QString &what);
    Q_INVOKABLE void dismissMessage();

Q_SIGNALS:
    void changed();
    void devicesChanged();
    void liveChanged();
    void busyChanged();
    void messageChanged();
    void applied(bool ok, const QString &what);

private:
    struct Run {
        int code = -1;
        QByteArray out;
        QByteArray err;
        bool failedToStart = false;
        bool timedOut = false;
    };
    Run run(const QString &program, const QStringList &args, int timeoutMs, const QByteArray &input = {}) const;
    void wait(int ms) const;
    void setBusy(bool busy);
    void setMessage(const QString &type, const QString &text);
    QString userBase() const;
    bool writeScope(bool system, const QVariantMap &state, QString *error);
    bool activate(const QString &what, bool removal);
    QString check(const QString &what, const QVariantMap &state);
    QString probe(const QString &node, const QVariantMap &choice);
    QList<QPair<int, QString>> pinnedStreams() const;
    QVariantMap readEngine() const;
    QVariantMap readGraph() const;
    QVariantList readDevices(const QVariantMap &engine, QVariantMap *live) const;
    QJsonDocument pwDump() const;
    static QVariantMap parseEld(const QString &text);

    QVariantMap m_engine;
    QVariantMap m_graph;
    QVariantMap m_user;
    QVariantMap m_system;
    QVariantList m_devices;
    QVariantMap m_live;
    QVariantMap m_message;
    bool m_busy = false;
};
