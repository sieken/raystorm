#include <raylib.h>
#include "game.h"

// Render @ 1080p
// #define TARGET_WIDTH  1920
// #define TARGET_HEIGHT 1080
// Run on window dimensions for now
#define TARGET_WIDTH  1280
#define TARGET_HEIGHT 720

#define CLEARCOLOR     LIGHTGRAY
#define DEBUGTEXTCOLOR DARKGRAY

// ---
// myr_math to raylib helpers
// ---

static inline Rectangle make_rect(v2 rect_tl, v2 rect_dim) {
    return (Rectangle){ rect_tl.x, rect_tl.y, rect_dim.x, rect_dim.y };
}

static inline Vector2 make_vector2(f32 x, f32 y) {
    return (Vector2){ x, y };
}

static inline Vector2 make_vector2(v2 v) {
    return (Vector2){ v.x, v.y };
}

struct PlayerFieldLayout {
    v2 field_tl;
    v2 field_wh;
    v2 cell_wh;
    v2 gutter_wh;
    v2 orb_pad;
    v2 orb_dim;
};

struct GameRenderLayout {
    v2 inner_tl;
    v2 inner_wh;
    v2 pf_gap;
    PlayerFieldLayout player_1;
    PlayerFieldLayout player_2;
};

#define LAYOUT_INNER_W       ((f32) TARGET_WIDTH * 0.5f)
#define LAYOUT_INNER_H       ((f32) TARGET_HEIGHT * 0.9f)
#define LAYOUT_INNER_X       (((f32) TARGET_WIDTH - LAYOUT_INNER_W) * 0.5f)
#define LAYOUT_INNER_Y       (((f32) TARGET_HEIGHT - LAYOUT_INNER_H) * 0.5f)
#define LAYOUT_PF_GAP        (LAYOUT_INNER_W * 0.1f)
#define LAYOUT_PF_BOT        40.f
#define LAYOUT_PF_W          ((LAYOUT_INNER_W - LAYOUT_PF_GAP) * 0.5f)
#define LAYOUT_PF_H          (LAYOUT_INNER_H - LAYOUT_PF_BOT)
#define LAYOUT_CELL_WH       (LAYOUT_PF_W / (f32) PLAYERFIELD_COLS)
#define LAYOUT_GUTTER_H      (LAYOUT_PF_H - (LAYOUT_CELL_WH * (f32) PLAYERFIELD_ROWS))
#define LAYOUT_ORB_PAD       4.f
#define LAYOUT_ORB_DIM       (LAYOUT_CELL_WH - (LAYOUT_ORB_PAD * 2.f))
#define LAYOUT_P2_X          (LAYOUT_INNER_X + LAYOUT_PF_GAP + LAYOUT_PF_W)

static const GameRenderLayout GAME_RENDER_LAYOUT = {
    { LAYOUT_INNER_X, LAYOUT_INNER_Y },
    { LAYOUT_INNER_W, LAYOUT_INNER_H },
    { LAYOUT_PF_GAP, 0.f },
    {
        { LAYOUT_INNER_X, LAYOUT_INNER_Y },
        { LAYOUT_PF_W, LAYOUT_PF_H },
        { LAYOUT_CELL_WH, LAYOUT_CELL_WH },
        { LAYOUT_PF_W, LAYOUT_GUTTER_H },
        { LAYOUT_ORB_PAD, LAYOUT_ORB_PAD },
        { LAYOUT_ORB_DIM, LAYOUT_ORB_DIM },
    },
    {
        { LAYOUT_P2_X, LAYOUT_INNER_Y },
        { LAYOUT_PF_W, LAYOUT_PF_H },
        { LAYOUT_CELL_WH, LAYOUT_CELL_WH },
        { LAYOUT_PF_W, LAYOUT_GUTTER_H },
        { LAYOUT_ORB_PAD, LAYOUT_ORB_PAD },
        { LAYOUT_ORB_DIM, LAYOUT_ORB_DIM },
    },
};

