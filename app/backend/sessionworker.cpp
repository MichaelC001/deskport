#include "sessionworker.h"
#include "diagnostics.h"
#include "computermanager.h"
#include "cli/startstream.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/transitionwindow.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>
namespace {
QByteArray frame(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n'; }
void postEvent(int command) { SDL_Event event{}; event.type = SDL_USEREVENT; event.user.code = command; SDL_PushEvent(&event); }
}
SessionWorker::SessionWorker(QString endpoint, QString token, QString host, QString app)
    : m_Endpoint(endpoint),m_Token(token),m_Host(host),m_App(app) { resize(1120,760); }
void SessionWorker::send(QJsonObject message) { if (m_Socket.state()==QLocalSocket::ConnectedState && m_Socket.bytesToWrite()<16384) m_Socket.write(frame(message)); }
int SessionWorker::run() {
    QTimer traffic;
    connect(&traffic, &QTimer::timeout, this, [this] {
        if (m_Session) { auto state=QJsonObject::fromVariantMap(m_Session->traffic()); state["type"]="traffic"; send(state); }
    });
    traffic.start(1000);
    connect(&m_Socket,&QLocalSocket::connected,this,[this] {
        send({{"token",m_Token}});

    });
    connect(&m_Socket,&QLocalSocket::readyRead,this,[this] {
        m_Buffer += m_Socket.readAll();
        if (m_Buffer.size()>16384) { m_Socket.abort(); return; }
        while (m_Buffer.contains('\n')) { const auto end=m_Buffer.indexOf('\n'); auto message=QJsonDocument::fromJson(m_Buffer.left(end)).object(); m_Buffer.remove(0,end+1); command(message); }
    });
    connect(&m_Socket,&QLocalSocket::disconnected,this,[this] {
        m_Stopping=true;
        if (m_Session) { m_Session->cancelRecovery(); postEvent(DeskPortEndSession); }
        else QCoreApplication::quit();
        QTimer::singleShot(5000,qApp,[] { QCoreApplication::exit(1); });
    });
    m_Socket.setReadBufferSize(16385); m_Socket.connectToServer(m_Endpoint);
    QTimer::singleShot(10000,this,[this] { if(m_Socket.state()!=QLocalSocket::ConnectedState) QCoreApplication::exit(1); });
    const auto result=QCoreApplication::exec();
    // A forced IPC-loss exit must not free computers beneath a live SDL worker.
    if (!m_Session) delete m_Computers;
    m_Computers=nullptr;
    return result;
}
void SessionWorker::attach(Session* session) {
    m_Session=session; session->setViewerRequested(m_Visible);
    if (m_Stopping) session->cancelRecovery();
    send({{"type","connecting"}});
    connect(session,&Session::connectionStarted,this,[this] { send({{"type","connected"}}); });
    connect(session,&Session::viewerReadyChanged,this,[this,session] {
        if (session->viewerReady()) { send({{"type","ready"}}); if(m_Visible) send({{"type","shown"}}); }
    });
    connect(session,&Session::presentationShown,this,[this] { if(m_Visible)send({{"type","shown"}}); });
    connect(session,&Session::presentationHidden,this,[this] { send({{"type","hidden"},{"epoch",double(m_HideEpoch)}}); });
    connect(session,&Session::stageFailed,this,[this](QString stage,int code,QString) {
        send({{"type","error"},{"message",tr("Connection failed at %1 (%2).").arg(stage).arg(code)}});
    });
    connect(session,&Session::displayLaunchError,this,[this](QString error) { send({{"type","error"},{"message",error}}); });
    connect(session,&Session::readyForDeletion,this,[this,session] {
        m_Session=nullptr;
        if (!m_Stopping && session->adaptiveRestartPending()) {
            auto next=session->adaptiveContinuation(); QTimer::singleShot(next->retryDelay(),this,[this,next] { attach(next); });
        } else finish();
    });
    QTimer::singleShot(0,this,[this,session] { session->exec(this); });
}
void SessionWorker::command(const QJsonObject& message) {
    const auto action=message.value("command").toString();
    if (action=="initialize" && !m_Computers && !m_Stopping) {
        m_Computers = new ComputerManager(StreamingPreferences::get(), true, m_Host);
        const auto peer=message.value("peer").toObject();
        if (!peer.isEmpty() && (peer.value("hostId").toString().compare(m_Host,Qt::CaseInsensitive) != 0 ||
            !m_Computers->addBoundHost(peer.toVariantMap(),false,true))) { finish(tr("The device binding is unavailable.")); return; }
        auto launcher = new CliStartStream::Launcher(m_Host,m_App,nullptr,this);
        connect(launcher,&CliStartStream::Launcher::sessionCreated,this,[this](QString,Session* session) { attach(session); });
        connect(launcher,&CliStartStream::Launcher::failed,this,[this](QString error) { finish(error); });
        connect(launcher,&CliStartStream::Launcher::appQuitRequired,this,[this](QString) { finish(tr("Another application is running on that server.")); });
        launcher->execute(m_Computers);
    } else if (action=="diagnostics" && message.value("enabled").isBool()) {
        Diagnostics::instance().setEnabled(message.value("enabled").toBool());
    } else if (action=="show") {
        m_Visible=true;
        if(m_Session) { m_Session->setViewerRequested(true); if(m_Session->viewerReady()) { postEvent(DeskPortRecallWindow); } }
    } else if(action=="hide") {
        m_Visible=false; m_HideEpoch=quint64(message.value("epoch").toDouble());
        if(m_Session) { m_Session->setViewerRequested(false); postEvent(DeskPortHideWindow); }
        if(!m_Session || !m_Session->inputDispatching()) send({{"type","hidden"},{"epoch",double(m_HideEpoch)}});
    } else if(action=="disconnect") {
        m_Stopping=true; if(m_Session) {m_Session->cancelRecovery();postEvent(DeskPortEndSession);} else finish();
    } else if(action=="reconnect" && m_Session) m_Session->requestReconnect();
    else if(action=="fullscreen" && m_Session && m_Visible) postEvent(DeskPortFullscreen);
}
void SessionWorker::showDevicesDuringSession() { m_Visible=false; if(m_Session)m_Session->setViewerRequested(false); send({{"type","devices"}}); }
void SessionWorker::prepareViewerRecall() { send({{"type","devices"}}); }
void SessionWorker::finish(const QString& error) {
    if(m_Finished)return; m_Finished=true;
    if(!error.isEmpty())send({{"type","error"},{"message",error}});
    m_Socket.flush(); QTimer::singleShot(0,qApp,&QCoreApplication::quit);
}
