#pragma once

#include <QObject>
#include <QQmlEngine>
#include <functional>

// The page can disappear while exec() or transport cleanup still uses the
// session. All methods run on the object's Qt thread; cleanup arrives queued.
class SessionLifetime {
public:
    explicit SessionLifetime(QObject* owner, std::function<void()> ready = {})
        : m_Owner(owner), m_Ready(std::move(ready)) {}
    static bool busy() { return s_ActiveOwner != nullptr; }
    bool beginExec() {
        if (m_Started || busy()) return false;
        m_Started = true;
        s_ActiveOwner = m_Owner;
        QQmlEngine::setObjectOwnership(m_Owner, QQmlEngine::CppOwnership);
        m_Executing = true;
        return true;
    }
    void endExec() {
        m_Executing = false;
        disposeIfReady();
    }
    void cleanupFinished() {
        m_Cleaned = true;
        disposeIfReady();
    }
private:
    void disposeIfReady() {
        if (m_Cleaned && !m_Executing && !m_Disposed) {
            m_Disposed = true;
            if (s_ActiveOwner == m_Owner) s_ActiveOwner = nullptr;
            if (m_Ready) m_Ready();
            m_Owner->deleteLater();
        }
    }
    QObject* m_Owner;
    std::function<void()> m_Ready;
    bool m_Disposed = false;
    inline static QObject* s_ActiveOwner = nullptr;
    bool m_Started = false;
    bool m_Executing = false;
    bool m_Cleaned = false;
};