static inline v2 board_cell_to_screen(const PlayerFieldLayout *layout, u32 col, u32 row) {
    ASSERT(layout);
    ASSERT(col < PLAYERFIELD_COLS);
    ASSERT(row < PLAYERFIELD_ROWS);

    v2 grid_pos = hadamard(make_v2(col, row), layout->cell_wh);
    return layout->field_tl + grid_pos + layout->orb_pad;
}

// ---
// board state helpers
// ---

static inline u32 board_get_index(u32 x, u32 y) {
    ASSERT(x < PLAYERFIELD_COLS && y < PLAYERFIELD_ROWS);
    return y * PLAYERFIELD_COLS + x;
}

static inline b32 board_in_bounds(i32 x, i32 y) {
    return x >= 0 && x < PLAYERFIELD_COLS && y >= 0 && y < PLAYERFIELD_ROWS;
}

static inline Orb *board_get_cell(PlayerState *p, u32 x, u32 y) {
    return &p->board[board_get_index(x, y)];
}

static inline b32 orb_exists(Orb orb) {
    return orb.type > ORB_NONE;
}

static inline Orb make_orb(PlayerState *p, OrbType type) {
    Orb result = {};
    result.type = type;
    result.id = p->next_orb_id++;
    if (result.id == ORB_ID_NONE) result.id = p->next_orb_id++;
    return result;
}

static void push_board_event(BoardEventBuffer *events, BoardEvent event) {
    if (!events) return;
    ASSERT(events->count < MAX_BOARD_EVENTS);
    events->events[events->count++] = event;
}

static inline u32 column_height(PlayerState *p, u32 col) {
    ASSERT(col < PLAYERFIELD_COLS);

    u32 count = 0;
    while (count < PLAYERFIELD_ROWS) {
        Orb *cell = board_get_cell(p, col, count);
        if (!orb_exists(*cell)) break;
        count++;
    }

    return count;
}

static inline i32 column_top_row(PlayerState *p, u32 col) {
    u32 height = column_height(p, col);
    return height > 0 ? (i32) height - 1 : -1;
}

static inline Orb column_pop(PlayerState *p, u32 col) {
    i32 top_row = column_top_row(p, col);
    ASSERT(top_row >= 0);

    Orb *cell = board_get_cell(p, col, (u32) top_row);
    Orb result = *cell;
    *cell = {};
    return result;
}

static inline u32 column_push(PlayerState *p, u32 col, Orb orb) {
    u32 dst_row = column_height(p, col);
    ASSERT(dst_row < PLAYERFIELD_ROWS);

    Orb *dst = board_get_cell(p, col, dst_row);
    ASSERT(!orb_exists(*dst));

    *dst = orb;
    return dst_row;
}

static void attempt_to_pull_stack(PlayerState *p, BoardEventBuffer *events) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);

    u32 height = column_height(p, p->at_col);
    if (height < 1) return; // Nothing to pull

    OrbType ref;
    if (p->hold_count > 0) ref = p->hold[p->hold_count - 1].type;
    else                   ref = board_get_cell(p, p->at_col, height - 1)->type;

    // Count how many to pull
    u32 pull_count = 0;
    i32 top_row = (i32) height - 1;
    while (top_row >= 0 && board_get_cell(p, p->at_col, (u32) top_row)->type == ref) {
        pull_count++;
        top_row--;
    }

    // If we can fit the stack...
    if (p->hold_count + pull_count <= PLAYER_HOLD_SIZE) {
        // ... We pull the stack
        for (u32 i = 0; i < pull_count; ++i) {
            u32 from_row = (u32) column_top_row(p, p->at_col);
            Orb o = column_pop(p, p->at_col);
            p->hold[p->hold_count++] = o;

            BoardEvent event = {};
            event.type = BOARDEVENT_ORB_HELD;
            event.orb = o;
            event.from_col = p->at_col;
            event.from_row = from_row;
            event.to_col = p->at_col;
            event.to_row = PLAYERFIELD_ROWS;
            push_board_event(events, event);
        }
    }
}

