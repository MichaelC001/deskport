#pragma once
#include <QObject>
#include <QWindow>
#include <memory>
#include <functional>

// Window-scoped pointer capture. Ownership ends before the native surface does.
class CanvasPointer : public QObject {
public:
    using Motion = std::function<void(qreal, qreal)>;
    explicit CanvasPointer(Motion motion) : m_Motion(std::move(motion)) {}
    virtual void update(QWindow* window, bool enabled) = 0;
protected:
    Motion m_Motion;
};
std::unique_ptr<CanvasPointer> createCanvasPointer(CanvasPointer::Motion motion);
#ifdef Q_OS_LINUX
std::unique_ptr<CanvasPointer> createWaylandCanvasPointer(CanvasPointer::Motion motion);
#endif
