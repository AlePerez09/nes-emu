// SDL2 frontend: window, input, and presenting the PPU framebuffer.
// Keys: arrows = D-pad, Z = B, X = A, Enter = Start, Right Shift = Select,
//       Tab = toggle pattern-table debug view, Esc = quit.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include <cstdio>
#include <exception>

#include "core/nes.h"

namespace {
uint8_t read_buttons(const Uint8* keys) {
    uint8_t b = 0;
    if (keys[SDL_SCANCODE_X])      b |= 1 << 0;  // A
    if (keys[SDL_SCANCODE_Z])      b |= 1 << 1;  // B
    if (keys[SDL_SCANCODE_RSHIFT]) b |= 1 << 2;  // Select
    if (keys[SDL_SCANCODE_RETURN]) b |= 1 << 3;  // Start
    if (keys[SDL_SCANCODE_UP])     b |= 1 << 4;
    if (keys[SDL_SCANCODE_DOWN])   b |= 1 << 5;
    if (keys[SDL_SCANCODE_LEFT])   b |= 1 << 6;
    if (keys[SDL_SCANCODE_RIGHT])  b |= 1 << 7;
    return b;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s game.nes\n", argv[0]);
        return 2;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int exit_code = 0;
    try {
        Nes nes(argv[1]);

        constexpr int kScale = 3;
        SDL_Window* window = SDL_CreateWindow("nes-emu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                              Ppu::kWidth * kScale, Ppu::kHeight * kScale, 0);
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                 Ppu::kWidth, Ppu::kHeight);

        bool running = true;
        bool debug_patterns = true;  // on by default until the real renderer exists
        bool reported_halt = false;

        while (running) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) running = false;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
                if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB) debug_patterns = !debug_patterns;
            }

            nes.bus.set_controller(0, read_buttons(SDL_GetKeyboardState(nullptr)));
            nes.run_frame();

            if (nes.cpu.halted() && !reported_halt) {
                std::printf("CPU halted at $%04X: %s\n", nes.cpu.pc, nes.cpu.halt_reason().c_str());
                reported_halt = true;
            }

            if (debug_patterns) nes.ppu.render_pattern_tables_debug();
            SDL_UpdateTexture(texture, nullptr, nes.ppu.framebuffer().data(), Ppu::kWidth * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        exit_code = 1;
    }

    SDL_Quit();
    return exit_code;
}
