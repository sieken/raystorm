#include <raylib.h>
#include "game.cpp"

#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

GameState game = {};

int main(int argc, char* argv[]) {
    ThreadContext *thread_ctx = thread_ctx_alloc();
    ctx_select(thread_ctx);

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "raystorm");

    SetTargetFPS(60);

    game.target_tick_rate = 60;

    EventLog events = { .arena = arena_alloc(MB(1), KB(10)), .events = 0, .first_free = 0 };

    game.debugfont = LoadFont("/home/sieken/gamedev/chainstorm/assets/fonts/Albura-Regular.ttf");

    while (!WindowShouldClose()) {
        f32 frame_delta = GetFrameTime();

        if (game.mode == GAMEMODE_GAME) {
            GameInput new_input = {};

            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_J)) new_input.controller |= GAMEINPUT_MOV_LEFT;
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_I)) new_input.controller |= GAMEINPUT_MOV_UP;
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_L)) new_input.controller |= GAMEINPUT_MOV_RIGHT;
            if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_K)) new_input.controller |= GAMEINPUT_MOV_DOWN;

            if (IsKeyPressed(KEY_SPACE)) new_input.controller |= GAMEINPUT_ACT_PRIMARY;

            update_game(&game, &new_input, &events);
        }

        update_events(&events, frame_delta);

        if (game.mode == GAMEMODE_GAME) {
            render_game(&game, &events);
        }
    }

    CloseWindow();

    thread_ctx_release(thread_ctx);

    return 0;
}
