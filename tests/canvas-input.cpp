#include "streaming/canvasbridge.h"
#include "gui/canvaskey.h"
#include <QCoreApplication>
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<SDL_Event> events() {
    std::vector<SDL_Event> result; SDL_Event event;
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) > 0) result.push_back(event);
    return result;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    assert(CanvasKey::windows(0x1e) == SDL_SCANCODE_A);
    assert(CanvasKey::windows(0x11d) == SDL_SCANCODE_RCTRL);
    assert(CanvasKey::windows(0x11c) == SDL_SCANCODE_KP_ENTER);
    assert(CanvasKey::windows(0x14b) == SDL_SCANCODE_LEFT);
    assert(CanvasKey::evdev(30) == SDL_SCANCODE_A);
    assert(CanvasKey::evdev(100) == SDL_SCANCODE_RALT);
    assert(CanvasKey::evdev(105) == SDL_SCANCODE_LEFT);
    assert(CanvasKey::evdev(194) == SDL_SCANCODE_F24);
    assert(CanvasKey::mac(0) == SDL_SCANCODE_A);
    assert(CanvasKey::mac(12) == SDL_SCANCODE_Q);
    assert(CanvasKey::mac(62) == SDL_SCANCODE_RCTRL);
    assert(CanvasKey::mac(76) == SDL_SCANCODE_KP_ENTER);
    SDL_SetMainReady(); SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    auto window = SDL_CreateWindow("isolated input fixture", 0, 0, 800, 600, SDL_WINDOW_HIDDEN);
    assert(window);
    QJsonObject state{{"width",800},{"height",600},{"scale",1},{"visible",true},{"focused",true},{"hovered",true}};
    CanvasBridge::configure(state); CanvasBridge::requested(true); CanvasBridge::registerWindow(window);
    CanvasBridge::inputEnabled(true); events();
    QJsonObject key{{"type","key"},{"down",true},{"scan",SDL_SCANCODE_A},{"symbol",SDLK_a},{"modifiers",0}};
    CanvasBridge::input(key); CanvasBridge::synchronize(window);
    auto received = events(); assert(received.size() == 1 && received[0].type == SDL_KEYDOWN);
    // Losing and regaining focus before the next worker tick must still release.
    state["focused"] = false; CanvasBridge::state(state);
    state["focused"] = true; CanvasBridge::state(state); CanvasBridge::input(key);
    CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 2);
    assert(received[0].window.event == SDL_WINDOWEVENT_FOCUS_LOST);
    assert(received[1].window.event == SDL_WINDOWEVENT_FOCUS_GAINED);
    // A flooded queue must preserve this boundary even after a fresh viewport.
    for (int i = 0; i < 257; ++i) CanvasBridge::input(key);
    CanvasBridge::state(state); CanvasBridge::synchronize(window); received = events();
    assert(received.size() == 2 && received[0].window.event == SDL_WINDOWEVENT_FOCUS_LOST);
    CanvasBridge::input(key); CanvasBridge::synchronize(window); assert(events().empty());
    CanvasBridge::inputEnabled(true);
    const auto unicode = QString::fromUtf8("中文🙂中文🙂中文🙂中文🙂中文🙂中文🙂");
    CanvasBridge::input({{"type","text"},{"text",unicode}}); CanvasBridge::synchronize(window);
    QByteArray text;
    for (const auto& event : events()) {
        assert(event.type == SDL_TEXTINPUT);
        QByteArray chunk(event.text.text);
        assert(QString::fromUtf8(chunk).toUtf8() == chunk); text += chunk;
    }
    assert(QString::fromUtf8(text) == unicode);
    CanvasBridge::question("Title", "Body", "Cancel", "Continue");
    auto id = quint64(CanvasBridge::description()["question"].toObject()["id"].toDouble());
    CanvasBridge::answer(id + 1, 1); assert(CanvasBridge::takeAnswer() == -1);
    CanvasBridge::answer(id, 2); assert(CanvasBridge::takeAnswer() == -1);
    CanvasBridge::answer(id, 1); assert(CanvasBridge::takeAnswer() == 1);
    CanvasBridge::dismissQuestion(); assert(!CanvasBridge::description().contains("question"));
    CanvasBridge::requested(false); CanvasBridge::input(key); CanvasBridge::synchronize(window);
    for (const auto& event : events()) assert(event.type != SDL_KEYDOWN);
    assert(SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN);
    SDL_DestroyWindow(window); SDL_Quit();
    puts("PASS: focus-loss boundaries, overflow release, hidden input rejection, Unicode chunks and stale question answers");
}
