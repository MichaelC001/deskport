// Device group and order rules; the mobile clients check the same cases.
#include "hostlayout.h"

#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static QStringList ids(const QVector<HostLayout::Entry>& entries)
{
    QStringList result;
    for (const auto& entry : entries) result.append(entry.id);
    return result;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QVariantList stored;
    bool saved = false;
    auto saver = [&](const QVariantList& items) { stored = items; saved = true; };
    const QStringList present{"a", "b", "c", "d"};

    HostLayout layout({}, false, {"B", "A"}, saver);
    check(ids(layout.entries(present)) == QStringList({"b", "a", "c", "d"}), "migrated order");
    const QString group = layout.combine("C", "a", present, "  Office \n");
    auto top = layout.entries(present);
    check(ids(top) == QStringList({"b", group, "d"}), "device on device groups at target place");
    check(top[1].group && top[1].name == "Office" && top[1].devices == QStringList({"a", "c"}), "group contents");
    check(layout.groupOf("C") == group, "group of");
    check(layout.combine("d", group, present, "x") == group, "device on group");
    check(layout.combine("b", "B", present, "x").isEmpty(), "no self group");
    check(ids(layout.entries(present, group)) == QStringList({"a", "c", "d"}), "members");
    layout.move(2, 0, present, group);
    layout.move(1, 0, present);
    check(saved, "changes are saved");

    HostLayout reloaded(stored, true, {}, saver);
    check(ids(reloaded.entries(present)) == QStringList({group, "b"}), "reload top");
    check(ids(reloaded.entries(present, group)) == QStringList({"d", "a", "c"}), "reload group");
    check(reloaded.flattened(present) == QStringList({"d", "a", "c", "b"}), "flattened");
    reloaded.move(0, 1, {"a", "b", "c"}, group);
    check(ids(reloaded.entries(present, group)) == QStringList({"d", "c", "a"}), "missing device keeps slot");
    reloaded.moveOut("c");
    check(ids(reloaded.entries(present)) == QStringList({group, "c", "b"}), "move out after group");
    reloaded.rename(group, "   ");
    reloaded.rename(group, "Lab");
    check(reloaded.nameOf(group) == "Lab", "rename");
    const QString empty = reloaded.addGroup("Spare", present);
    check(reloaded.entries(present).last().id == empty, "empty group kept at end");
    reloaded.deleteGroup(group);
    check(ids(reloaded.entries(present)) == QStringList({"d", "a", "c", "b", empty}), "delete in place");
    reloaded.deleteGroup(empty);
    check(!reloaded.hasGroups() && reloaded.nameOf(empty).isNull(), "no groups");
    reloaded.forget("A");
    check(ids(reloaded.entries({"b", "c", "d"})) == QStringList({"d", "c", "b"}), "forget");

    const QVariantList malformed{QVariantMap{{"type", "device"}, {"id", "x"}}, QString("junk"),
                                 QVariantMap{{"type", "group"}, {"id", "g"}, {"name", 5}, {"devices", QVariantList{"x", "y", 3}}},
                                 QVariantMap{{"type", "group"}, {"id", "g"}, {"name", "dup"}}};
    const auto loaded = HostLayout(malformed, true, {}, {}).entries({"x", "y"});
    check(ids(loaded) == QStringList({"x", "g"}) && loaded[1].devices == QStringList({"y"}) && loaded[1].name.isEmpty(), "malformed storage");
    check(HostLayout::normalizeName(QString(100, 'x')).size() == 64, "name bound");
    std::puts("PASS: desktop device groups match the mobile clients: combine, reorder, move out, rename, delete in place, migrate, reload");
    return 0;
}
