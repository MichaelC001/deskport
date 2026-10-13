#pragma once
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <functional>

// Called only by the SDL owner. On Linux Qt has its own GUI thread; on
// macOS/Windows this owner must also service Qt while the transport waits.
// pump services the connecting window; a session also reports its focus and
// presentation there, which the tray relies on while connecting.
inline void waitForDesktopConnection(QThread& connection, const std::function<void()>& pump,
                                     bool serviceQt, const std::function<void()>& checkCancellation) {
    connection.start();
    do {
        pump();
        checkCancellation();
        if (serviceQt) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            QCoreApplication::sendPostedEvents();
        }
    } while (!connection.wait(10));
    pump();
    checkCancellation();
    if (serviceQt) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
        QCoreApplication::sendPostedEvents();
    }
}
