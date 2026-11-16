/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <KSharedConfig>
#include <QMap>
#include <QObject>
#include <QTimer>
#include <memory>

class AudioRoutes : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.plasma.AudioRoutes")
public:
    explicit AudioRoutes(QObject *parent);
    ~AudioRoutes() override;
    Q_SCRIPTABLE QString snapshot() const;
    Q_SCRIPTABLE void setListen(const QString &source, const QString &output, bool enabled);
    Q_SCRIPTABLE void setListenRoute(const QString &id, const QString &source, const QString &output, bool enabled);
    Q_SCRIPTABLE void moveListen(const QString &id, bool playback, uint deviceIndex);
    Q_SCRIPTABLE void setCombined(const QString &source, const QString &output, bool enabled);
Q_SIGNALS:
    Q_SCRIPTABLE void changed();

private:
    struct Route;
    void schedule();
    void reconcile();
    void save(Route &route);
    void start(Route &route);
    bool stop(Route &route);
    void nativeStep(Route &route);
    void failed(Route &route, int reason, const QString &detail = {});
    bool wouldLoop(const Route &route) const;
    void cleanOrphans();
    QString m_owner;
    KSharedConfig::Ptr m_config;
    QMap<QString, std::shared_ptr<Route>> m_listen;
    std::shared_ptr<Route> m_combined;
    QTimer m_timer;
};
