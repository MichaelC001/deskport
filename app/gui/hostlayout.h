#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <functional>

// The local arrangement of devices and groups, like folders on a phone home
// screen. It is stored on this computer only and never sent to a host; the
// mobile clients follow the same rules. Devices it does not mention follow the
// saved entries, in the order they are passed in.
class HostLayout
{
public:
    struct Entry
    {
        bool group = false;
        QString id;          // lower-case device ID, or the group ID
        QString name;        // group name
        QStringList devices; // present member device IDs, in order
    };

    using Saver = std::function<void(const QVariantList&)>;

    // Items are maps: {"type": "device"|"group", "id", "name", "devices"}.
    // Without stored items, the earlier flat order is migrated.
    HostLayout(const QVariantList& stored, bool hasStored, const QStringList& legacyOrder, Saver save);
    // The layout saved in QSettings under "ui/hostLayout".
    static HostLayout load();

    static QString normalizeName(const QString& value);

    // A null or empty group lists the top level; a group lists its members.
    QVector<Entry> entries(const QStringList& devices, const QString& group = QString()) const;
    // Devices in display order, with each group's members at the group's place.
    QStringList flattened(const QStringList& devices) const;
    QString groupOf(const QString& device) const;
    // A null string when the group does not exist.
    QString nameOf(const QString& group) const;
    bool hasGroups() const;
    QStringList groupNames() const;

    void move(int from, int to, const QStringList& devices, const QString& group = QString());
    // Applies the shown order of one level; devices that are not present keep their slots.
    void arrange(const QStringList& shownOrder, const QStringList& devices, const QString& group = QString());
    // Dropping a device on a device makes a group of both, at the target's place;
    // dropping it on a group adds it to the end of that group. Returns the group ID.
    QString combine(const QString& device, const QString& target, const QStringList& devices, const QString& defaultName);
    QString addGroup(const QString& name, const QStringList& devices);
    void rename(const QString& group, const QString& name);
    // The group's devices return to the top level, where the group was.
    void deleteGroup(const QString& group);
    // The device returns to the top level, just after its group.
    void moveOut(const QString& device);
    void forget(const QString& device);

private:
    struct Item { bool group; QString id; QString name; QStringList devices; };
    QVector<Item> m_Items;
    Saver m_Save;

    int indexOf(const QString& id) const;
    Item* groupItem(const QString& group);
    const Item* groupItem(const QString& group) const;
    bool mentions(const QString& device) const;
    void adopt(const QStringList& devices);
    void detach(const QString& device);
    void save();
};
