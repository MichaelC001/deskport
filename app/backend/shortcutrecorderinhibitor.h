#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <memory>

// The adapter borrows the recorder's native window and must release its request
// before that window loses focus or destroys its platform surface.
class ShortcutRecorderInhibitionBackend {
public:
    virtual ~ShortcutRecorderInhibitionBackend() = default;
    virtual bool start(QWindow* window) = 0;
    virtual void stop() = 0;
};

std::unique_ptr<ShortcutRecorderInhibitionBackend> createShortcutRecorderInhibitionBackend();

class ShortcutRecorderInhibitor final : public QObject {
public:
    explicit ShortcutRecorderInhibitor(QObject* parent = nullptr);
    ShortcutRecorderInhibitor(std::unique_ptr<ShortcutRecorderInhibitionBackend> backend,
                             QObject* parent = nullptr);
    ~ShortcutRecorderInhibitor() override;

    void setWindow(QWindow* window);
    void setEnabled(bool enabled);
    bool isInhibited() const { return m_Inhibited; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void release();
    QPointer<QWindow> m_Window;
    std::unique_ptr<ShortcutRecorderInhibitionBackend> m_Backend;
    bool m_Enabled = false;
    bool m_Inhibited = false;
};