static void mark_matching_recursive_depth_first(PlayerState *p, BoardResolveScratch *scratch, u32 col, u32 row, OrbType type, StackDirection from_dir) {
    // NULL or empty, or incorrect type should stop recursion
    if (!board_in_bounds((i32) col, (i32) row)) return;

    u32 index = board_get_index(col, row);
    Orb *cell = &p->board[index];
    if (cell->type == ORB_NONE || cell->type != type) return;

    // Do not visit already checked cells, otherwise recursion can loop through connected regions.
    if (scratch->visited[index]) return;

    scratch->visited[index] = true;
    scratch->to_remove[index] = true;

    // Recurse over all adjacent cells except the origin direction.
    for (u32 i = 0; i < STACKDIR_INVALID; ++i) {
        if (i == from_dir) continue;

        i32 next_col = (i32) col;
        i32 next_row = (i32) row;
        switch ((StackDirection) i) {
            case STACKDIR_UP:    next_row--; break;
            case STACKDIR_RIGHT: next_col++; break;
            case STACKDIR_DOWN:  next_row++; break;
            case STACKDIR_LEFT:  next_col--; break;
            default: break;
        }

        if (!board_in_bounds(next_col, next_row)) continue;
        mark_matching_recursive_depth_first(p, scratch, (u32) next_col, (u32) next_row, type, opposite_dir((StackDirection) i));
    }
}

static inline bool should_trigger_chain(PlayerState *p, BoardResolveScratch *scratch, u32 col, u32 row) {
    Orb *cell = board_get_cell(p, col, row);
    if (cell->type == ORB_NONE) return false;

    // Check neighbors
    for (u32 i = 0; i < STACKDIR_INVALID; ++i) {
        i32 next_col = (i32) col;
        i32 next_row = (i32) row;
        switch ((StackDirection) i) {
            case STACKDIR_UP:    next_row--; break;
            case STACKDIR_RIGHT: next_col++; break;
            case STACKDIR_DOWN:  next_row++; break;
            case STACKDIR_LEFT:  next_col--; break;
            default: break;
        }

        if (!board_in_bounds(next_col, next_row)) continue;

        u32 neighbor_index = board_get_index((u32) next_col, (u32) next_row);
        Orb *neighbor = &p->board[neighbor_index];

        // Should trigger a chain reaction if neighbor wasn't placed just now, and it is of matching type
        if (!scratch->just_dropped[neighbor_index] && neighbor->type == cell->type) return true;
    }

    return false;
}

static void attempt_to_push_stack(PlayerState *p, BoardResolveScratch *scratch, BoardEventBuffer *events) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);
    if (p->hold_count < 1) return; // Nothing to push

    u32 height = column_height(p, p->at_col);
    if (height + p->hold_count > PLAYERFIELD_ROWS) return; // Can't exceed column stack max size

    OrbType ref = p->hold[p->hold_count - 1].type;

    bool trigger_chain = false;
    u32 last_row = 0;
    while (p->hold_count > 0) {
        Orb o = p->hold[--p->hold_count];
        u32 dst_row = column_push(p, p->at_col, o);
        last_row = dst_row;

        scratch->just_dropped[board_get_index(p->at_col, dst_row)] = true;
        if (!trigger_chain) trigger_chain = should_trigger_chain(p, scratch, p->at_col, dst_row);

        BoardEvent event = {};
        event.type = BOARDEVENT_ORB_RELEASED;
        event.orb = o;
        event.from_col = p->at_col;
        event.from_row = PLAYERFIELD_ROWS;
        event.to_col = p->at_col;
        event.to_row = dst_row;
        push_board_event(events, event);
    }

    if (trigger_chain) {
        // Come at the nodes from the stack top (i.e. visually below), meaning
        // that the initial one should ignore going down
        StackDirection initial_from_dir = STACKDIR_DOWN;
        mark_matching_recursive_depth_first(p, scratch, p->at_col, last_row, ref, initial_from_dir);
    }
}

