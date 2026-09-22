#include "computermodel.h"
#include "backend/hostalias.h"

#include <QThreadPool>
#include <QSet>
#include <QSettings>
#include <algorithm>

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);

    m_Rows = buildRows();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Rows.count());

    const Row& row = m_Rows[index.row()];
    if (row.add) {
        switch (role) {
        case IsAddRole: return true;
        case IsGroupRole: case OnlineRole: case PairedRole: case BusyRole: case WakeableRole: case StatusUnknownRole: case ServerSupportedRole: return false;
        case SourceIndexRole: return -1;
        case MemberCountRole: return 0;
        case MemberSystemsRole: return QStringList();
        default: return QString();
        }
    }
    if (!row.computer) {
        switch (role) {
        case IsAddRole: return false;
        case IsGroupRole: return true;
        case GroupIdRole: return row.entry.id;
        case NameRole: return row.entry.name.isEmpty() ? tr("Group") : row.entry.name;
        case MemberCountRole: return int(row.entry.devices.size());
        case MemberSystemsRole: return row.systems;
        case SourceIndexRole: return -1;
        case OnlineRole: case PairedRole: case BusyRole: case WakeableRole: case StatusUnknownRole: case ServerSupportedRole: return false;
        default: return QString();
        }
    }
    NvComputer* computer = row.computer;
    // getComputers() takes manager and host locks. Resolve the source index
    // before taking this host lock to avoid recursive locking / lock inversion.
    if (role == SourceIndexRole) return m_ComputerManager->getComputers().indexOf(computer);
    QReadLocker lock(&computer->lock);

    switch (role) {
    case IsAddRole: return false;
    case IsGroupRole: return false;
    case GroupIdRole: return QString();
    case MemberCountRole: return 0;
    case MemberSystemsRole: return QStringList();
    case OperatingSystemRole: return computer->operatingSystem.isEmpty() && computer->isNvidiaServerSoftware ? QStringLiteral("Windows") : computer->operatingSystem;
    case HostIdRole: return computer->uuid;
    case NameRole:
        return HostAlias::displayName(computer->uuid, computer->name);
    case AliasRole:
        return HostAlias::get(computer->uuid);
    case ReportedNameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case AddressRole:
    case HostAddressRole: {
        const auto address = !computer->activeAddress.isNull() ? computer->activeAddress :
            !computer->manualAddress.isNull() ? computer->manualAddress : computer->localAddress;
        return role == AddressRole ? address.toString() : address.address();
    }
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        const QString alias = HostAlias::get(computer->uuid);
        return (alias.isEmpty() ? QString() : tr("Alias: %1").arg(alias) + '\n') +
               tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Rows.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[HostIdRole] = "hostId";
    names[SourceIndexRole] = "sourceIndex";
    names[NameRole] = "name";
    names[AliasRole] = "alias";
    names[ReportedNameRole] = "reportedName";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[AddressRole] = "address";
    names[HostAddressRole] = "hostAddress";
    names[OperatingSystemRole] = "operatingSystem";
    names[DetailsRole] = "details";
    names[IsGroupRole] = "isGroup";
    names[GroupIdRole] = "groupId";
    names[MemberCountRole] = "memberCount";
    names[MemberSystemsRole] = "memberSystems";
    names[IsAddRole] = "isAdd";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    NvComputer* computer = computerAt(computerIndex);
    if (!computer) return nullptr;

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    NvComputer* computer = computerAt(computerIndex);
    if (!computer) return;

    {
        QReadLocker lock(&computer->lock);
        HostAlias::set(computer->uuid, QString());
        HostLayout::load().forget(computer->uuid);
    }

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // The computer will be deleted by this call
    m_ComputerManager->deleteHost(computer);

    // Remove the now invalid item
    m_Rows.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    NvComputer* computer = computerAt(computerIndex);
    if (!computer) return;

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(computer);
    QThreadPool::globalInstance()->start(wakeTask);
}

void ComputerModel::setAlias(int computerIndex, QString alias)
{
    NvComputer* computer = computerAt(computerIndex);
    if (!computer) return;

    QString uuid;
    {
        QReadLocker lock(&computer->lock);
        uuid = computer->uuid;
    }
    HostAlias::set(uuid, alias);
    emit dataChanged(createIndex(computerIndex, 0), createIndex(computerIndex, 0));
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    if (NvComputer* computer = computerAt(computerIndex)) m_ComputerManager->pairHost(computer, pin);
}

void ComputerModel::handlePairingCompleted(NvComputer*, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<Row> rows = buildRows();

    // Reset the model if the structural layout of the list has changed
    if (!sameStructure(m_Rows, rows)) {
        beginResetModel();
        m_Rows = rows;
        endResetModel();
    }
    else {
        m_Rows = rows;
        // Let the view know that this specific computer changed
        for (int index = 0; index < m_Rows.size(); ++index)
            if (m_Rows[index].computer == computer) emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    }
}

NvComputer* ComputerModel::computerAt(int index) const
{
    return index >= 0 && index < m_Rows.size() ? m_Rows[index].computer : nullptr;
}

