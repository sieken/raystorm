#include <raylib.h>
#include "game.cpp"

#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

GameState game = {};

int main(int argc, char* argv[]) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "raystorm");

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (game.mode == GAMEMODE_GAME) {
            GameInput new_input = {};

            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_J)) new_input.controller |= GAMEINPUT_MOV_LEFT;
            if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_I)) new_input.controller |= GAMEINPUT_MOV_UP;
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_L)) new_input.controller |= GAMEINPUT_MOV_RIGHT;
            if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_K)) new_input.controller |= GAMEINPUT_MOV_DOWN;

            if (IsKeyPressed(KEY_SPACE)) new_input.controller |= GAMEINPUT_ACT_PRIMARY;

            update_game(&game, &new_input);
        }

        if (game.mode == GAMEMODE_GAME) {
            render_game(&game);
        }
    }

    CloseWindow();

    return 0;
}