static inline OrbType random_orb(void)  {
    return (OrbType) GetRandomValue(ORB_NONE + 1, ORB_MAX - 1);
}

static inline void populate_random_rows(PlayerState *p, u32 rows) {
    for (u32 row = 0; row < rows; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            Orb *cell = board_get_cell(p, col, row);
            *cell = make_orb(p, random_orb());
        }
    }
}

static void apply_board_gravity(PlayerState *p, BoardEventBuffer *events) {
    for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
        u32 write_row = 0;

        for (u32 read_row = 0; read_row < PLAYERFIELD_ROWS; ++read_row) {
            Orb *src = board_get_cell(p, col, read_row);
            if (!orb_exists(*src)) continue;

            Orb *dst = board_get_cell(p, col, write_row);
            if (read_row != write_row) {
                Orb moved_orb = *src;
                *dst = *src;
                *src = {};

                BoardEvent event = {};
                event.type = BOARDEVENT_ORB_MOVED;
                event.orb = moved_orb;
                event.from_col = col;
                event.from_row = read_row;
                event.to_col = col;
                event.to_row = write_row;
                push_board_event(events, event);
            }

            write_row++;
        }
    }
}

static void update_board(PlayerState *p, BoardResolveScratch *scratch, BoardEventBuffer *events) {
    for (u32 i = 0; i < TOTAL_BOARD_SIZE; ++i) {
        if (!scratch->to_remove[i]) continue;

        Orb *cell = &p->board[i];
        if (!orb_exists(*cell)) continue;

        BoardEvent event = {};
        event.type = BOARDEVENT_ORB_REMOVED;
        event.orb = *cell;
        event.from_col = i % PLAYERFIELD_COLS;
        event.from_row = i / PLAYERFIELD_COLS;
        event.to_col = event.from_col;
        event.to_row = event.from_row;
        push_board_event(events, event);

        *cell = {};
    }

    apply_board_gravity(p, events);
}

static void draw_orb_rect(v2 pos, v2 dim, OrbType type) {
    DrawRectangleRec(make_rect(pos, dim), get_orb_render_color(type));
}

static void draw_player_field_grid(const PlayerFieldLayout *layout) {
    for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
        v2 row_offset = make_v2(0, (f32) (row + 1) * layout->cell_wh.y);
        v2 len = make_v2(layout->field_wh.x, 0);
        DrawLineEx(make_vector2(layout->field_tl + row_offset), make_vector2(layout->field_tl + row_offset + len), 1, GRAY);
    }

    for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
        v2 col_offset = make_v2((f32) (col + 1) * layout->cell_wh.x, 0);
        v2 height = make_v2(0, layout->field_wh.y);
        DrawLineEx(make_vector2(layout->field_tl + col_offset), make_vector2(layout->field_tl + col_offset + height), 1, GRAY);
    }

    v2 gutter_offset = make_v2(0, layout->field_wh.y - layout->gutter_wh.y) + make_v2(1, 1);
    v2 gutter_dim = layout->gutter_wh - make_v2(2, 2);
    DrawRectangleRec(make_rect(layout->field_tl + gutter_offset, gutter_dim), GRAY);
}

static void draw_player_field_border(const PlayerFieldLayout *layout) {
    DrawRectangleLinesEx(make_rect(layout->field_tl, layout->field_wh), 1, DARKBLUE);
}

