#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <functional>
#include "shortcutbinding.h"

// Native adapters report edges; the shell owns the action and its lifetime.
// Only the resident GUI process constructs a backend.
class GlobalShortcutBackend : public QObject {
public:
    using QObject::QObject;
    virtual bool start() = 0;
    virtual void stop() = 0;
    std::function<void(bool, const QByteArray&)> edge;
    std::function<void(QString)> error;
    std::function<void(bool)> availability;
    ShortcutBinding binding = ShortcutBinding::parse(ShortcutBinding::defaultSequence());
    QString triggerDescription;
    // Portals may let users change the binding. Never keep intercepting the
    // requested SDL chord if the compositor assigned a different shortcut.
    bool fixedChord = true;
};

GlobalShortcutBackend* createGlobalShortcutBackend(QObject* parent);

// A complete native press/release is authoritative when it overlaps the SDL
// fallback. Cross-source delivery can be reordered by the two event loops;
// absorb that duplicate without rate-limiting consecutive native presses.
class ShortcutActivationGate {
public:
    bool nativeEdge(bool pressed, qint64 now) {
        if (pressed) {
            if (!m_NativeDown) {
                m_NativeDown = true;
                m_FallbackDuringHold = false;
                m_SuppressNative = m_LastFallback >= 0 && now - m_LastFallback < 250;
                if (m_SuppressNative) m_LastFallback = -1;
            }
            return false;
        }
        if (!m_NativeDown) return false;
        m_NativeDown = false;
        if (m_SuppressNative) { m_SuppressNative = false; return false; }
        m_LastNative = m_FallbackDuringHold ? -1 : now;
        return true;
    }
    bool fallback(qint64 now) {
        if (m_NativeDown) { m_FallbackDuringHold = true; return false; }
        if (m_LastNative >= 0 && now - m_LastNative < 250) { m_LastNative = -1; return false; }
        m_LastFallback = now;
        return true;
    }
    void reset() { *this = ShortcutActivationGate(); }
private:
    bool m_NativeDown = false, m_SuppressNative = false;
    bool m_FallbackDuringHold = false;
    qint64 m_LastNative = -1, m_LastFallback = -1;
};

class GlobalShortcut : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(bool desktopAssigned READ desktopAssigned NOTIFY statusChanged)
    Q_PROPERTY(QString effectiveShortcut READ effectiveShortcut NOTIFY statusChanged)
public:
    explicit GlobalShortcut(QObject* parent = nullptr);
    ~GlobalShortcut();
    void start();
    void setEnabled(bool enabled);
    bool setSequence(const QString& sequence);
    QString sequence() const { return m_Sequence; }
    Q_INVOKABLE void setRecording(bool recording);
    Q_INVOKABLE void fallbackActivated();
    bool available() const { return m_Available; }
    QString error() const { return m_Error; }
    bool desktopAssigned() const { return m_Backend && m_Available && !m_Backend->fixedChord; }
    QString effectiveShortcut() const {
        return m_Backend && !m_Backend->triggerDescription.isEmpty() ? m_Backend->triggerDescription :
            ShortcutBinding::display(m_Sequence);
    }
signals:
    void activated(QByteArray activationToken);
    void availabilityChanged(bool available);
    void fallbackAvailabilityChanged(bool available);
    void statusChanged();
private:
    void setAvailable(bool available);
    void stop();
    GlobalShortcutBackend* m_Backend = nullptr;
    ShortcutActivationGate m_Gate;
    QElapsedTimer m_Clock;
    QByteArray m_Token;
    bool m_Enabled = true;
    bool m_Started = false, m_Recording = false;
    bool m_Available = false;
    QString m_Sequence = ShortcutBinding::defaultSequence(), m_Error;
};
