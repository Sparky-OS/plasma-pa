/*
    SPDX-FileCopyrightText: 2026 Daniel Campos Ramos
    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "soundformatconf.h"

#include <QJsonDocument>
#include <QObject>

#include <KAuth/ActionReply>
#include <KAuth/HelperSupport>

using namespace Qt::StringLiterals;

/**
 * Writes the sound format settings that apply to every account, as /etc drop-ins for PipeWire and WirePlumber.
 * It accepts one thing: a state map that SoundFormatConf::validate approves. It builds the file text itself and never
 * writes a path or text it was given, and it removes only files that carry the Sound page's mark.
 */
class SoundFormatsHelper : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    KAuth::ActionReply write(const QVariantMap &args)
    {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(args.value(u"state"_s).toString().toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            return failure(u"The settings were not readable."_s);
        }
        QString error;
        if (!SoundFormatConf::write(u"/etc"_s, doc.object().toVariantMap(), &error)) {
            return failure(error);
        }
        return KAuth::ActionReply::SuccessReply();
    }

private:
    static KAuth::ActionReply failure(const QString &text)
    {
        auto reply = KAuth::ActionReply::HelperErrorReply();
        reply.setErrorDescription(text);
        return reply;
    }
};

KAUTH_HELPER_MAIN("org.kde.plasma.pa.soundformats", SoundFormatsHelper)

#include "soundformatshelper.moc"
