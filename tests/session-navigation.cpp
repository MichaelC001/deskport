#include <QGuiApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cassert>
#include "gui/sdlgamepadkeynavigation.h"
#include "streaming/sessionlifetime.h"
#include "settings/mappingmanager.h"
// No mapping downloads, saved settings, devices or personal hosts in this test.
MappingManager::MappingManager() {}
void MappingManager::applyMappings() {}
static void events() {
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    assert(SDL_Init(SDL_INIT_EVENTS) == 0);
    SdlGamepadKeyNavigation navigation(nullptr);
    navigation.enable();
    navigation.disable();
    auto owner = new QObject;
    SessionLifetime lifetime(owner);
    assert(lifetime.beginExec());
    SDL_Event event {}; event.type = SDL_USEREVENT; event.user.code = 1234;
    assert(SDL_PushEvent(&event) == 1);
    for (int i = 0; i < 3; ++i) {
        navigation.enable();
        assert(navigation.getConnectedGamepads() == 0);
        events();
        assert(SDL_HasEvent(SDL_USEREVENT));
        assert(SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0);
    }
    lifetime.endExec();
    events();
    assert(SDL_HasEvent(SDL_USEREVENT));
    lifetime.cleanupFinished();
    events(); events();
    assert(SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0);
    assert(!SDL_HasEvent(SDL_USEREVENT));
    navigation.disable();
    SDL_Quit();
}
