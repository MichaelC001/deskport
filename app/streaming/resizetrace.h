#pragma once
#include <SDL.h>
inline void deskportResizeStage(const char* stage, int width = 0, int height = 0) {
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "DeskPort resize stage=%s tick_ms=%u width=%d height=%d",
                stage, SDL_GetTicks(), width, height);
}
