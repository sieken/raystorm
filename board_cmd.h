#ifndef _BOARD_CMD_H
#define _BOARD_CMD_H

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

#endif // _BOARD_CMD_H
