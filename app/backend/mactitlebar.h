#pragma once
class QWindow;

// The main window's content already extends under the title bar (see
// Qt::ExpandedClientAreaHint in main.qml). This hides the title text and adds an
// empty unified toolbar so the close, minimize and zoom buttons sit vertically
// centred in DeskPort's 52-point top bar, at its left edge.
void deskPortUnifyTitleBar(QWindow* window);
