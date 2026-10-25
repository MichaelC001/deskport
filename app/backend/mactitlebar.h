#pragma once
#include <QObject>
class QQuickWindow;

// macOS only: DeskPort's top bar replaces the title bar. The window content
// extends under it (Qt::ExpandedClientAreaHint in main.qml), the title text is
// hidden, and an empty unified toolbar centres the close, minimize and zoom
// buttons at the left of the 52-point bar. A press on an empty part of the bar
// (the item named "topBar", outside any control) moves the window and a
// double-click zooms or minimizes it, following System Settings. The green
// button zooms instead of entering full screen.
// The remote canvas uses the standard system title bar outside its content;
// returning to Devices restores the unified application bar on the same window.
class MacTitleBar : public QObject
{
    Q_OBJECT
public:
    explicit MacTitleBar(QObject* parent = nullptr);
    ~MacTitleBar() override;
    void attach(QQuickWindow* window);
    void setCanvasVisible(bool visible);
    bool isNativeFullscreen() const;
    bool isToolbarVisible() const;
    bool isContentUnderTitleBar() const;
    bool isFullscreenTransitioning() const { return m_FullscreenTransitioning; }
private:
    // True when the point (window coordinates) is on the bar but not on a control.
    bool isDragArea(const QPointF& point) const;
    void updateChrome();
    QQuickWindow* m_Window = nullptr;
    void* m_Monitor = nullptr;
    void* m_FullscreenObservers = nullptr;
    bool m_FullscreenTransitioning = false;
    bool m_CanvasVisible = false;
};
