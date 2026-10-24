#include "backend/globalshortcut.h"
#include <QCoreApplication>
#include <QScopedPointer>
#include <cassert>

// Link the native adapter without registering any shortcut in the user's session.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QScopedPointer<GlobalShortcutBackend> backend(createGlobalShortcutBackend(nullptr));
    assert(backend && backend->binding.portable() == ShortcutBinding::defaultSequence());
    backend->binding = ShortcutBinding::parse("Ctrl+Alt+K");
    assert(backend->binding.valid());
    backend->stop();
}
