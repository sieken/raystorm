#ifndef _GAME_H

#include <myr_math.h>

#if DEBUG
#define ASSERT(expression) if (!(expression)) { *(int*)0 = 0; }
#else
#define ASSERT(expression)
#endif

enum GameMode {
    GAMEMODE_GAME,
};

#define PLAYERFIELD_COLS 7
#define PLAYERFIELD_STACK_SIZE 14

enum CellState {
    CELL_EMPTY = 0,

    CELL_PINK,
    CELL_YELLOW,
    CELL_BLUE,
    CELL_GREEN,

    CELL_UNDEFINED
};

static inline Color get_cell_render_color(CellState c) {
    switch (c) {
        case CELL_PINK: return PINK;
        case CELL_YELLOW: return YELLOW;
        case CELL_BLUE: return BLUE;
        case CELL_GREEN: return GREEN;
        default: return MAROON;
    }
}

struct CellStack {
    u32 count;
    u32 cap;
    CellState cells[PLAYERFIELD_STACK_SIZE];
};

struct PlayerState {
    u8 at_col;

    u32       hold_count;
    CellState hold_color;

    CellStack field_stacks[PLAYERFIELD_COLS];
};

enum GameControllerInput {
    GAMEINPUT_NONE        = (1U << 0),

    GAMEINPUT_MOV_LEFT    = (1U << 1),
    GAMEINPUT_MOV_RIGHT   = (1U << 2),
    GAMEINPUT_MOV_UP      = (1U << 3),
    GAMEINPUT_MOV_DOWN    = (1U << 4),

    GAMEINPUT_ACT_PRIMARY = (1U << 5)
};

struct GameInput {
    f32 time_delta_seconds;

    u32 controller;         // GameControllerInput flags
};

struct GameState {
    b32 initialized;

    GameMode mode;

    PlayerState player_1;
    PlayerState player_2;
};

#define _GAME_H
#endif
