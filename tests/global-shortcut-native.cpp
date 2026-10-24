#include "backend/globalshortcut.h"
#include <QCoreApplication>
#include <QScopedPointer>
#include <cassert>

#ifdef Q_OS_MACOS
// Keep the platform mapping in this fixture's translation unit so it can be
// checked without installing an event handler or registering a global key.
#include "../app/backend/globalshortcut_mac.mm"
#endif

// Link the native adapter without registering any shortcut in the user's session.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QScopedPointer<GlobalShortcutBackend> backend(createGlobalShortcutBackend(nullptr));
    assert(backend && backend->binding.portable() == ShortcutBinding::defaultSequence());
    backend->binding = ShortcutBinding::parse("Ctrl+Alt+K");
    assert(backend->binding.valid());
#ifdef Q_OS_MACOS
    const struct { int qt; UInt32 carbon; } namedKeys[] {
        {Qt::Key_Space, kVK_Space}, {Qt::Key_Tab, kVK_Tab},
        {Qt::Key_Backspace, kVK_Delete}, {Qt::Key_Delete, kVK_ForwardDelete},
        {Qt::Key_Home, kVK_Home}, {Qt::Key_End, kVK_End},
        {Qt::Key_PageUp, kVK_PageUp}, {Qt::Key_PageDown, kVK_PageDown},
        {Qt::Key_Left, kVK_LeftArrow}, {Qt::Key_Right, kVK_RightArrow},
        {Qt::Key_Up, kVK_UpArrow}, {Qt::Key_Down, kVK_DownArrow},
        {Qt::Key_Escape, kVK_Escape}, {Qt::Key_Return, kVK_Return},
        {Qt::Key_F12, kVK_F12}, {Qt::Key_F13, kVK_F13},
        {Qt::Key_F14, kVK_F14}, {Qt::Key_F15, kVK_F15},
        {Qt::Key_F16, kVK_F16}, {Qt::Key_F17, kVK_F17},
        {Qt::Key_F18, kVK_F18}, {Qt::Key_F19, kVK_F19}, {Qt::Key_F20, kVK_F20},
    };
    for (const auto& expected : namedKeys) {
        const ShortcutBinding binding{expected.qt, Qt::ControlModifier | Qt::AltModifier};
        UInt32 key = 0, modifiers = 0;
        assert(binding.valid());
        assert(currentKey(binding, key, modifiers));
        assert(key == expected.carbon);
        assert(modifiers == (controlKey | optionKey));
    }
#endif
    backend->stop();
}
