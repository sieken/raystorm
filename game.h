#ifndef _GAME_H

#if DEBUG
#define ASSERT(expression) if (!(expression)) { *(int*)0 = 0; }
#else
#define ASSERT(expression)
#endif

#include <myr.h>
#include <myr_math.h>
#include <myr_arena.h>

typedef u32 OrbId;
#define ORB_ID_NONE 0

enum BoardCommandType {
    BOARDCMD_MOVE_CURSOR_LEFT,
    BOARDCMD_MOVE_CURSOR_RIGHT,
    BOARDCMD_PULL_STACK,
    BOARDCMD_PUSH_STACK
};

typedef u64 PlayerId;

#define PLAYER_1_ID    123
#define PLAYER_2_ID    456

struct BoardCommand {
    BoardCommandType type;
    PlayerId player_id;
    u64 cmd_seq;
};

enum BoardEventType {
    BOARDEVENT_ORB_MOVED,
    BOARDEVENT_ORB_REMOVED,
    BOARDEVENT_ORB_HELD,
    BOARDEVENT_ORB_RELEASED,
    BOARDEVENT_ORB_DEADZONED,
    BOARDEVENT_ORB_SPAWNED,

    BOARDEVENT_CURSOR_MOVED,

    BOARDEVENT_PLAYER_ELIMINATED,

    BOARDEVENT_GAME_OVER
};

enum OrbType {
    ORB_NONE = 0,

    ORB_PINK,
    ORB_YELLOW,
    ORB_BLUE,
    ORB_GREEN,

    ORB_MAX
};

enum Player {
    PLAYER1 = 0,
    PLAYER2,

    MAX_PLAYER_COUNT
};

struct GameResult {
    b32 game_over;
    b32 is_draw;
    u32 winner_count;
    PlayerId winners[MAX_PLAYER_COUNT];
};

struct BoardEvent {
    BoardEventType type;
    PlayerId player_id;
    u64 event_seq;

    OrbId   orb_id;
    OrbType orb_type;

    u32 from_col;
    u32 from_row;
    u32 to_col;
    u32 to_row;

    GameResult result;
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

#define MAX_BOARD_EVENTS 256
struct BoardEventBuffer {
    u64 tick;
    BoardEvent events[MAX_BOARD_EVENTS];
    u32 count;
};

// TODO: These should probably be unbounded
#define MAX_CLEAR_GROUPS_PER_STEP 32
#define MAX_REACTION_STEPS        16
struct ClearGroup {
    OrbType type;
    u32 count;
    u32 cells[TOTAL_BOARD_SIZE];
};
struct ReactionStep {
    u32 clear_group_count;
    ClearGroup clear_groups[MAX_CLEAR_GROUPS_PER_STEP];
};
struct ComboResolution {
    PlayerId player;
    u32 reaction_step_count;
    ReactionStep steps[MAX_REACTION_STEPS];

    u32 total_orbs_cleared;
    u32 total_clear_groups;
    u32 largest_group;
};

struct BoardResolveScratch {
    bool candidates[TOTAL_BOARD_SIZE];
    bool to_remove[TOTAL_BOARD_SIZE];
    bool visited[TOTAL_BOARD_SIZE];
    ComboResolution combo;
};

struct GameTickScratch {
    BoardResolveScratch players[MAX_PLAYER_COUNT];
};

#define MAX_BOARD_COMMANDS 256
struct BoardCommandBuffer {
    PlayerId id;
    u64 tick;

    u32 count;
    BoardCommand buffer[MAX_BOARD_COMMANDS];
};

enum PlayerCondition {
    PLAYER_INACTIVE = 0,
    PLAYER_ACTIVE,
    PLAYER_ELIMINATED,
};

#define PLAYER_SPECIAL_MAX 24
#define PLAYER_ULTRA_MAX   24
struct PlayerState {
    PlayerId id;

    PlayerCondition condition;

    u32 at_col;

    u32 special_charge;
    u32 ultra_charge;

    Orb hold[PLAYER_HOLD_SIZE];
    u32 hold_count;

    Orb board[TOTAL_BOARD_SIZE];
    Orb deadzone[PLAYERFIELD_COLS];
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

struct BoardControl {
    OrbId next_orb_id;

    u64 random_spawn_tick_interval;
    u64 next_random_spawn_tick;
};

struct BoardState {
    u32 player_count;
    PlayerState players[MAX_PLAYER_COUNT];
};

typedef BoardState BoardSnapshot;

enum BoardEffectType {
    BOARDEFFECT_NONE = 0,
    BOARDEFFECT_SPAWN_RANDOM_ROW,
};

struct BoardEffect {
    u64 target_tick;
    BoardEffectType type;
    Player target_player;
};

#define MAX_PENDING_EFFECTS    64
#define MAX_SCHEDULED_EFFECTS 256
struct GameServer {
    GameResult result;
    BoardControl control;
    BoardState boardstate;

    u32 pending_effects_count;
    BoardEffect pending_effects[MAX_PENDING_EFFECTS];
    u32 scheduled_effects_count;
    BoardEffect scheduled_effects[MAX_SCHEDULED_EFFECTS];
};

struct GameClient {
    GameResult result;
    BoardState boardstate;
};

struct GameState {
    b32 initialized;
    GameMode mode;

    Font debugfont;

    u64 target_tick_rate; // Ticks per second
    u64 tick;

    GameServer server;
    GameClient client;
};

static inline u64 estimate_ticks_for_duration(f32 duration_s, u64 target_ticks_per_s) {
    // Round up to cover the requested duration
    return duration_s < 0 ? 0 : (u64) ((duration_s * target_ticks_per_s) + 0.5f);
}

#define EVENT_TIMEOUT    5.f
#define EVENT_LENGTH_MAX 256
struct Event {
    Event *next;

    f32 time_left;

    u32  length;
    char buf[EVENT_LENGTH_MAX];
};

struct EventLog {
    Arena *arena;

    Event *events;
    Event *first_free;
};

static inline void event_push(EventLog *log, const char *msg, u32 length) {
    ASSERT(length < EVENT_LENGTH_MAX);

    Event *new_msg = 0;

    if (log->first_free) {
        new_msg = log->first_free;
        log->first_free = new_msg->next;
        memzero_struct(new_msg);
    } else {
        new_msg = push_struct_zero(log->arena, Event);
    }

    if (!new_msg) return;

    memcpy(new_msg->buf, msg, length);
    new_msg->length = length;
    new_msg->time_left = EVENT_TIMEOUT;

    new_msg->next = log->events;
    log->events = new_msg;
}

static inline void update_events(EventLog *log, f32 dt) {
    ASSERT(log);

    Event **indirect = &log->events;

    while (*indirect) {
        Event *event = *indirect;

        event->time_left -= dt;

        if (event->time_left <= 0) {
            *indirect = event->next;
            event->next = log->first_free;
            log->first_free = event;
        } else {
            indirect = &event->next;
        }
    }
}

#define _GAME_H
#endif
