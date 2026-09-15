#include "clipboardsync.h"
#include "backend/clipboardprotocol.h"
#include <SDL.h>

ClipboardSync::ClipboardSync(std::unique_ptr<ClipboardChannel> channel) : m_Channel(std::move(channel)) {}
void ClipboardSync::notice(const QString& text) {
    m_Status = text;
    m_NoticeAt = SDL_GetTicks();
}
void ClipboardSync::tick() {
    if (!m_Channel) return;
    if (SDL_GetTicks() - m_LastPoll < 250) return;
    m_LastPoll = SDL_GetTicks();
    if (!m_Status.isEmpty() && m_LastPoll - m_NoticeAt >= 5000) m_Status.clear();
    if (!m_Channel->error().isEmpty()) { m_Status = m_Channel->error(); return; }
    if (!m_Channel->ready()) return;
    if (m_Channel->nativeSharing()) {
        const auto message = m_Channel->notice();
        if (message != m_LastNativeNotice) { m_LastNativeNotice = message; notice(message); }
        return;
    }
    // SDL owns the native clipboard connection while the streaming loop owns the
    // main thread. Never access the Qt clipboard from the network worker.
    QString current = m_Observed;
    bool skipped = false;
    if (!m_Initialized || m_Changed) {
        // A non-text offer may make GetClipboardText fail. Never let that skip
        // draining replies/heartbeats and eventually expire the whole channel.
        skipped = !SDL_HasClipboardText();
        if (!skipped) {
            char* raw = SDL_GetClipboardText();
            if (raw) { current = QString::fromUtf8(raw); SDL_free(raw); }
            else {
                skipped = true;
                notice(QStringLiteral("This clipboard copy could not be read and was skipped. Text sharing remains active."));
            }
        } else if (m_Changed) {
            notice(QStringLiteral("This non-text copy was skipped. Text clipboard sharing remains active."));
        }
        if (skipped) current.clear();
    }
    if (!m_Initialized) { m_Observed = current; m_Initialized = true; m_Changed = false; }
    if (current != m_Observed) { m_Observed = current; m_Dirty = true; ++m_LocalGeneration; }
    if (skipped) {
        ++m_LocalGeneration;
        m_Dirty = false;
    }
    m_Changed = false;
    QJsonObject reply;
    if (m_Channel->take(reply)) {
        m_InFlight = false;
        if (reply["rev"].toInt(-1) < m_Revision) {
            m_Status = QStringLiteral("Invalid clipboard revision; reconnect to resume sharing.");
            m_Channel.reset(); return;
        }
        m_Revision = reply["rev"].toInt();
        if (reply.contains("error")) notice(reply["error"].toString());
        if (reply.contains("text")) {
            const bool hasText = SDL_HasClipboardText();
            char* raw = hasText ? SDL_GetClipboardText() : nullptr;
            if (!hasText) current.clear();
            else if (!raw) {
                // Do not overwrite an unreadable native offer, but keep polling.
                ++m_LocalGeneration;
                notice(QStringLiteral("This clipboard copy could not be read and was skipped. Text sharing remains active."));
            } else { current = QString::fromUtf8(raw); SDL_free(raw); }
            if (current != m_Observed) { m_Observed = current; m_Dirty = true; ++m_LocalGeneration; }
            QString remote;
            if (!DeskPortClipboard::decode(reply["text"], remote)) {
                notice(QStringLiteral("Invalid remote clipboard text."));
            } else if (current == m_SentSnapshot && m_LocalGeneration == m_SentGeneration) {
                if (SDL_SetClipboardText(remote.toUtf8().constData()) == 0) {
                    current = m_Observed = remote; m_Dirty = false; m_Status.clear();
                } else notice(QStringLiteral("Unable to write the local clipboard."));
            }
            // A new local copy made while the request was in flight remains
            // pending and is sent against the returned host revision.
        }
    }
    if (m_InFlight) return;
    QJsonObject request{{"type", "clipboard-poll"}, {"seq", m_Sequence + 1}, {"rev", m_Revision}};
    if (m_Dirty) {
        QString encoded;
        if (!DeskPortClipboard::encode(current, encoded, m_Channel->maxText()))
            notice(QStringLiteral("This copy exceeds the %1 MiB text limit or contains NUL characters and was skipped. Text sharing remains active.").arg(m_Channel->maxText() / (1024 * 1024)));
        else if (!SDL_HasClipboardText())
            notice(QStringLiteral("This non-text copy was skipped. Text clipboard sharing remains active."));
        else { request["text"] = encoded; m_Status.clear(); }
    }
    if (m_Channel->submit(request)) {
        ++m_Sequence; m_SentSnapshot = current; m_SentGeneration = m_LocalGeneration; m_Dirty = false; m_InFlight = true;
    }
}
