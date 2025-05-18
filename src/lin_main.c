#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <SDL.h>
#include "walker.h"

#define TICKS_PER_SEC 4 /* RTC/4 */

static bool walkerRunning;

struct Vector2i {
    union {
        int x;
        int width;
    };
    union {
        int y;
        int height;
    };
};

float getEllapsedSeconds(Uint64 endPerformanceCount, Uint64 startPerformanceCount, Uint64 performanceFrequency) {
    Uint64 ellapsedMicroSeconds = endPerformanceCount - startPerformanceCount;
    return (float)ellapsedMicroSeconds / (float)performanceFrequency;
}

int main(int argc, char *argv[]) {
    struct Vector2i nativeRes = { LCD_WIDTH, LCD_HEIGHT };
    int scalingFactor = 4;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    struct Vector2i screenRes = { nativeRes.width * scalingFactor, nativeRes.height * scalingFactor };
    SDL_Window *win = SDL_CreateWindow("PokeStroller", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, screenRes.width, screenRes.height, SDL_WINDOW_SHOWN);
    if (win == NULL) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (ren == NULL) {
        SDL_DestroyWindow(win);
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    void* bitMapMemory = malloc(nativeRes.width * nativeRes.height * 4);
    if (bitMapMemory == NULL) {
        printf("Memory allocation error\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, nativeRes.width, nativeRes.height);
    if (texture == NULL) {
        printf("SDL_CreateTexture Error: %s\n", SDL_GetError());
        free(bitMapMemory);
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    initWalker();
    walkerRunning = true;
    uint64_t cycleCount = 0;

    Uint64 performanceFrequency = SDL_GetPerformanceFrequency();
    Uint64 startPerformanceCount = SDL_GetPerformanceCounter();

    while (walkerRunning) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                walkerRunning = false;
            }
            if (e.type == SDL_KEYDOWN) {
                uint8_t newInput = 0;
                switch (e.key.keysym.sym) {
                    case SDLK_SPACE:
                        newInput |= ENTER;
                        break;
                    case SDLK_z:
                        newInput |= LEFT;
                        break;
                    case SDLK_x:
                        newInput |= RIGHT;
                        break;
                }
                setKeys(newInput);
            }
        }

        bool error = runNextInstruction(&cycleCount);
        if (error) {
            walkerRunning = false;
        }

        if (cycleCount >= SYSTEM_CLOCK_CYCLES_PER_SECOND / TICKS_PER_SEC) {
            cycleCount -= SYSTEM_CLOCK_CYCLES_PER_SECOND / TICKS_PER_SEC;

            quarterRTCInterrupt();
            fillVideoBuffer(bitMapMemory);

            SDL_UpdateTexture(texture, NULL, bitMapMemory, nativeRes.width * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, texture, NULL, NULL);
            SDL_RenderPresent(ren);

            float desiredFrameTimeInS = 1.0f / TICKS_PER_SEC;
            Uint64 endPerformanceCount = SDL_GetPerformanceCounter();
            float elapsedSeconds = getEllapsedSeconds(endPerformanceCount, startPerformanceCount, performanceFrequency);
#ifdef DISPLAY_FRAME_TIME
            printf("%f\n", elapsedSeconds);
#endif
            if (elapsedSeconds < desiredFrameTimeInS) {
                SDL_Delay((Uint32)(1000.0f * (desiredFrameTimeInS - elapsedSeconds)));
                endPerformanceCount = SDL_GetPerformanceCounter();
            }
            startPerformanceCount = endPerformanceCount;
        }
    }

    free(bitMapMemory);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