static void draw_player_cursor(PlayerState *player, const PlayerFieldLayout *layout) {
    f32 gutter_offset = layout->field_wh.y - layout->gutter_wh.y;
    v2 center = layout->cell_wh * 0.5f;
    v2 cursor_dim = make_v2(10, 10);
    v2 cursor_pos = layout->field_tl + make_v2(player->at_col * layout->cell_wh.x, gutter_offset) + center - (cursor_dim * 0.5f);

    DrawRectangleRec(make_rect(cursor_pos, cursor_dim), ORANGE);
}

static void draw_player_board(PlayerState *player, const PlayerFieldLayout *layout) {
    for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            Orb *orb = board_get_cell(player, col, row);
            if (!orb_exists(*orb)) continue;

            draw_orb_rect(board_cell_to_screen(layout, col, row), layout->orb_dim, orb->type);
        }
    }
}

static void draw_player_hold(PlayerState *player, const PlayerFieldLayout *layout) {
    v2 padding = make_v2(10.f, 0.f);
    v2 stack_pos = layout->field_tl + make_v2(0.f, layout->field_wh.y) - padding;
    v2 orb_dim = make_v2(10.f, 10.f);
    f32 gap = 5.f;

    for (u32 i = 0; i < player->hold_count; ++i) {
        v2 orb_offset = make_v2(-orb_dim.x, -orb_dim.y * i - gap);
        DrawRectangleRec(make_rect(stack_pos + orb_offset, orb_dim), get_orb_render_color(player->hold[i].type));
    }
}

static void draw_player_field(PlayerState *player, const PlayerFieldLayout *layout) {
    draw_player_field_grid(layout);
    draw_player_field_border(layout);
    draw_player_cursor(player, layout);
    draw_player_board(player, layout);
    draw_player_hold(player, layout);
}

// ---
// player state stuff
// ---

static inline void clear_player_state(PlayerState *p) {
    // Clear everything
    memset(p, 0, sizeof(PlayerState));

    // Set initial position to be middle col
    p->at_col = (u8) PLAYERFIELD_COLS / 2;
    p->next_orb_id = 1;
}

static void update_game_simulation(GameState *game, GameInput *new_input,
                                   BoardEventBuffer *p1_events, BoardEventBuffer *p2_events) {
    if (!game->initialized) {
        clear_player_state(&game->player_1);
        clear_player_state(&game->player_2);

        populate_random_rows(&game->player_1, 4);
        populate_random_rows(&game->player_2, 4);

        game->initialized = true;
    }

    BoardResolveScratch p1_scratch = {};
    BoardResolveScratch p2_scratch = {};

    PlayerState *p1 = &game->player_1;

    u32 controller = new_input->controller;

    // Movement
    i8 player_at = p1->at_col;
    if (controller & GAMEINPUT_MOV_LEFT)  player_at -= 1;
    if (controller & GAMEINPUT_MOV_RIGHT) player_at += 1;

    if (player_at < 0)                 player_at = 0;
    if (player_at >= PLAYERFIELD_COLS) player_at = PLAYERFIELD_COLS - 1;

    // TODO: Generate cursor move event into p1_events
    p1->at_col = player_at;

    // Stack interaction
    if (controller & GAMEINPUT_MOV_DOWN) attempt_to_pull_stack(p1, p1_events);
    if (controller & GAMEINPUT_MOV_UP)   attempt_to_push_stack(p1, &p1_scratch, p1_events);

    // Update board and generate events
    update_board(&game->player_1, &p1_scratch, p1_events);
    update_board(&game->player_2, &p2_scratch, p2_events);
}

static void update_game(GameState *game, GameInput *new_input) {
    BoardEventBuffer p1_events = {};
    BoardEventBuffer p2_events = {};

    update_game_simulation(game, new_input, &p1_events, &p2_events);
}

static void render_game(GameState *game) {
    BeginDrawing();

    ClearBackground(CLEARCOLOR);

    const GameRenderLayout *layout = &GAME_RENDER_LAYOUT;
    draw_player_field(&game->player_1, &layout->player_1);
    draw_player_field(&game->player_2, &layout->player_2);

    EndDrawing();
}