bool ComputerModel::sameStructure(const QVector<Row>& a, const QVector<Row>& b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a[i].computer != b[i].computer || a[i].add != b[i].add || a[i].entry.id != b[i].entry.id || a[i].entry.name != b[i].entry.name ||
            a[i].entry.devices != b[i].entry.devices || a[i].systems != b[i].systems) return false;
    }
    return true;
}

QStringList ComputerModel::presentDevices(QHash<QString, NvComputer*>* byId) const
{
    struct Named { QString name; QString id; NvComputer* computer; };
    QVector<Named> named;
    QSet<QString> seen;
    for (NvComputer* computer : m_ComputerManager->getComputers()) {
        QReadLocker lock(&computer->lock);
        const QString id = computer->uuid.toLower();
        if (id.isEmpty() || seen.contains(id)) continue;
        seen.insert(id);
        named.append({HostAlias::displayName(computer->uuid, computer->name), id, computer});
    }
    // Devices the layout does not mention yet follow it, by name.
    std::stable_sort(named.begin(), named.end(), [](const Named& a, const Named& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QStringList present;
    for (const Named& item : named) {
        present.append(item.id);
        if (byId) byId->insert(item.id, item.computer);
    }
    return present;
}

QVector<ComputerModel::Row> ComputerModel::buildRows() const
{
    QHash<QString, NvComputer*> byId;
    const QStringList present = presentDevices(&byId);
    QVector<Row> rows;
    for (const HostLayout::Entry& entry : HostLayout::load().entries(present, m_Group)) {
        Row row;
        row.entry = entry;
        if (!entry.group) row.computer = byId.value(entry.id);
        else for (const QString& member : entry.devices.mid(0, 4)) {
            NvComputer* computer = byId.value(member);
            QReadLocker lock(&computer->lock);
            row.systems.append(computer->operatingSystem.isEmpty() && computer->isNvidiaServerSoftware ? QStringLiteral("Windows") : computer->operatingSystem);
        }
        rows.append(row);
    }
    // The add card ends the top level once there is something to arrange.
    if (m_Group.isEmpty() && !rows.isEmpty()) {
        Row add;
        add.add = true;
        rows.append(add);
    }
    return rows;
}

void ComputerModel::reload()
{
    // A deleted group closes; its devices are back at the top level.
    if (!m_Group.isEmpty() && HostLayout::load().nameOf(m_Group).isNull()) {
        m_Group.clear();
        emit currentGroupChanged();
    }
    beginResetModel();
    m_Rows = buildRows();
    endResetModel();
}

void ComputerModel::setCurrentGroup(const QString& group)
{
    if (m_Group == group) return;
    m_Group = group;
    emit currentGroupChanged();
    reload();
}

void ComputerModel::moveComputer(int from, int to)
{
    // The add card stays last and never moves.
    if (from < 0 || to < 0 || from >= m_Rows.size() || to >= m_Rows.size() || from == to || m_Rows[from].add || m_Rows[to].add) return;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
    m_Rows.move(from, to);
    endMoveRows();
    HostLayout::load().move(from, to, presentDevices(), m_Group);
}

QString ComputerModel::combine(int from, int to)
{
    // Groups hold devices only, and do not nest.
    if (!m_Group.isEmpty() || !computerAt(from) || to < 0 || to >= m_Rows.size() || from == to || m_Rows[to].add) return {};
    HostLayout layout = HostLayout::load();
    const QString group = layout.combine(m_Rows[from].entry.id, m_Rows[to].entry.id, presentDevices(), defaultGroupName());
    reload();
    return group;
}

QString ComputerModel::addGroup(QString name)
{
    HostLayout layout = HostLayout::load();
    const QString group = layout.addGroup(name.trimmed().isEmpty() ? defaultGroupName() : name, presentDevices());
    reload();
    return group;
}

void ComputerModel::renameGroup(QString groupId, QString name)
{
    HostLayout::load().rename(groupId, name);
    reload();
}

void ComputerModel::deleteGroup(QString groupId)
{
    HostLayout::load().deleteGroup(groupId);
    reload();
}

void ComputerModel::moveOutOfGroup(int computerIndex)
{
    if (!computerAt(computerIndex)) return;
    HostLayout::load().moveOut(m_Rows[computerIndex].entry.id);
    reload();
}

QString ComputerModel::groupName(QString groupId) const
{
    const QString name = HostLayout::load().nameOf(groupId);
    return name.isEmpty() ? tr("Group") : name;
}

QString ComputerModel::defaultGroupName() const
{
    const QString base = tr("Group");
    const QStringList names = HostLayout::load().groupNames();
    if (!names.contains(base)) return base;
    for (int number = 2; ; ++number) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(number);
        if (!names.contains(name)) return name;
    }
}

bool ComputerModel::canEdit() const
{
    return !m_Group.isEmpty() || presentDevices().size() > 1 || HostLayout::load().hasGroups();
}

void ComputerModel::refreshFavorites()
{
    reload();
}

#include "computermodel.moc"
