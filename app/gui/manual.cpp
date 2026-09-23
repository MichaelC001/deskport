#include "manual.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// The requested language, then another variant of the same language (Traditional
// Chinese uses Simplified before English), then English.
static QString localized(const QJsonObject& entry, const QString& language)
{
    if (entry.contains(language)) return entry[language].toString();
    const QString base = language.section('-', 0, 0);
    if (entry.contains(base)) return entry[base].toString();
    QStringList keys = entry.keys();
    keys.sort();
    for (const QString& key : keys)
        if (key.startsWith(base)) return entry[key].toString();
    return entry["en"].toString();
}

QVariantList Manual::chapters(const QString& language)
{
    QFile file(":/manual/manual.json");
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "The bundled manual is missing";
        return {};
    }
    const QJsonObject manual = QJsonDocument::fromJson(file.readAll()).object();
    QVariantList result;
    for (const QJsonValue& value : manual["chapters"].toArray()) {
        const QJsonObject chapter = value.toObject();
        QStringList steps;
        for (const QJsonValue& step : chapter["steps"].toArray())
            steps.append(localized(step.toObject()["text"].toObject(), language));
        result.append(QVariantMap{{"title", localized(chapter["title"].toObject(), language)},
                                  {"steps", steps}});
    }
    return result;
}
