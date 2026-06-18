#ifndef _GAME_H

#if DEBUG
#define ASSERT(expression) if (!(expression)) { *(int*)0 = 0; }
#else
#define ASSERT(expression)
#endif

#include <myr.h>
#include <myr_math.h>
#include <myr_arena.h>

enum OrbType {
    ORB_NONE = 0,

    ORB_PINK,
    ORB_YELLOW,
    ORB_BLUE,
    ORB_GREEN,

    ORB_MAX
};

static inline Color get_orb_render_color(OrbType o) {
    switch (o) {
        case ORB_PINK: return PINK;
        case ORB_YELLOW: return YELLOW;
        case ORB_BLUE: return BLUE;
        case ORB_GREEN: return GREEN;
        default: return BLACK;
    }
}

typedef u32 OrbId;
#define ORB_ID_NONE 0

struct Orb {
    OrbType type;
    OrbId id;
};

// NOTE: These values are used as direction indices in board traversal.
enum StackDirection {
    STACKDIR_UP = 0,
    STACKDIR_RIGHT,
    STACKDIR_DOWN,
    STACKDIR_LEFT,

    STACKDIR_INVALID
};

static inline StackDirection opposite_dir(StackDirection dir) {
    ASSERT(dir < STACKDIR_INVALID);

    switch (dir) {
        case STACKDIR_UP: return STACKDIR_DOWN;
        case STACKDIR_RIGHT: return STACKDIR_LEFT;
        case STACKDIR_DOWN: return STACKDIR_UP;
        case STACKDIR_LEFT: return STACKDIR_RIGHT;
        default: return STACKDIR_INVALID; // NOTE: Unchecked, maybe worth going INVALID_CODEPATH here?
    }
}

#define PLAYERFIELD_COLS 7
#define PLAYERFIELD_ROWS 14
#define PLAYER_HOLD_SIZE PLAYERFIELD_ROWS / 2
#define TOTAL_BOARD_SIZE PLAYERFIELD_ROWS * PLAYERFIELD_COLS

enum BoardEventType {
    BOARDEVENT_ORB_MOVED,
    BOARDEVENT_ORB_REMOVED,
    BOARDEVENT_ORB_HELD,
    BOARDEVENT_ORB_RELEASED,
};

struct BoardEvent {
    BoardEventType type;
    Orb orb;
    u32 from_col;
    u32 from_row;
    u32 to_col;
    u32 to_row;
};

#define MAX_BOARD_EVENTS 256
struct BoardEventBuffer {
    BoardEvent events[MAX_BOARD_EVENTS];
    u32 count;
};

struct BoardResolveScratch {
    bool just_dropped[TOTAL_BOARD_SIZE];
    bool to_remove[TOTAL_BOARD_SIZE];
    bool visited[TOTAL_BOARD_SIZE];
};

struct PlayerState {
    u32 at_col;

    Orb hold[PLAYER_HOLD_SIZE];
    u32 hold_count;

    Orb board[TOTAL_BOARD_SIZE];
    OrbId next_orb_id;
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
    u32 controller;         // GameControllerInput flags
};

enum GameMode {
    GAMEMODE_GAME,
};

struct GameState {
    b32 initialized;

    GameMode mode;

    PlayerState player_1;
    PlayerState player_2;
};

#define _GAME_H
#endif
