#include "streaming/input/localtextinput.h"
#include "streaming/video/overlaymanager.h"
#include <QGuiApplication>
#include "path.h"
#include <QFile>
#include <QTextStream>
#include <cassert>
#include <vector>

// OverlayManager's diagnostic font is irrelevant to native text font fallback.
QByteArray Path::readDataFile(QString) { return {}; }

static SDL_KeyboardEvent key(SDL_Scancode scan, SDL_Keymod mod=KMOD_NONE, bool down=true) {
    SDL_KeyboardEvent event {};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.keysym.scancode = scan; event.keysym.mod = mod;
    return event;
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    const bool native = app.arguments().contains("--native");
    auto window = SDL_CreateWindow("DeskPort isolated local IME test — synthetic text only", 100, 100, 960, 480,
                                   native ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN);
    assert(window);
    const Uint32 id = SDL_GetWindowID(window);
    std::vector<QByteArray> commits;
    QString preedit;
    int updates = 0;
    Overlay::OverlayManager overlay;
    LocalTextInput input(true, [&](const QByteArray& text) { commits.push_back(text); },
        [&](const QString& text, int start, int length) {
            preedit = text; ++updates; overlay.updateTextInput(text, start, length);
        });
    input.setActive(true);
    assert(SDL_IsTextInputActive());
    auto edit = [&](const char* text, int start=0, int length=0) {
        input.editing(id, SDL_GetTicks(), text, start, length, id);
    };
    auto commit = [&](const char* text) { input.text(id, SDL_GetTicks(), text, id); };
    if (!native) {
        for (const auto text : {u8"中文输入", u8"日本語にほんご", u8"한글입력", u8"é à € 😀"}) {
            edit(text, 1, 2); assert(preedit == QString::fromUtf8(text));
            assert(commits.empty() || !commits.back().isEmpty());
            auto surface = overlay.getUpdatedOverlaySurface(Overlay::OverlayTextInput);
            assert(surface && surface->w == 960 && surface->h == 44); SDL_FreeSurface(surface);
            assert(input.consumeKey(key(SDL_SCANCODE_RETURN)));
            commit(text); assert(commits.back() == QByteArray(text));
            assert(!input.composing()); assert(preedit.isEmpty());
            // The key-up that confirmed composition must not leak remotely.
            assert(input.consumeKey(key(SDL_SCANCODE_RETURN, KMOD_NONE, false)));
        }
        for (const auto scan : {SDL_SCANCODE_A, SDL_SCANCODE_SPACE, SDL_SCANCODE_1, SDL_SCANCODE_SLASH, SDL_SCANCODE_KP_PLUS})
            assert(input.consumeKey(key(scan)));
        for (const auto scan : {SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_TAB, SDL_SCANCODE_LEFT, SDL_SCANCODE_F1})
            assert(!input.consumeKey(key(scan)));
        assert(input.consumeKey(key(SDL_SCANCODE_LSHIFT)));
        assert(!input.consumeKey(key(SDL_SCANCODE_C, KMOD_CTRL)));
        const auto count = commits.size(); commit("c"); assert(commits.size() == count);
        assert(input.consumeKey(key(SDL_SCANCODE_SPACE, KMOD_CTRL)));
        assert(input.consumeKey(key(SDL_SCANCODE_SPACE, KMOD_GUI)));
        assert(input.consumeKey(key(SDL_SCANCODE_E, SDL_Keymod(KMOD_RALT | KMOD_CTRL))));
        commit(u8"€"); assert(commits.back() == QByteArray(u8"€"));
        edit(u8"未提交取消"); input.cancel(); assert(preedit.isEmpty()); assert(!input.composing());
        commit("late cancelled commit"); assert(commits.size() == count+1);
        input.setActive(false); commit(u8"禁止"); assert(commits.size() == count+1);
        input.setActive(true);
        input.text(id+1, SDL_GetTicks(), u8"別の窓", id);
        input.text(id, SDL_GetTicks()-1000, "stale", id);
        commit("\xff"); commit("\n"); commit("\t"); assert(commits.size() == count+1);
        edit(std::string(400, 'x').c_str(), 10000, 10000); assert(preedit.size()==400);
        commit(u8"한"); edit(u8"글"); commit(u8"글");
        assert(commits[commits.size()-2] == QByteArray(u8"한")); assert(commits.back()==QByteArray(u8"글"));
        input.setActive(false); assert(!SDL_IsTextInputActive());
        LocalTextInput physical(false, [](const QByteArray&) { assert(false); }, [](const QString&,int,int) {});
        physical.setActive(true); assert(!physical.active()); assert(!physical.consumeKey(key(SDL_SCANCODE_A)));
        qInfo("PASS: local IME CJK/Unicode, preedit, commands, AltGr, cancellation, stale/window guards and overlay glyph surface");
    } else {
        QFile output(qEnvironmentVariable("DESKPORT_IME_TEST_LOG"));
        assert(output.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream log(&output);
        auto renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        assert(renderer);
        bool running = true;
        const Uint32 started = SDL_GetTicks();
        while (running && SDL_GetTicks()-started < 120000) {
            SDL_Event e;
            if (!SDL_WaitEventTimeout(&e, 50)) continue;
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_HIDDEN) input.setActive(false);
                if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) input.setActive(true);
            } else if (e.type == SDL_TEXTEDITING) input.editing(e.edit.windowID,e.edit.timestamp,e.edit.text,e.edit.start,e.edit.length,id);
#if SDL_VERSION_ATLEAST(2, 0, 22)
            else if (e.type == SDL_TEXTEDITING_EXT) { input.editing(e.editExt.windowID,e.editExt.timestamp,e.editExt.text,e.editExt.start,e.editExt.length,id); SDL_free(e.editExt.text); }
#endif
            else if (e.type == SDL_TEXTINPUT) input.text(e.text.windowID,e.text.timestamp,e.text.text,id);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                const bool consumed = input.consumeKey(e.key);
                log << "key " << e.key.keysym.scancode << " " << consumed << "\n";
            }
            if (!preedit.isEmpty()) log << "preedit " << preedit << "\n";
            for (const auto& text : commits) log << "commit " << QString::fromUtf8(text) << "\n";
            commits.clear(); log.flush();
            SDL_SetRenderDrawColor(renderer, 24,26,30,255); SDL_RenderClear(renderer);
            auto surface = overlay.getUpdatedOverlaySurface(Overlay::OverlayTextInput);
            if (surface) {
                auto texture = SDL_CreateTextureFromSurface(renderer,surface); SDL_Rect rect{0,0,surface->w,surface->h};
                SDL_RenderCopy(renderer,texture,nullptr,&rect); SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
            }
            SDL_RenderPresent(renderer);
        }
        input.setActive(false); SDL_DestroyRenderer(renderer);
    }
    input.setActive(false); SDL_DestroyWindow(window); SDL_Quit();
}
