#pragma once
#include <QCoreApplication>
#include <QString>

// Presentation of existing protocol results and local preparation failures only.
// Never display arbitrary peer error text or interpret an unknown code as denial.
namespace DeskPortSessionFailure {
inline QString message(const QString& code) {
    const auto tr = [](const char* text) { return QCoreApplication::translate("Session", text); };
    if (code == "cancelled") return tr("Connection cancelled. Reconnect when ready.");
    if (code == "unauthorized") return tr("Session access was denied. Review saved access on the host and reconnect.");
    if (code == "busy") return tr("The host is busy with another session. Try again after it disconnects.");
    if (code == "stale") return tr("Session confirmation expired or the host state changed. Reconnect to try again.");
    if (code == "cycle") return tr("This connection would create a loop. Disconnect one of the existing links first.");
    if (code.startsWith("topology-")) return tr("The connection path could not be verified. Update DeskPort on every desktop in the chain and try again.");
    if (code == "display-failed") return tr("The host could not prepare the virtual screen. Check host display diagnostics and reconnect.");
    if (code == "policy-unsupported") return tr("The selected virtual screen mode is unavailable. Update the host or choose another mode.");
    if (code == "capture-encoder-failed") return tr("The host could not initialize video capture or encoding. Check screen recording permission and encoder availability.");
    if (code == "timeout") return tr("Host session preparation timed out. Check that sharing is available and reconnect.");
    return tr("The host could not prepare this session. Check that sharing is running and review host diagnostics, then reconnect.");
}
}
