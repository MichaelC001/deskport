#pragma once
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRectF>
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
// With the optional hidden stream title bar, the canvas extends under a
// transparent title bar without its buttons instead. Control+Command reserves
// local window gestures: drag inside to move, or an edge/corner to resize. Plain
// pointer gestures belong to the remote canvas. Native full screen is retained.
class MacTitleBar : public QObject
{
    Q_OBJECT
public:
    explicit MacTitleBar(QObject* parent = nullptr);
    ~MacTitleBar() override;
    void attach(QQuickWindow* window);
    void setCanvasVisible(bool visible);
    void setStreamTitleBarHidden(bool hidden);
    bool isNativeFullscreen() const;
    bool isToolbarVisible() const;
    bool isContentUnderTitleBar() const;
    bool isFullscreenTransitioning() const { return m_FullscreenTransitioning; }
private:
    // True when the point (window coordinates) is on the bar but not on a control.
    bool isDragArea(const QPointF& point) const;
    bool isCanvasArea(const QPointF& point) const;
    void setDragInputSuspended(bool suspended);
    void cancelWindowGesture(bool drainMouse = false);
    void updateChrome();
    QPointer<QQuickWindow> m_Window;
    void* m_Monitor = nullptr;
    void* m_FullscreenObservers = nullptr;
    bool m_FullscreenTransitioning = false;
    bool m_CanvasVisible = false;
    bool m_StreamTitleBarHidden = false;
    bool m_LocalGestureActive = false;
    int m_GestureEdges = 0;
    QPointF m_GestureOrigin;
    QRectF m_GestureFrame;
    QRectF m_FullscreenRestoreFrame;
    quint64 m_GestureGeneration = 0;
    bool m_NativeResizable = true;
    bool m_NativeMovable = true;
    bool m_ResizingWindow = false;
    bool m_DragInputSuspended = false;
    bool m_ConsumeDragTail = false;
};
