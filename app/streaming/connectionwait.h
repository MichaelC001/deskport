#pragma once
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <functional>
#include "transitionwindow.h"

// Called only by the SDL owner. On Linux Qt has its own GUI thread; on
// macOS/Windows this owner must also service Qt while the transport waits.
inline void waitForDesktopConnection(QThread& connection, TransitionWindow& window,
                                     bool serviceQt, const std::function<void()>& checkCancellation) {
    connection.start();
    do {
        window.pump();
        checkCancellation();
        if (serviceQt) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            QCoreApplication::sendPostedEvents();
        }
    } while (!connection.wait(10));
    window.pump();
    checkCancellation();
    if (serviceQt) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        QCoreApplication::sendPostedEvents();
    }
}
