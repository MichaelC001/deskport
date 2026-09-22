#pragma once
#include <QObject>
class QWindow;

// The main window's content already extends under the title bar (see
// Qt::ExpandedClientAreaHint in main.qml). This hides the title text and adds an
// empty unified toolbar so the close, minimize and zoom buttons sit vertically
// centred in DeskPort's 52-point top bar, at its left edge.
void deskPortUnifyTitleBar(QWindow* window);

// Title-bar behaviour for empty parts of the top bar, exposed to QML as
// "macTitleBar". QWindow::startSystemMove() does not move a window whose content
// covers the title bar, so the drag goes through AppKit directly.
class MacTitleBar : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    void setWindow(QWindow* window) { m_Window = window; }
    // Call from a mouse press: AppKit follows the pointer until release.
    Q_INVOKABLE void startDrag();
    // Honours System Settings: zoom, minimize or nothing on a double-click.
    Q_INVOKABLE void doubleClick();
private:
    QWindow* m_Window = nullptr;
};
