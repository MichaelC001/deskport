#pragma once
#include <QObject>
#include <QVariantList>

// The user manual bundled with the application: the shared core file, embedded as
// a resource. Nothing is fetched at runtime, so it always matches this build.
class Manual : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // Chapters in file order, with one language already chosen for every string.
    Q_INVOKABLE QVariantList chapters(const QString& language);
};
