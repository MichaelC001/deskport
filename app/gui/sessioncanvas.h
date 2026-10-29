#pragma once
#include <QQuickItem>
#include <QTimer>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QElapsedTimer>
#include "sessionpresentation.h"
#include "canvaspointer.h"
#include "backend/shortcutrecorderinhibitor.h"

class CanvasTransitionCover;

class SessionCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* presentation READ presentation WRITE setPresentation NOTIFY presentationChanged)
    Q_PROPERTY(bool frameReady READ frameReady NOTIFY frameReadyChanged)
    Q_PROPERTY(bool transitionWaiting MEMBER m_TransitionWaiting NOTIFY transitionWaitingChanged)
    Q_PROPERTY(bool textInputEnabled MEMBER m_TextInput NOTIFY textInputChanged)
    Q_PROPERTY(bool cursorHidden MEMBER m_CursorHidden NOTIFY cursorHiddenChanged)
    Q_PROPERTY(bool captureRequested MEMBER m_CaptureRequested NOTIFY captureRequestedChanged)
    Q_PROPERTY(bool relativePointer MEMBER m_RelativePointer NOTIFY relativePointerChanged)
    Q_PROPERTY(QJsonObject pointerRegion MEMBER m_PointerRegion NOTIFY pointerRegionChanged)
public:
    explicit SessionCanvas(QQuickItem* parent = nullptr);
    ~SessionCanvas() override;
    QObject* presentation() const { return m_Presentation; }
    void setPresentation(QObject* presentation);
    bool frameReady() const { return m_Presentation && m_Presentation->ready(); }
signals:
    void presentationChanged();
    void frameReadyChanged();
    void transitionWaitingChanged();
    void captureRequestedChanged();
    void relativePointerChanged();
    void pointerRegionChanged();
    void textInputChanged();
    void cursorHiddenChanged();
    void viewportChanged(QJsonObject state);
    void input(QJsonObject event);
    void transitionCancelRequested();
protected:
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void inputMethodEvent(QInputMethodEvent* event) override;
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
    void touchEvent(QTouchEvent* event) override;
private:
    void refresh();
    void key(QKeyEvent* event, bool down);
    void mouse(QMouseEvent* event, bool down, bool button);
    QPoint position(const QPointF& value) const;
    QPointer<SessionPresentation> m_Presentation;
    QWindow* m_Surface = nullptr;
    CanvasTransitionCover* m_TransitionCover = nullptr;
    bool m_CoverRaised = false;
    QPointer<QQuickWindow> m_RootWindow;
    bool m_LocalWindowDrag = false;
    bool m_TransitionWaiting = false;
    QTimer m_ResizeTimer;
    int m_Buttons = 0;
    bool m_TextInput = false, m_CursorHidden = false;
    QTimer m_Timer;
    QJsonObject m_LastState;
    QPoint m_Mouse;
    bool m_Hovered = false, m_CaptureRequested = false, m_Grabbed = false;
    ShortcutRecorderInhibitor m_Inhibitor;
    std::unique_ptr<CanvasPointer> m_Pointer;
    bool m_RelativePointer = false;
    QJsonObject m_PointerRegion;
    QPointF m_RelativeRemainder;
    struct Touch { int slot; QPointF position; };
    static constexpr int MaxTouches = 16, MaxPendingTouches = 64, PendingTouchMs = 500;
    void touchInput(const QJsonObject& event);
    void flushPendingTouches();
    QHash<int, Touch> m_Touches;
    QList<QJsonObject> m_PendingTouches;
    QElapsedTimer m_PendingTouchAge;
};
