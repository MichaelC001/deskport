#pragma once
#include <QObject>
class QQuickWindow;

// macOS only: DeskPort's top bar replaces the title bar. The window content
// extends under it (Qt::ExpandedClientAreaHint in main.qml), the title text is
// hidden, and an empty unified toolbar centres the close, minimize and zoom
// buttons at the left of the 52-point bar. A press on an empty part of the bar
// (the item named "topBar", outside any control) moves the window and a
// double-click zooms or minimizes it, following System Settings.
class MacTitleBar : public QObject
{
    Q_OBJECT
public:
    explicit MacTitleBar(QObject* parent = nullptr);
    ~MacTitleBar() override;
    void attach(QQuickWindow* window);
private:
    // True when the point (window coordinates) is on the bar but not on a control.
    bool isDragArea(const QPointF& point) const;
    QQuickWindow* m_Window = nullptr;
    void* m_Monitor = nullptr;
};
