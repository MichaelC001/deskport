#include "hostlayout.h"

#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QUuid>
#include <QVariantMap>

namespace {
QString key(const QString& id) { return id.trimmed().toLower(); }
}

QString HostLayout::normalizeName(const QString& value)
{
    QString text = value.simplified();
    text.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    return text.left(64);
}

HostLayout::HostLayout(const QVariantList& stored, bool hasStored, const QStringList& legacyOrder, Saver save)
    : m_Save(std::move(save))
{
    QSet<QString> seen;
    if (!hasStored) {
        for (const QString& id : legacyOrder) {
            const QString device = key(id);
            if (!device.isEmpty() && !seen.contains(device)) { seen.insert(device); m_Items.append({false, device, {}, {}}); }
        }
        return;
    }
    for (const QVariant& value : stored) {
        const QVariantMap item = value.toMap();
        const QString type = item.value("type").toString(), id = item.value("id").toString();
        if (id.isEmpty()) continue;
        if (type == "device") {
            const QString device = key(id);
            if (!seen.contains(device)) { seen.insert(device); m_Items.append({false, device, {}, {}}); }
        } else if (type == "group" && !seen.contains(id)) {
            seen.insert(id);
            QStringList members;
            for (const QVariant& member : item.value("devices").toList()) {
                const QString device = member.typeId() == QMetaType::QString ? key(member.toString()) : QString();
                if (!device.isEmpty() && !seen.contains(device)) { seen.insert(device); members.append(device); }
            }
            const QVariant name = item.value("name");
            m_Items.append({true, id, name.typeId() == QMetaType::QString ? normalizeName(name.toString()) : QString(), members});
        }
    }
}

HostLayout HostLayout::load()
{
    QSettings settings;
    // Pinned devices from earlier versions start the order when nothing else was saved.
    const QStringList legacy = settings.contains("ui/hostOrder") ? settings.value("ui/hostOrder").toStringList()
                                                                 : settings.value("ui/favorites").toStringList();
    return HostLayout(settings.value("ui/hostLayout").toList(), settings.contains("ui/hostLayout"), legacy,
                      [](const QVariantList& items) {
                          QSettings settings;
                          settings.setValue("ui/hostLayout", items);
                          settings.remove("ui/hostOrder");
                      });
}

void HostLayout::save()
{
    QVariantList items;
    for (const Item& item : m_Items) {
        if (item.group) items.append(QVariantMap{{"type", "group"}, {"id", item.id}, {"name", item.name}, {"devices", item.devices}});
        else items.append(QVariantMap{{"type", "device"}, {"id", item.id}});
    }
    if (m_Save) m_Save(items);
}

int HostLayout::indexOf(const QString& id) const
{
    for (int i = 0; i < m_Items.size(); ++i) if (m_Items[i].id == id) return i;
    return -1;
}

HostLayout::Item* HostLayout::groupItem(const QString& group)
{
    for (Item& item : m_Items) if (item.group && item.id == group) return &item;
    return nullptr;
}

const HostLayout::Item* HostLayout::groupItem(const QString& group) const
{
    for (const Item& item : m_Items) if (item.group && item.id == group) return &item;
    return nullptr;
}

bool HostLayout::mentions(const QString& device) const
{
    for (const Item& item : m_Items) if (item.group ? item.devices.contains(device) : item.id == device) return true;
    return false;
}

// Unsaved present devices become top-level items, so every card has a slot.
void HostLayout::adopt(const QStringList& devices)
{
    for (const QString& id : devices) {
        const QString device = key(id);
        if (!device.isEmpty() && !mentions(device)) m_Items.append({false, device, {}, {}});
    }
}

QVector<HostLayout::Entry> HostLayout::entries(const QStringList& devices, const QString& group) const
{
    QSet<QString> present;
    for (const QString& id : devices) present.insert(key(id));
    QVector<Entry> entries;
    if (!group.isEmpty()) {
        if (const Item* item = groupItem(group))
            for (const QString& device : item->devices) if (present.contains(device)) entries.append({false, device, {}, {}});
        return entries;
    }
    QSet<QString> listed;
    for (const Item& item : m_Items) {
        if (item.group) {
            QStringList members;
            for (const QString& device : item.devices) { listed.insert(device); if (present.contains(device)) members.append(device); }
            entries.append({true, item.id, item.name, members});
        } else {
            listed.insert(item.id);
            if (present.contains(item.id)) entries.append({false, item.id, {}, {}});
        }
    }
    for (const QString& id : devices) {
        const QString device = key(id);
        if (!listed.contains(device)) { listed.insert(device); entries.append({false, device, {}, {}}); }
    }
    return entries;
}

