// Link against the real desktop objects, with an isolated application identity.
#include "backend/computermanager.h"
#include "backend/hostliststate.h"
#include "backend/hostalias.h"
#include "gui/computermodel.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QFile>
#include <QProcess>
#include <cstdio>
#include <cstdlib>
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    QCoreApplication::setOrganizationName("DeskPortRemovalTest");
    QCoreApplication::setApplicationName("ProductionManager");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    if (argc != 3) {
        QTemporaryDir dir;
        for (const auto action : {"remove","restart","readd"})
            check(QProcess::execute(QCoreApplication::applicationFilePath(),{action,dir.path()})==0,action);
        std::puts("PASS: real manager and two models; offline removal, stale callbacks, settings cleanup and restart/rebind");
        return 0;
    }
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,argv[2]);
    QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,argv[2]);
    auto prefs=StreamingPreferences::get(); prefs->enableMdns=false;
    QFile cert(qEnvironmentVariable("TEST_CERT_A")); check(cert.open(QIODevice::ReadOnly),"test certificate");
    QVariantMap peer{{"hostId","fixture-device"},{"name","Offline fixture"},{"address","192.0.2.1"},{"hostPort",48989},{"hostCert",QString::fromUtf8(cert.readAll())}};
    ComputerManager manager(prefs);
    ComputerModel first,second; first.initialize(&manager); second.initialize(&manager);
    const QString action=argv[1];
    if(action=="remove") {
        check(manager.addBoundHost(peer,true),"seed offline host");
        check(manager.getComputers().size()==1,"host imported");
        auto host=manager.getComputers().first();
        HostAlias::set("fixture-device","custom alias");
        auto device=prefs->forDevice("fixture-device"); device->fps=30; device->save(); delete device;
        // A viewer can retain an older registry snapshot while the shell removes
        // a device. It must not clear the tombstone or persist that snapshot.
        ComputerManager viewer(prefs, true);
        check(viewer.addBoundHost(peer,false,true),"worker imports an in-memory binding before a settings flush");
        check(viewer.getComputers().size()==1,"worker snapshot loaded");
        manager.deleteHostById("fixture-device");
        check(!viewer.addBoundHost(peer,true),"viewer cannot write bindings or clear deletion tombstones");
        check(manager.getComputers().isEmpty(),"manager detached host synchronously");
        for(auto model:{&first,&second}) for(int i=0;i<model->rowCount({});++i)
            check(model->data(model->index(i),model->roleNames().key("hostId")).toString()!="fixture-device","every model detached retired host");
        QThreadPool::globalInstance()->waitForDone();
        check(QMetaObject::invokeMethod(&manager,"handleComputerStateChanged",Qt::DirectConnection,Q_ARG(NvComputer*,host)),"late callback delivered");
        check(manager.getComputers().isEmpty(),"late callback cannot resurrect freed host");
        check(HostAlias::get("fixture-device").isEmpty(),"alias deleted");
        QSettings settings;
        for(const auto& key:settings.allKeys()) check(!key.startsWith("devices/"),"device profile deleted");
        check(!manager.addBoundHost(peer),"background binding replay blocked immediately");
    } else {
        check(manager.getComputers().isEmpty(),"restart stays empty");
        check(!manager.addBoundHost(peer),"offline startup binding replay blocked");
        if(action=="readd") check(manager.addBoundHost(peer,true),"explicit new binding allowed");
    }
    QThreadPool::globalInstance()->waitForDone();
    return 0;
}
