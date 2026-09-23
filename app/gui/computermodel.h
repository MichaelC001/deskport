#include "backend/computermanager.h"
#include "gui/hostlayout.h"
#include "streaming/session.h"

#include <QAbstractListModel>

class ComputerModel : public QAbstractListModel
{
    Q_OBJECT
    // Empty for the top level; otherwise the ID of the group being shown.
    Q_PROPERTY(QString currentGroup READ currentGroup WRITE setCurrentGroup NOTIFY currentGroupChanged)

    enum Roles
    {
        NameRole = Qt::UserRole,
        OnlineRole,
        PairedRole,
        BusyRole,
        WakeableRole,
        StatusUnknownRole,
        ServerSupportedRole,
        AddressRole,
        HostAddressRole,
        HostIdRole,
        SourceIndexRole,
        OperatingSystemRole,
        DetailsRole,
        AliasRole,
        ReportedNameRole,
        IsGroupRole,
        GroupIdRole,
        MemberCountRole,
        MemberSystemsRole,
        IsAddRole
    };

public:
    explicit ComputerModel(QObject* object = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager);

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

    // Re-reads the devices and groups in the saved local layout.
    Q_INVOKABLE void refreshFavorites();
    // Moves one card within the shown level and saves the layout locally.
    Q_INVOKABLE void moveComputer(int from, int to);
    // Drops a device card on a device or group card at the top level.
    Q_INVOKABLE QString combine(int from, int to);
    Q_INVOKABLE QString addGroup(QString name);
    Q_INVOKABLE void renameGroup(QString groupId, QString name);
    // The group's devices return to the top level, where the group was.
    Q_INVOKABLE void deleteGroup(QString groupId);
    Q_INVOKABLE void moveOutOfGroup(int computerIndex);
    Q_INVOKABLE QString groupName(QString groupId) const;
    Q_INVOKABLE QString defaultGroupName() const;
    Q_INVOKABLE bool canEdit() const;

    QString currentGroup() const { return m_Group; }
    void setCurrentGroup(const QString& group);

    Q_INVOKABLE void deleteComputer(int computerIndex);

    Q_INVOKABLE QString generatePinString();

    Q_INVOKABLE void pairComputer(int computerIndex, QString pin);

    Q_INVOKABLE void testConnectionForComputer(int computerIndex);

    Q_INVOKABLE void wakeComputer(int computerIndex);

    // An empty alias restores the name reported by the host.
    Q_INVOKABLE void setAlias(int computerIndex, QString alias);

    Q_INVOKABLE Session* createSessionForCurrentGame(int computerIndex);

signals:
    void currentGroupChanged();
    void pairingCompleted(QVariant error);
    void connectionTestCompleted(int result, QString blockedPorts);

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handlePairingCompleted(NvComputer* computer, QString error);

private:
    struct Row
    {
        NvComputer* computer = nullptr; // null for a group or the add card
        bool add = false;               // the trailing add card at the top level
        HostLayout::Entry entry;
        QStringList systems;            // group member operating systems, for the card icons
    };
    // Present device IDs, ordered by display name for devices the layout does not mention.
    QStringList presentDevices(QHash<QString, NvComputer*>* byId = nullptr) const;
    QVector<Row> buildRows() const;
    void reload();
    static bool sameStructure(const QVector<Row>& a, const QVector<Row>& b);
    NvComputer* computerAt(int index) const;
    QVector<Row> m_Rows;
    QString m_Group;
    ComputerManager* m_ComputerManager;
};
