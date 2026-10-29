// The worker-control suite exercises real processes and authenticated sockets,
// but deliberately excludes GPU setup. Native --canvas-self-test covers the
// production decoder/presentation implementation on each supported platform.
#include "gui/sessionpresentation.h"
struct SessionPresentation::Impl {};
SessionPresentation::SessionPresentation(QObject* parent) : QObject(parent) {}
SessionPresentation::~SessionPresentation() = default;
bool SessionPresentation::ready() const { return false; }
bool SessionPresentation::active() const { return false; }
bool SessionPresentation::hasFrames() const { return false; }
QString SessionPresentation::backdrop() const { return {}; }
QImage SessionPresentation::backdropImage() const { return {}; }
bool SessionPresentation::configure(const QJsonObject&) { return true; }
void SessionPresentation::frame(QByteArray) {}
void SessionPresentation::status(const QJsonObject&) {}
void SessionPresentation::overlays(const QJsonObject&) {}
void SessionPresentation::preedit(const QString&) {}
void SessionPresentation::attach(QWindow*) {}
void SessionPresentation::detach() {}
void SessionPresentation::resized(bool) {}
QJsonObject SessionPresentation::capabilities() const { return {{"ok",true}}; }
