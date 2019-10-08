#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

// #include "soundboi.hpp"
#include "soundboy.hpp"

#include <iostream>
#include <string>
#include <chrono>

#include <limits.h>

// clang++ main.cpp -o program.exe -lSDL2 -lSDL2_ttf -lSDL2_image

using timepoint = std::chrono::high_resolution_clock::time_point;
#define time() std::chrono::high_resolution_clock::now()

bool init();
void exit();
void drawText(std::string text, int x, int y, float size, SDL_Color color);
void audioFormatInfo(const SDL_AudioSpec &spec);

const int WINDOW_WIDTH = 1080;
const int WINDOW_HEIGHT = 720;
const char *title = "Game.";

SDL_Window *window = nullptr;
SDL_Renderer *renderer = nullptr;
SDL_Texture *tree = nullptr;

TTF_Font *font = nullptr;

SoundEngine::Track soundboi;

const int GROUND_LEVEL = WINDOW_HEIGHT - WINDOW_HEIGHT / 3;
SDL_Rect ground = { 
    0, GROUND_LEVEL,
    WINDOW_WIDTH,  WINDOW_HEIGHT - GROUND_LEVEL
};

int main (int argc, char *argv[]) {

    if (!init())
        return 1;

    SoundEngine::Clip c = {{
        soundboi.buffer,
        0, soundboi.length,
        0,
        0,
        1.0f, 1.0f,
        0.2f
    }};

    SoundEngine::ClipInfo ci {
        soundboi.buffer,
        0, soundboi.length,
        0,
        0,
        1.0f, 1.0f,
        0.5f
    };

    timepoint oldTime = time();
    uint64_t currentTime = 0;
    int frameCount = 0;
    std::string framesNow = "0";

    SDL_Event event;

    bool running = true;
    while (running) {

        timepoint newTime = time();
        uint64_t deltaTime = (newTime - oldTime).count();
        oldTime = newTime;

        frameCount++;
        currentTime += deltaTime;
        const int SECOND = 1000000000;
        if (currentTime >= SECOND) {
            int frames = frameCount / (currentTime / SECOND);
            currentTime %= SECOND;
            framesNow = std::to_string(frames);
            frameCount = 0;
        }

        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
//                     SoundEngine::dispatch(ci);
                    c.play();
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0x77, 0x77, 0x77, 0xFF);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 0x22, 0x22, 0x22, 0xFF);
        SDL_RenderFillRect(renderer, &ground);

        SDL_Rect srcRect = { 0, 0, 128, 128 };
        SDL_Rect dstRect = { 50, GROUND_LEVEL - 256 , 256, 256 };
        SDL_RenderCopy(renderer, tree, &srcRect, &dstRect);

        drawText(framesNow, 0, 0, 0.5f, { 255, 255, 255, 255 });

        SDL_RenderPresent(renderer);


    }
    
    exit();

    return 0;
}

void drawText(
    std::string text,
    int x, int y,
    float size,
    SDL_Color color)
{
    SDL_Surface* surfaceMessage = TTF_RenderText_Solid(font, text.c_str(), color);
    SDL_Texture* message = SDL_CreateTextureFromSurface(renderer, surfaceMessage);
    SDL_Rect Message_rect;
    Message_rect.x = x;
    Message_rect.y = y;
    Message_rect.w = (int)(surfaceMessage->w * size);
    Message_rect.h = (int)(surfaceMessage->h * size);
    SDL_RenderCopy(renderer, message, NULL, &Message_rect);
    SDL_FreeSurface(surfaceMessage);
    SDL_DestroyTexture(message);
}

bool init() {

    if (SDL_Init(SDL_INIT_VIDEO | SoundEngine::FLAGS) != 0) {
        std::cerr << "SDL initialization failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    if (TTF_Init() == -1) {
        std::cerr << "TTF initialization failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "IMG initialization failed! Error: " << IMG_GetError() << '\n';
        return false;
    }

    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    if (window == nullptr) {
        std::cerr << "Creation of window failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        std::cerr << "Creation of renderer failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    font = TTF_OpenFont("res/orbitron/Orbitron-Regular.ttf", 100);
    if (font == nullptr) {
        std::cerr << "Loading of font failed! Error: " << SDL_GetError() << '\n';
        return false;
    }

    SDL_Surface *loadedSurface = nullptr;
    loadedSurface = IMG_Load("res/tree.png");
    if (loadedSurface == nullptr) {
        std::cout << "Loading tree image failed! Error: " << SDL_GetError() << '\n';
        return false;
    }
    tree = SDL_CreateTextureFromSurface(renderer, loadedSurface);
    SDL_FreeSurface( loadedSurface );

    SoundEngine::init();
    soundboi = SoundEngine::load("res/noice.wav");

    return true;
}

void exit() {
    SDL_DestroyTexture(tree);
    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SoundEngine::exit();

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}
