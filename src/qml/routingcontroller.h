/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

class RoutingController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap listens READ listens NOTIFY changed)
    Q_PROPERTY(QVariantMap combined READ combined NOTIFY changed)
    Q_PROPERTY(QString serviceError READ serviceError NOTIFY changed)
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY changed)
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
public:
    explicit RoutingController(QObject *parent = nullptr);
    QVariantMap listens() const;
    QVariantMap combined() const;
    QString serviceError() const;
    QVariantList inputs() const;
    QVariantList outputs() const;
    Q_INVOKABLE void setListen(const QString &source, const QString &output, bool enabled);
    Q_INVOKABLE void setCombined(const QString &source, const QString &output, bool enabled);
    Q_INVOKABLE QString message(const QVariantMap &state) const;
Q_SIGNALS:
    void changed();
private Q_SLOTS:
    void refresh();

private:
    void configure(const QString &method, const QString &source, const QString &output, bool enabled);
    QVariantMap m_snapshot;
    QString m_error;
    QTimer m_timer;
    bool m_pending = false;
};
