/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

/**
 * What the Speaker Setup page shows and sets for a device with more than two channels.
 *
 * It reads the devices PipeWire reports (pw-dump: the channel map of each sink's current profile) and the person's own
 * drop-in (SpeakerSetupConf, for this account or, through KAuth, for every account). A change is validated, written,
 * applied (PipeWire is restarted so it loads the filter) and then checked against the running graph. If the check fails
 * the earlier file is written back and the reason is said; nothing stays half applied.
 *
 * With every speaker at its defaults no file exists and the device has no filter in its signal path.
 */
class SpeakerSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap userState READ userState NOTIFY changed)
    Q_PROPERTY(QVariantMap systemState READ systemState NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QString defaultSink READ defaultSink NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QVariantMap message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString testing READ testing NOTIFY testingChanged)
    Q_PROPERTY(bool testRunning READ testRunning NOTIFY testingChanged)
    Q_PROPERTY(bool feet READ feet WRITE setFeet NOTIFY settingsChanged)
    Q_PROPERTY(int testSeconds READ testSeconds WRITE setTestSeconds NOTIFY settingsChanged)
    Q_PROPERTY(double speedOfSound READ speedOfSound CONSTANT)
    Q_PROPERTY(double minLevel READ minLevel CONSTANT)
    Q_PROPERTY(double maxLevel READ maxLevel CONSTANT)
    Q_PROPERTY(double minDistance READ minDistance CONSTANT)
    Q_PROPERTY(double maxDistance READ maxDistance CONSTANT)
    Q_PROPERTY(int minCrossover READ minCrossover CONSTANT)
    Q_PROPERTY(int maxCrossover READ maxCrossover CONSTANT)
    Q_PROPERTY(int defaultCrossover READ defaultCrossover CONSTANT)
public:
    explicit SpeakerSetup(QObject *parent = nullptr);
    ~SpeakerSetup() override;

    QVariantMap userState() const { return m_user; }
    QVariantMap systemState() const { return m_system; }
    QVariantList devices() const { return m_devices; }
    QString defaultSink() const { return m_defaultSink; }
    bool busy() const { return m_busy; }
    QVariantMap message() const { return m_message; }
    QString testing() const { return m_testing; }
    bool testRunning() const { return m_cat != nullptr; }
    bool feet() const { return m_feet; }
    void setFeet(bool feet);
    int testSeconds() const { return m_testSeconds; }
    void setTestSeconds(int seconds);
    double speedOfSound() const;
    double minLevel() const;
    double maxLevel() const;
    double minDistance() const;
    double maxDistance() const;
    int minCrossover() const;
    int maxCrossover() const;
    int defaultCrossover() const;

    Q_INVOKABLE void refresh();
    /** Where a speaker stands in the room, degrees clockwise from the front centre. */
    Q_INVOKABLE double angle(const QString &position, const QStringList &positions) const;
    /** The delay in seconds a speaker at distance (metres) gets when the farthest speaker is at farthest. */
    Q_INVOKABLE double delaySeconds(double distance, double farthest) const;
    /** A device's description cut to what a setup's name may hold; empty when nothing usable is left. */
    Q_INVOKABLE QString sanitiseLabel(const QString &description) const;
    /** Whether a label is one the drop-in accepts. */
    Q_INVOKABLE bool validLabel(const QString &label) const;
    /** The setup, for this account or every account, as the person set it for one device. */
    Q_INVOKABLE QVariantMap deviceState(bool system, const QString &node) const;
    /**
     * Replace the person's speaker setup for one scope with newState and check the result. node is the device being changed;
     * makeDefault says whether the setup's output also becomes this account's default output.
     */
    Q_INVOKABLE void apply(bool system, const QVariantMap &newState, const QString &node, bool makeDefault);
    Q_INVOKABLE void dismissMessage();
    Q_INVOKABLE void testChannel(const QString &node, const QString &position);
    Q_INVOKABLE void testAll(const QString &node);
    Q_INVOKABLE void stopTest();

Q_SIGNALS:
    void changed();
    void devicesChanged();
    void busyChanged();
    void messageChanged();
    void testingChanged();
    void settingsChanged();
    void applied(bool ok, const QString &node);

private:
    struct Run {
        int code = -1;
        QByteArray out;
        QByteArray err;
        bool failedToStart = false;
        bool timedOut = false;
    };
    Run run(const QString &program, const QStringList &args, int timeoutMs) const;
    void wait(int ms) const;
    void setBusy(bool busy);
    void setMessage(const QString &type, const QString &text);
    QString userBase() const;
    bool writeScope(bool system, const QVariantMap &state, QString *error);
    bool activate(const QString &node);
    QString check(const QString &node, const QVariantMap &state, bool makeDefault);
    QString defaultSinkNow() const;
    QJsonDocument pwDump() const;
    QVariantList readDevices() const;
    QVariantMap deviceInfo(const QString &node) const;
    void startTest(const QString &node, const QStringList &which);
    void loadSettings();

    QVariantMap m_user;
    QVariantMap m_system;
    QVariantList m_devices;
    QVariantMap m_message;
    QString m_defaultSink;
    QString m_testing;
    bool m_busy = false;
    bool m_feet = false;
    int m_testSeconds = 2;
    QProcess *m_cat = nullptr;
    QTimer m_testTimer;
    QElapsedTimer m_clock;
    QStringList m_testOrder;
    int m_testStep = 0;
};
