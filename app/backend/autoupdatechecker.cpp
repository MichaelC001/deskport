#include "autoupdatechecker.h"
#include "version.h"
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QTimer>

AutoUpdateChecker::AutoUpdateChecker(QObject *parent) : QObject(parent), m_Nam(this)
{
    m_Nam.setStrictTransportSecurityEnabled(true);
    m_Nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

QString AutoUpdateChecker::evaluate(const QByteArray& json, const QString& current,
                                    QString& version, QString& notes, QString& date, QString& url)
{
    version.clear(); notes.clear(); date.clear(); url.clear();
    const auto document = QJsonDocument::fromJson(json);
    if (!document.isObject()) return QStringLiteral("error");
    const auto release = document.object();
    if (!release.value("draft").isBool() || !release.value("prerelease").isBool() ||
        release.value("draft").toBool() || release.value("prerelease").toBool())
        return QStringLiteral("error");
    const QString tag = release.value("tag_name").toString();
    const QRegularExpression stable(QStringLiteral("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$"));
    const auto match = stable.match(tag);
    if (!match.hasMatch()) return QStringLiteral("error");
    const QString link = release.value("html_url").toString();
    if (link != QStringLiteral("https://github.com/keithxc/deskport/releases/tag/") + tag)
        return QStringLiteral("error");
    const auto latest = QVersionNumber::fromString(match.captured(1));
    const auto installed = QVersionNumber::fromString(current);
    if (latest.isNull() || installed.isNull()) return QStringLiteral("error");
    version = match.captured(1);
    notes = release.value("body").toString();
    date = release.value("published_at").toString();
    url = link;
    // Development builds at the same numeric version must never be downgraded.
    return QVersionNumber::compare(latest, installed) > 0
        ? QStringLiteral("available") : QStringLiteral("current");
}

void AutoUpdateChecker::start()
{
    if (m_Status == QStringLiteral("checking")) return;
    m_Status = QStringLiteral("checking");
    emit changed();
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/keithxc/deskport/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "DeskPort/" VERSION_STR);
    auto *reply = m_Nam.get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(15000);
    // Bound memory use even if a server returns an unexpectedly large response.
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_Status = QStringLiteral("error");
        m_Version.clear(); m_Notes.clear(); m_Date.clear(); m_Url.clear();
        if (reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
            m_Status = evaluate(reply->readAll(), QStringLiteral(VERSION_STR),
                                m_Version, m_Notes, m_Date, m_Url);
        reply->deleteLater();
        emit changed();
    });
}
