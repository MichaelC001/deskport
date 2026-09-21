#pragma once
#include <QObject>
#include <QNetworkAccessManager>

class AutoUpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status MEMBER m_Status NOTIFY changed)
    Q_PROPERTY(QString latestVersion MEMBER m_Version NOTIFY changed)
    Q_PROPERTY(QString releaseNotes MEMBER m_Notes NOTIFY changed)
    Q_PROPERTY(QString publishedAt MEMBER m_Date NOTIFY changed)
    Q_PROPERTY(QString releaseUrl MEMBER m_Url NOTIFY changed)
public:
    explicit AutoUpdateChecker(QObject *parent = nullptr);
    Q_INVOKABLE void start();
    // Pure parser shared with isolated release-channel tests.
    static QString evaluate(const QByteArray& json, const QString& current,
                            QString& version, QString& notes, QString& date, QString& url);
signals:
    void changed();
private:
    QString m_Status = QStringLiteral("idle");
    QString m_Version, m_Notes, m_Date, m_Url;
    QNetworkAccessManager m_Nam;
};
