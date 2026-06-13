#ifndef _GAME_H

#include <myr_math.h>

#if DEBUG
#define ASSERT(expression) if (!(expression)) { *(int*)0 = 0; }
#else
#define ASSERT(expression)
#endif

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

enum OrbFlag {
    ORB_JUST_DROPPED = (1U << 0),
    ORB_MOVING       = (1U << 1),
};

struct Orb {
    u64 flags;
    OrbType type;
};

static inline b32 orb_has_flags(Orb *o, u64 flags) {
    ASSERT(o && flags); // Treat 0 flag as invalid
    return (o->flags & flags) == flags;
}

#define ORB_CLEAR_FLAGS(optr, f) ((optr)->flags &= ~((u64) (f)))

struct GridOrb {
    Orb orb;

    bool marked;
    u32  move_from_row;

    // Board neighbors
    union {
        struct {
            GridOrb *neighbor_up;
            GridOrb *neighbor_right;
            GridOrb *neighbor_down;
            GridOrb *neighbor_left;
        };
        GridOrb* neighbor[4];
    };
};

// NOTE: These need to match the order of neighbor links
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
struct PlayerState {
    u32 at_col;

    u32     hold_count;
    OrbType hold_type;
    Orb     hold[PLAYER_HOLD_SIZE];

    GridOrb board[TOTAL_BOARD_SIZE];
};

static inline u32 board_get_index(u32 x, u32 y) {
    ASSERT(x < PLAYERFIELD_COLS && y < PLAYERFIELD_ROWS);
    return y * PLAYERFIELD_COLS + x;
}

struct StackView {
    GridOrb *start;
    GridOrb *top;
    StackDirection dir;
    u32 count;
    u32 max;
};

static inline StackView make_stack_view(GridOrb *start, StackDirection dir, u32 max = PLAYERFIELD_ROWS) {
    StackView result = {};

    GridOrb *prev = 0;
    GridOrb *o    = start;

    // Build stack info
    u32 count = 0;
    while (o && o->orb.type > ORB_NONE && count < max) {
        prev = o;
        o    = o->neighbor[dir];
        count++;
    }

    result.start = start;
    result.top = prev;
    result.count = count;
    result.max = max;
    result.dir = dir;

    return result;
}

static inline Orb stack_view_pop(StackView *s) {
    ASSERT(s->count > 0 && s->top);

    GridOrb result = *s->top;
    s->top = s->top->neighbor[opposite_dir(s->dir)];
    s->count--;
    return result.orb;
}

static inline Orb stack_view_pop_commit(StackView *s) {
    ASSERT(s->count > 0 && s->top);

    Orb result = s->top->orb;
    s->top->orb = {};
    s->top = s->top->neighbor[opposite_dir(s->dir)];
    s->count--;
    return result;
}

static inline void stack_view_push_commit(StackView *s, Orb orb) {
    ASSERT(s->count < s->max);

    GridOrb *dst = s->top ? s->top->neighbor[s->dir] : s->start;
    ASSERT(dst);

    dst->orb = orb;
    s->top = dst;

    s->count++;
}

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
