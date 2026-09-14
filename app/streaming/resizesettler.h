#pragma once
#include <QSize>
#include <QtGlobal>

// Trailing-edge debounce. Input activity restarts the quiet interval too, so
// releasing a drag cannot immediately commit a size observed mid-gesture.
class ResizeSettler {
public:
    static constexpr quint32 QuietMs = 500;
    bool update(QSize size, int scale, quint32 now, bool dragging) {
        if (!size.isValid()) { m_Valid = false; return false; }
        if (!m_Valid || size != m_Size || scale != m_Scale || dragging) {
            m_Size = size; m_Scale = scale; m_ChangedAt = now; m_Valid = true;
            return false;
        }
        return quint32(now - m_ChangedAt) >= QuietMs;
    }
private:
    QSize m_Size;
    int m_Scale = 0;
    quint32 m_ChangedAt = 0;
    bool m_Valid = false;
};
