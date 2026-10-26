#include "canvasbridge.h"
#include <atomic>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <cmath>

namespace {
std::atomic<bool> configured{false}, wanted{false}, acceptsInput{false};
QMutex stateMutex;
QPair<QString, QString> videoConnection;
QJsonObject viewport, details;
QQueue<QJsonObject> pendingInput;
constexpr int MaxWidth = 16384, MaxHeight = 16384;
bool previousFocus = false, previousHover = false;
bool releasePending = false;
quint64 questionId = 0;
int questionAnswer = -1;
constexpr const char* CanvasTag = "deskport-shell-canvas";
constexpr int CanvasTextCode = 0x44505458;
constexpr size_t TextPayloadSize = sizeof(SDL_UserEvent::data1) + sizeof(SDL_UserEvent::data2);
static_assert(TextPayloadSize >= 5 && TextPayloadSize <= SDL_TEXTINPUTEVENT_TEXT_SIZE);
int bounded(const QJsonObject& object, const char* key, int maximum) {
    const auto value = object.value(key).toDouble(-1);
    return std::isfinite(value) && value >= 0 && value <= maximum ? int(value) : -1;
}
void windowEvent(SDL_Window* window, Uint8 kind) {
    SDL_Event event{}; event.type = SDL_WINDOWEVENT; event.window.windowID = SDL_GetWindowID(window);
    event.window.event = kind; SDL_PushEvent(&event);
}
}
void CanvasBridge::setVideoTransport(const QString& endpoint, const QString& token) {
    QMutexLocker lock(&stateMutex);
    videoConnection = {endpoint, token};
}
QPair<QString, QString> CanvasBridge::videoTransport() {
    QMutexLocker lock(&stateMutex);
    return videoConnection;
}
void CanvasBridge::configure(const QJsonObject& value) { state(value); configured = true; }
bool CanvasBridge::enabled() { return configured.load(); }
void CanvasBridge::state(const QJsonObject& value) {
    const int width = bounded(value, "width", MaxWidth), height = bounded(value, "height", MaxHeight);
    const qreal scale = value.value("scale").toDouble();
    if (width < 1 || height < 1 || !std::isfinite(scale) || scale < 0.5 || scale > 4) return;
    QMutexLocker lock(&stateMutex);
    if (!value["visible"].toBool() || !value["focused"].toBool()) {
        releasePending = true;
        pendingInput.clear();
    }
    viewport = value;
}
void CanvasBridge::requested(bool visible) {
    wanted = visible;
    if (!visible) inputEnabled(false);
}
bool CanvasBridge::requested() { return wanted.load(); }
void CanvasBridge::registerWindow(SDL_Window* window) {
    if (!enabled() || !window) return;
    SDL_SetWindowData(window, CanvasTag, &configured);
    previousFocus = previousHover = false;
    synchronize(window);
}
bool CanvasBridge::owns(SDL_Window* window) { return enabled() && window && SDL_GetWindowData(window, CanvasTag); }
QSize CanvasBridge::pixels() {
    QMutexLocker lock(&stateMutex);
    return QSize(qBound(1, qRound(viewport["width"].toInt() * viewport["scale"].toDouble(1)), MaxWidth),
                 qBound(1, qRound(viewport["height"].toInt() * viewport["scale"].toDouble(1)), MaxHeight));
}
qreal CanvasBridge::scale() { QMutexLocker lock(&stateMutex); return viewport["scale"].toDouble(1); }
bool CanvasBridge::fullscreen() { QMutexLocker lock(&stateMutex); return viewport["fullscreen"].toBool(); }
Uint32 CanvasBridge::flags(SDL_Window* window) {
    const auto native = SDL_GetWindowFlags(window);
    if (!owns(window)) return native;
    QMutexLocker lock(&stateMutex);
    auto result = native & ~(SDL_WINDOW_HIDDEN | SDL_WINDOW_SHOWN | SDL_WINDOW_MINIMIZED |
        SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_KEYBOARD_GRABBED);
    const bool visible = wanted && viewport["visible"].toBool();
    result |= visible ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN;
    if (visible && viewport["focused"].toBool()) result |= SDL_WINDOW_INPUT_FOCUS;
    if (visible && viewport["hovered"].toBool()) result |= SDL_WINDOW_MOUSE_FOCUS;
    if (viewport["fullscreen"].toBool()) result |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (viewport["grabbed"].toBool() && (result & SDL_WINDOW_INPUT_FOCUS)) result |= SDL_WINDOW_KEYBOARD_GRABBED;
    return result;
}
Uint32 CanvasBridge::mouseState(int* x, int* y) {
    QMutexLocker lock(&stateMutex);
    if (x) *x = viewport["x"].toInt();
    if (y) *y = viewport["y"].toInt();
    return quint32(viewport["buttons"].toInt());
}
void CanvasBridge::inputEnabled(bool enabled) {
    acceptsInput = enabled;
    if (!enabled) { QMutexLocker lock(&stateMutex); pendingInput.clear(); releasePending = true; details["grab"] = false; }
}
void CanvasBridge::input(const QJsonObject& event) {
    if (!enabled() || !wanted || !acceptsInput) return;
    QMutexLocker lock(&stateMutex);
    if (!viewport["visible"].toBool() || !viewport["focused"].toBool()) return;
    if (pendingInput.size() >= 256) {
        // Never replay a backlog after the user has released control.
        pendingInput.clear(); acceptsInput = false;
        releasePending = true;
        viewport["focused"] = false; details["grab"] = false;
        return;
    }
    pendingInput.enqueue(event);
}
bool CanvasBridge::decodeTextEvent(SDL_Event& event) {
    if (event.type != SDL_USEREVENT || event.user.code != CanvasTextCode) return false;
    SDL_Event text{};
    text.type = SDL_TEXTINPUT;
    text.text.windowID = event.user.windowID;
    text.text.timestamp = event.user.timestamp;
    SDL_memcpy(text.text.text, &event.user.data1, TextPayloadSize);
    text.text.text[TextPayloadSize - 1] = '\0';
    event = text;
    return true;
}
void CanvasBridge::synchronize(SDL_Window* window) {
    if (!owns(window)) return;
    QJsonObject state; QQueue<QJsonObject> input; bool release;
    { QMutexLocker lock(&stateMutex); state = viewport; input.swap(pendingInput); release = releasePending; releasePending = false; }
    const int width = state["width"].toInt(1120), height = state["height"].toInt(704);
    int currentWidth, currentHeight; SDL_GetWindowSize(window, &currentWidth, &currentHeight);
    if (width != currentWidth || height != currentHeight) SDL_SetWindowSize(window, width, height);
    const bool focus = wanted && state["visible"].toBool() && state["focused"].toBool();
    const bool hover = focus && state["hovered"].toBool();
    if (release) {
        // A rapid focus return must not erase the release boundary. Deliver
        // loss before gain and discard input queued across that boundary.
        windowEvent(window, SDL_WINDOWEVENT_FOCUS_LOST);
        previousFocus = false; input.clear();
    }
    if (previousFocus != focus) windowEvent(window, focus ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST);
    if (previousHover != hover) windowEvent(window, hover ? SDL_WINDOWEVENT_ENTER : SDL_WINDOWEVENT_LEAVE);
    previousFocus = focus; previousHover = hover;
    if (!focus || !acceptsInput) return;
    for (const auto& value : input) {
        SDL_Event event{};
        const auto type = value["type"].toString();
        if (type == "key") {
            const int scan = bounded(value, "scan", SDL_NUM_SCANCODES - 1);
            const int symbol = bounded(value, "symbol", SDLK_SCANCODE_MASK + SDL_NUM_SCANCODES);
            const int modifiers = bounded(value, "modifiers", 65535);
            if (scan < 0 || symbol < 0 || modifiers < 0) continue;
            event.type = value["down"].toBool() ? SDL_KEYDOWN : SDL_KEYUP;
            event.key.windowID = SDL_GetWindowID(window); event.key.state = value["down"].toBool() ? SDL_PRESSED : SDL_RELEASED;
            event.key.repeat = value["repeat"].toBool(); event.key.keysym.scancode = SDL_Scancode(scan);
            event.key.keysym.sym = symbol; event.key.keysym.mod = modifiers;
        } else if (type == "motion" || type == "button") {
            const int x = bounded(value, "x", MaxWidth), y = bounded(value, "y", MaxHeight);
            if (x < 0 || y < 0) continue;
            if (type == "motion") {
                event.type = SDL_MOUSEMOTION; event.motion.windowID = SDL_GetWindowID(window);
                event.motion.x = x; event.motion.y = y;
                event.motion.xrel = qBound(-32767, value["dx"].toInt(), 32767);
                event.motion.yrel = qBound(-32767, value["dy"].toInt(), 32767);
            } else {
                const int button = bounded(value, "button", 5); if (button < 1) continue;
                event.type = value["down"].toBool() ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
                event.button.windowID = SDL_GetWindowID(window); event.button.button = button;
                event.button.state = value["down"].toBool() ? SDL_PRESSED : SDL_RELEASED;
                event.button.x = x; event.button.y = y;
            }
        } else if (type == "text") {
            const auto text = value["text"].toString();
            if (text.size() > 4096) continue;
            QByteArray chunk;
            const auto flush = [&] {
                if (chunk.isEmpty()) return;
                // sdl2-compat 2.32.68 rejects pushed SDL_TEXTINPUT events and
                // forwards a null event to SDL3. Carry owned inline bytes in a
                // user event instead; no allocation can leak on queue flushes.
                SDL_Event commit{}; commit.type = SDL_USEREVENT;
                commit.user.code = CanvasTextCode;
                commit.user.windowID = SDL_GetWindowID(window); commit.user.timestamp = SDL_GetTicks();
                SDL_memcpy(&commit.user.data1, chunk.constData(), size_t(chunk.size()));
                SDL_PushEvent(&commit); chunk.clear();
            };
            for (char32_t scalar : text.toUcs4()) {
                const auto encoded = QString::fromUcs4(&scalar, 1).toUtf8();
                if (size_t(chunk.size() + encoded.size()) >= TextPayloadSize) flush();
                chunk += encoded;
            }
            flush(); continue;
        } else if (type == "wheel") {
            event.type = SDL_MOUSEWHEEL; event.wheel.windowID = SDL_GetWindowID(window);
            event.wheel.x = qBound(-120, value["x"].toInt(), 120); event.wheel.y = qBound(-120, value["y"].toInt(), 120);
            event.wheel.preciseX = value["x"].toDouble(); event.wheel.preciseY = value["y"].toDouble();
        } else continue;
        SDL_PushEvent(&event);
    }
}
void CanvasBridge::capture(bool grab, bool hidden, bool relative) {
    QMutexLocker lock(&stateMutex); details["grab"] = grab; details["cursorHidden"] = hidden;
    details["relative"] = relative;
}
void CanvasBridge::overlay(int type, bool enabled, const QString& text) {
    QMutexLocker lock(&stateMutex);
    auto overlays = details["overlays"].toObject();
    overlays[QString::number(type)] = QJsonObject{{"enabled",enabled},{"text",text.left(512)}};
    details["overlays"] = overlays;
}
void CanvasBridge::textInput(bool enabled) { QMutexLocker lock(&stateMutex); details["textInput"] = enabled; }
void CanvasBridge::label(const QString& text) { QMutexLocker lock(&stateMutex); details["label"] = text; }
QJsonObject CanvasBridge::description() {
    QJsonObject result;
    { QMutexLocker lock(&stateMutex); result = details; }
    return result;
}
void CanvasBridge::question(const QString& title, const QString& text, const QString& cancel, const QString& accept) {
    QMutexLocker lock(&stateMutex);
    questionAnswer = -1;
    details["question"] = QJsonObject{{"id",double(++questionId)}, {"title",title}, {"text",text}, {"cancel",cancel}, {"accept",accept}};
}
void CanvasBridge::answer(quint64 id, int choice) {
    QMutexLocker lock(&stateMutex);
    if (wanted && id == questionId && details.value("question").isObject() && (choice == 0 || choice == 1)) questionAnswer = choice;
}
int CanvasBridge::takeAnswer() { QMutexLocker lock(&stateMutex); return questionAnswer; }
void CanvasBridge::dismissQuestion() { QMutexLocker lock(&stateMutex); details.remove("question"); questionAnswer = -1; }
