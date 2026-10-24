#pragma once
#include <QQuickItem>
#include <QTimer>
#include <QJsonObject>
#include "sessionpresentation.h"
#include "backend/shortcutrecorderinhibitor.h"

class SessionCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* presentation READ presentation WRITE setPresentation NOTIFY presentationChanged)
    Q_PROPERTY(bool frameReady READ frameReady NOTIFY frameReadyChanged)
    Q_PROPERTY(bool captureRequested MEMBER m_CaptureRequested NOTIFY captureRequestedChanged)
public:
    explicit SessionCanvas(QQuickItem* parent = nullptr);
    ~SessionCanvas() override;
    QObject* presentation() const { return m_Presentation; }
    void setPresentation(QObject* presentation);
    bool frameReady() const { return m_Presentation && m_Presentation->ready(); }
signals:
    void presentationChanged();
    void frameReadyChanged();
    void captureRequestedChanged();
    void viewportChanged(QJsonObject state);
    void input(QJsonObject event);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void geometryChange(const QRectF& next, const QRectF& previous) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
private:
    void refresh();
    void key(QKeyEvent* event, bool down);
    void mouse(QMouseEvent* event, bool down, bool button);
    QPoint position(const QPointF& value) const;
    QPointer<SessionPresentation> m_Presentation;
    QWindow* m_Surface = nullptr;
    QTimer m_ResizeTimer;
    int m_Buttons = 0;
    QTimer m_Timer;
    QJsonObject m_LastState;
    QPoint m_Mouse;
    bool m_Hovered = false, m_CaptureRequested = false, m_Grabbed = false;
    ShortcutRecorderInhibitor m_Inhibitor;
};