QStringList HostLayout::flattened(const QStringList& devices) const
{
    QStringList order;
    for (const Entry& entry : entries(devices)) {
        if (entry.group) order.append(entry.devices); else order.append(entry.id);
    }
    return order;
}

QString HostLayout::groupOf(const QString& device) const
{
    const QString id = key(device);
    for (const Item& item : m_Items) if (item.group && item.devices.contains(id)) return item.id;
    return {};
}

QString HostLayout::nameOf(const QString& group) const
{
    const Item* item = groupItem(group);
    return item ? (item->name.isNull() ? QStringLiteral("") : item->name) : QString();
}

bool HostLayout::hasGroups() const
{
    for (const Item& item : m_Items) if (item.group) return true;
    return false;
}

QStringList HostLayout::groupNames() const
{
    QStringList names;
    for (const Item& item : m_Items) if (item.group) names.append(item.name);
    return names;
}

void HostLayout::arrange(const QStringList& shownOrder, const QStringList& devices, const QString& group)
{
    adopt(devices);
    QStringList shown;
    for (const Entry& entry : entries(devices, group)) shown.append(entry.id);
    if (QSet<QString>(shown.begin(), shown.end()) != QSet<QString>(shownOrder.begin(), shownOrder.end()) || shown == shownOrder) return;
    int next = 0;
    if (!group.isEmpty()) {
        Item* item = groupItem(group);
        for (QString& member : item->devices) if (shown.contains(member)) member = shownOrder[next++];
    } else {
        QVector<Item> reordered;
        for (const Item& item : m_Items) reordered.append(shown.contains(item.id) ? m_Items[indexOf(shownOrder[next++])] : item);
        m_Items = reordered;
    }
    save();
}

void HostLayout::move(int from, int to, const QStringList& devices, const QString& group)
{
    QStringList shown;
    for (const Entry& entry : entries(devices, group)) shown.append(entry.id);
    if (from < 0 || to < 0 || from >= shown.size() || to >= shown.size() || from == to) return;
    shown.move(from, to);
    arrange(shown, devices, group);
}

void HostLayout::detach(const QString& device)
{
    for (int i = m_Items.size() - 1; i >= 0; --i) {
        if (m_Items[i].group) m_Items[i].devices.removeAll(device);
        else if (m_Items[i].id == device) m_Items.removeAt(i);
    }
}

QString HostLayout::combine(const QString& deviceId, const QString& targetId, const QStringList& devices, const QString& defaultName)
{
    const QString device = key(deviceId);
    if (device.isEmpty() || device == key(targetId)) return {};
    adopt(devices);
    if (Item* group = groupItem(targetId)) {
        const QString id = group->id;
        detach(device);
        groupItem(id)->devices.append(device);
        save();
        return id;
    }
    const QString target = key(targetId);
    if (indexOf(target) < 0) return {};
    detach(device);
    const QString id = "group-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_Items[indexOf(target)] = {true, id, normalizeName(defaultName), {target, device}};
    save();
    return id;
}

QString HostLayout::addGroup(const QString& name, const QStringList& devices)
{
    adopt(devices);
    const QString id = "group-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_Items.append({true, id, normalizeName(name), {}});
    save();
    return id;
}

void HostLayout::rename(const QString& group, const QString& name)
{
    const QString text = normalizeName(name);
    Item* item = groupItem(group);
    if (!item || text.isEmpty()) return;
    item->name = text;
    save();
}

void HostLayout::deleteGroup(const QString& group)
{
    const int index = indexOf(group);
    if (index < 0 || !m_Items[index].group) return;
    const QStringList members = m_Items.takeAt(index).devices;
    for (int i = members.size() - 1; i >= 0; --i) m_Items.insert(index, {false, members[i], {}, {}});
    save();
}

void HostLayout::moveOut(const QString& deviceId)
{
    const QString device = key(deviceId), group = groupOf(device);
    if (group.isEmpty()) return;
    detach(device);
    m_Items.insert(indexOf(group) + 1, {false, device, {}, {}});
    save();
}

void HostLayout::forget(const QString& device)
{
    detach(key(device));
    save();
}
