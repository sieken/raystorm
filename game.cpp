#include <raylib.h>
#include <stdio.h> // Used for snprintf, replace with own string lib later
#include "game.h"

// Render @ 1080p
// #define TARGET_WIDTH  1920
// #define TARGET_HEIGHT 1080
// Run on window dimensions for now
#define TARGET_WIDTH  1280
#define TARGET_HEIGHT 720

#define CLEARCOLOR     LIGHTGRAY
#define DEBUGTEXTCOLOR DARKGRAY

struct ThreadContext {
    Arena *scratch[2];
};

__thread ThreadContext* thread_local_context;

static ThreadContext* thread_ctx_alloc(void) {
    Arena *arena_0 = arena_alloc();
    Arena *arena_1 = arena_alloc();

    ThreadContext *ctx = push_struct(arena_0, ThreadContext);
    ctx->scratch[0] = arena_0;
    ctx->scratch[1] = arena_1;

    return ctx;
}

static void thread_ctx_release(ThreadContext *ctx) {
    arena_release(ctx->scratch[1]);
    arena_release(ctx->scratch[0]); // This one contains the ThreadContext header, release last
}

static void ctx_select(ThreadContext *ctx) {
    thread_local_context = ctx;
}

static ThreadContext* ctx_selected(void) {
    return thread_local_context;
}

// Conflicts is a list of pointers to arenas, count is the number of elements in this list
// Returns first available arena of the current thread context that does not conflict with
// the provided list of conflict arenas, or NULL if none could be found
static Arena* ctx_get_scratch(Arena **conflicts, u64 count) {
    ThreadContext *ctx = ctx_selected();

    Arena *result = 0;
    Arena **arena_ptr = ctx->scratch;

    for (u64 i = 0; i < ARRAY_COUNT(ctx->scratch); i += 1, arena_ptr += 1) {
        Arena **conflict_ptr = conflicts;

        b32 has_conflict = 0;
        for (u64 j = 0; j < count; j += 1, conflict_ptr += 1) {
            if (*arena_ptr == *conflict_ptr) {
                has_conflict = true;
                break;
            }
        }

        if (!has_conflict) {
            result = *arena_ptr;
            break;
        }
    }

    return result;
}

#define temp_begin(conflicts, count) temp_begin(ctx_get_scratch((conflicts), (count)))

__thread u64 cmd_seq = 0;
__thread u64 event_seq = 0;

#define cmd_seq_next() (cmd_seq++)
#define event_seq_next() (event_seq++)

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

static inline b32 orb_exists(Orb *orb) {
    ASSERT(orb != 0);
    return orb_exists(*orb);
}

static inline Orb make_orb(OrbId id, OrbType type) {
    ASSERT(id != ORB_ID_NONE);

    Orb result = {};
    result.type = type;
    result.id = id;

    return result;
}

static void push_board_event(BoardEventBuffer *events, PlayerState *player, BoardEvent event) {
    if (!events) return;

    ASSERT(events->count < MAX_BOARD_EVENTS);
    ASSERT(player);

    event.player_id = player->id;
    event.event_seq = event_seq_next();
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

static void attempt_to_pull_stack(BoardEventBuffer *events, PlayerState *p) {
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
            event.orb_id = o.id;
            event.from_col = p->at_col;
            event.from_row = from_row;
            event.to_col = p->at_col;
            event.to_row = PLAYERFIELD_ROWS;
            push_board_event(events, p, event);
        }
    }
}

static void attempt_to_push_stack(BoardEventBuffer *events, PlayerState *p, BoardResolveScratch *scratch) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);
    if (p->hold_count < 1) return; // Nothing to push

    u32 height = column_height(p, p->at_col);
    if (height + p->hold_count > PLAYERFIELD_ROWS) return; // Can't exceed column stack max size

    while (p->hold_count > 0) {
        Orb o = p->hold[--p->hold_count];
        u32 dst_row = column_push(p, p->at_col, o);

        scratch->candidates[board_get_index(p->at_col, dst_row)] = true;

        BoardEvent event = {};
        event.type = BOARDEVENT_ORB_RELEASED;
        event.orb_id = o.id;
        event.from_col = p->at_col;
        event.from_row = PLAYERFIELD_ROWS;
        event.to_col = p->at_col;
        event.to_row = dst_row;
        push_board_event(events, p, event);
    }
}

static inline OrbType random_orb(void)  {
    return (OrbType) GetRandomValue(ORB_NONE + 1, ORB_MAX - 1);
}

// TODO: This function is starting to get kind of stupid,
//       please remove and replace with something better
//       for initialization
static inline void populate_random_rows(GameServer *server, Player player, u32 rows) {
    ASSERT(server);
    ASSERT(player < MAX_PLAYER_COUNT);

    BoardControl *control = &server->control;
    PlayerState *ps = &server->boardstate.players[player];

    for (u32 row = 0; row < rows; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            Orb *cell = board_get_cell(ps, col, row);
            *cell = make_orb(control->next_orb_id++, random_orb());
        }
    }
}

static inline void clear_reaction_marks(BoardResolveScratch *scratch) {
    memzero(scratch->visited, sizeof(scratch->visited));
    memzero(scratch->to_remove, sizeof(scratch->to_remove));
}

static inline void clear_reaction_candidates(BoardResolveScratch *scratch) {
    memzero(scratch->candidates, sizeof(scratch->candidates));
}

static inline void combo_record_step(ComboResolution *combo, ReactionStep *step) {
    ASSERT(combo->reaction_step_count < MAX_REACTION_STEPS);

    combo->steps[combo->reaction_step_count++] = *step;

    for (u32 i = 0; i < step->clear_group_count; ++i) {
        ClearGroup *group = &step->clear_groups[i];
        combo->total_clear_groups++;
        combo->total_orbs_cleared += group->count;
        if (combo->largest_group < group->count) combo->largest_group = group->count;
    }
}

static void collect_matching_group(PlayerState *p, BoardResolveScratch *scratch, u32 start_index, ClearGroup *group, bool *has_non_candidate) {
    ASSERT(start_index < TOTAL_BOARD_SIZE);
    ASSERT(group);
    ASSERT(has_non_candidate);

    OrbType type = p->board[start_index].type;
    if (type == ORB_NONE) return;

    u32 stack[TOTAL_BOARD_SIZE] = {};
    u32 stack_count = 0;
    stack[stack_count++] = start_index;

    // Evaluate all entries in the stack until stack is empty
    while (stack_count > 0) {
        u32 index = stack[--stack_count];
        if (scratch->visited[index]) continue; // We have already evaluated this

        u32 col = index % PLAYERFIELD_COLS;
        u32 row = index / PLAYERFIELD_COLS;
        Orb *cell = board_get_cell(p, col, row);
        if (cell->type != type) continue;     // Non-matching type, do not include in group

        scratch->visited[index] = true;

        ASSERT(group->count < TOTAL_BOARD_SIZE);
        group->cells[group->count++] = index;
        group->type = type;

        if (!scratch->candidates[index]) *has_non_candidate = true;

        // Chains trigger in the cardinal directions, check all neighbors
        for (u32 dir = 0; dir < STACKDIR_INVALID; ++dir) {
            i32 next_col = (i32) col;
            i32 next_row = (i32) row;

            switch ((StackDirection) dir) {
                case STACKDIR_UP:    next_row--; break;
                case STACKDIR_RIGHT: next_col++; break;
                case STACKDIR_DOWN:  next_row++; break;
                case STACKDIR_LEFT:  next_col--; break;
                default: break;
            }

            if (!board_in_bounds(next_col, next_row)) continue;

            u32 next_index = board_get_index((u32) next_col, (u32) next_row);
            if (scratch->visited[next_index]) continue;
            if (p->board[next_index].type != type) continue;

            ASSERT(stack_count < TOTAL_BOARD_SIZE);
            stack[stack_count++] = next_index; // Add neighbor to stack for evaluation
        }
    }
}

static bool find_reaction_step_from_candidates(PlayerState *p, BoardResolveScratch *scratch, ReactionStep *step) {
    ASSERT(p && scratch && step);

    clear_reaction_marks(scratch);
    *step = {};

    for (u32 i = 0; i < TOTAL_BOARD_SIZE; ++i) {
        // Only check unvisited, existing candidates
        if (scratch->visited[i]
            || !orb_exists(p->board[i])
            || !scratch->candidates[i]) continue;

        // Start of new clear group
        ClearGroup group = {};
        bool has_non_candidate = false;
        collect_matching_group(p, scratch, i, &group, &has_non_candidate);

        // Valid groups/chain reactions have non-candidate entries
        if (group.count > 1 && has_non_candidate) {
            ASSERT(step->clear_group_count < MAX_CLEAR_GROUPS_PER_STEP);
            step->clear_groups[step->clear_group_count++] = group;

            for (u32 cell_index = 0; cell_index < group.count; ++cell_index) {
                scratch->to_remove[group.cells[cell_index]] = true;
            }
        }
    }

    return step->clear_group_count > 0;
}

static void clear_reaction_step_orbs(PlayerState *p, ReactionStep *step, BoardEventBuffer *events) {
    for (u32 group_index = 0; group_index < step->clear_group_count; ++group_index) {
        ClearGroup *group = &step->clear_groups[group_index];

        for (u32 cell_index = 0; cell_index < group->count; ++cell_index) {
            u32 index = group->cells[cell_index];
            Orb *cell = &p->board[index];
            if (!orb_exists(*cell)) continue;

            BoardEvent event = {};
            event.type = BOARDEVENT_ORB_REMOVED;
            event.orb_id = cell->id;
            event.from_col = index % PLAYERFIELD_COLS;
            event.from_row = index / PLAYERFIELD_COLS;
            event.to_col = event.from_col;
            event.to_row = event.from_row;
            push_board_event(events, p, event);

            *cell = {};
        }
    }
}

static void apply_board_gravity(PlayerState *p, BoardResolveScratch *scratch, BoardEventBuffer *events) {
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
                event.orb_id = moved_orb.id;
                event.from_col = col;
                event.from_row = read_row;
                event.to_col = col;
                event.to_row = write_row;
                push_board_event(events, p, event);

                scratch->candidates[board_get_index(col, write_row)] = true;
            }

            write_row++;
        }
    }
}

static void resolve_board_reactions(PlayerState *p, BoardResolveScratch *scratch, BoardEventBuffer *events) {
    scratch->combo.player = p->id;

    for (;;) {
        if (scratch->combo.reaction_step_count >= MAX_REACTION_STEPS) break;

        ReactionStep step = {};
        if (!find_reaction_step_from_candidates(p, scratch, &step)) break;

        combo_record_step(&scratch->combo, &step);
        clear_reaction_candidates(scratch);
        clear_reaction_step_orbs(p, &step, events);
        apply_board_gravity(p, scratch, events);
    }
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

    // Overlay to visualize the current column
    v2 col_rect_tl = layout->field_tl + make_v2(player->at_col * layout->cell_wh.x, 0.f);
    v2 col_rect_wh = hadamard(layout->cell_wh, make_v2(1.f, PLAYERFIELD_ROWS));
    Color color = ColorAlpha(ORANGE, 0.25f);
    DrawRectangleRec(make_rect(col_rect_tl, col_rect_wh), color);
}

static void draw_player_board(PlayerState *player, const PlayerFieldLayout *layout) {
    for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            Orb *orb = board_get_cell(player, col, row);
            if (!orb_exists(*orb)) continue;

            draw_orb_rect(board_cell_to_screen(layout, col, row), layout->orb_dim, orb->type);
        }
    }

    // Draw simple overlay for an eliminated player
    if (player->condition == PLAYER_ELIMINATED) {
        Color color = ColorAlpha(DARKBLUE, 0.5f);
        DrawRectangleRec(make_rect(layout->field_tl, layout->field_wh), color);
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

static inline void clear_player_state(PlayerState *p, PlayerId id) {
    // Clear everything
    memset(p, 0, sizeof(PlayerState));

    // Set ID
    p->id = id;
    p->condition = PLAYER_ACTIVE;

    // Set initial position to be middle col
    p->at_col = (u8) PLAYERFIELD_COLS / 2;
}

static inline PlayerState* boardstate_get_player(BoardState *board, PlayerId player_id) {
    PlayerState *result = 0;

    if (player_id == PLAYER_1_ID) result = &board->players[PLAYER1];
    if (player_id == PLAYER_2_ID) result = &board->players[PLAYER2];

    return result;
}

static inline BoardResolveScratch* gametick_get_scratch(GameTickScratch *scratch, PlayerId player_id) {
    BoardResolveScratch *result = 0;

    if (player_id == PLAYER_1_ID) result = &scratch->players[PLAYER1];
    if (player_id == PLAYER_2_ID) result = &scratch->players[PLAYER2];

    return result;
}

static inline void generate_board_command(BoardCommandBuffer *buf, BoardCommandType type) {
    ASSERT(buf->count < MAX_BOARD_COMMANDS);
    BoardCommand cmd = {
        .type      = type,
        .player_id = buf->id,
        .cmd_seq   = cmd_seq_next(),
    };
    buf->buffer[buf->count++] = cmd;
}

static void shift_column_down(PlayerState *ps, u32 col, BoardEventBuffer *event_buf) {
    for (i32 row = PLAYERFIELD_ROWS - 1; row >= 0; --row) {
        Orb *cell = board_get_cell(ps, col, row);
        if (!orb_exists(cell)) continue;

        u32 new_row = row + 1;

        if (new_row >= PLAYERFIELD_ROWS) {
            BoardEvent event = {};
            event.type     = BOARDEVENT_ORB_DEADZONED;
            event.orb_id   = cell->id;
            event.from_col = col;
            event.from_row = row;
            event.to_col   = col;
            event.to_row   = new_row; // Bad to provide an illegal row even if DEADZONED event?
            push_board_event(event_buf, ps, event);

            ps->deadzone[col] = *cell;
            *cell = {};
        } else {
            Orb *new_cell = board_get_cell(ps, col, new_row);
            ASSERT(!orb_exists(new_cell));

            BoardEvent event = {};
            event.type     = BOARDEVENT_ORB_MOVED;
            event.orb_id   = cell->id;
            event.from_col = col;
            event.from_row = row;
            event.to_col   = col;
            event.to_row   = new_row;
            push_board_event(event_buf, ps, event);

            *new_cell = *cell;
            *cell = {};
        }
    }
}

static void spawn_random_row(GameServer *server, Player player, BoardEventBuffer *event_buf) {
    ASSERT(server);
    ASSERT(player < MAX_PLAYER_COUNT);

    BoardControl *control = &server->control;
    PlayerState *ps = &server->boardstate.players[player];

    // Push down all rows that contain any orbs
    for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
        shift_column_down(ps, col, event_buf);
        Orb *cell = board_get_cell(ps, col, 0);
        ASSERT(!orb_exists(cell));

        Orb new_orb = make_orb(control->next_orb_id++, random_orb());
        BoardEvent event = {};
        event.type     = BOARDEVENT_ORB_SPAWNED;
        event.orb_id   = new_orb.id;
        event.orb_type = new_orb.type;
        event.to_col   = col;
        event.to_row   = 0;
        push_board_event(event_buf, ps, event);

        *cell = new_orb;
    }
}

static inline b32 deadzone_is_empty(PlayerState *ps) {
    for (u32 i = 0; i < PLAYERFIELD_COLS; ++i) {
        if (ps->deadzone[i].type != ORB_NONE) return false;
    }
    return true;
}

static inline void eliminate_player(PlayerState *ps, BoardEventBuffer *event_buf) {
    if (ps->condition == PLAYER_ELIMINATED) return;

    BoardEvent event = {};
    event.type = BOARDEVENT_PLAYER_ELIMINATED;
    push_board_event(event_buf, ps, event);
    ps->condition = PLAYER_ELIMINATED;
}

static void push_game_over_events(GameServer *server, BoardEventBuffer *event_buf, GameResult result) {
    ASSERT(server);

    BoardState *boardstate = &server->boardstate;

    BoardEvent event = {};
    event.type = BOARDEVENT_GAME_OVER;
    event.result = result;

    for (u32 i = 0; i < boardstate->player_count; ++i) {
        push_board_event(event_buf, &boardstate->players[i], event);
    }
}

static void enqueue_pending_effect(GameServer *server, BoardEffectType effect, Player target_player) {
    ASSERT(server->pending_effects_count < MAX_PENDING_EFFECTS);
    ASSERT(target_player >= PLAYER1 && target_player < MAX_PLAYER_COUNT);

    BoardEffect e = {};
    e.type = effect;
    e.target_player = target_player;
    server->pending_effects[server->pending_effects_count++] = e;
}

// NOTE: Does not clear the pending effects, intentionally
static void resolve_pending_effects(GameServer *server, BoardEventBuffer *event_buf) {
    for (u32 i = 0; i < server->pending_effects_count; ++i) {
        BoardEffect *e = &server->pending_effects[i];
        PlayerState *p = &server->boardstate.players[e->target_player];

        // Maybe some effects should affect inactive players,
        // but for now we'll just keep that blocker here
        if (p->condition != PLAYER_ACTIVE) continue;

        switch (e->type) {
            case BOARDEFFECT_SPAWN_RANDOM_ROW: {
                spawn_random_row(server, e->target_player, event_buf);
            } break;

            default: continue;
        }
    }
}

static void apply_combo_rewards(GameServer *server, ComboResolution *combo, BoardEventBuffer *event_buf) {
    if (combo->reaction_step_count == 0) return;

    if (combo->reaction_step_count >= 2) {
        enqueue_pending_effect(server, BOARDEFFECT_SPAWN_RANDOM_ROW, PLAYER2);
    }

}

static void gamestate_tick(GameServer *server, BoardCommandBuffer *cmd_buf, BoardEventBuffer *event_buf) {
    GameResult *result = &server->result;
    if (result->game_over) return;

    server->pending_effects_count = 0;

    u64 current_tick = event_buf->tick;

    GameTickScratch scratch = {};
    BoardState *boardstate = &server->boardstate;

    // Process commands
    for (u32 i = 0; i < cmd_buf->count; ++i) {
        BoardCommand        *e  = &cmd_buf->buffer[i];
        PlayerState         *st = boardstate_get_player(boardstate, e->player_id);
        BoardResolveScratch *sc = gametick_get_scratch(&scratch, e->player_id);

        if (!st || !sc) continue;

        // Active player actions
        if (st->condition == PLAYER_ACTIVE) {
            // Consume and validate movements
            i8 player_at = st->at_col;
            if (e->type == BOARDCMD_MOVE_CURSOR_LEFT)  player_at -= 1;
            if (e->type == BOARDCMD_MOVE_CURSOR_RIGHT) player_at += 1;
            if (player_at < 0)                 player_at = 0;
            if (player_at >= PLAYERFIELD_COLS) player_at = PLAYERFIELD_COLS - 1;

            if ((u8) player_at != st->at_col) {
                BoardEvent event = {};
                event.type = BOARDEVENT_CURSOR_MOVED;
                event.orb_id = ORB_NONE;
                event.from_col = st->at_col;
                event.to_col = player_at;
                push_board_event(event_buf, st, event);
            }
            st->at_col = player_at;

            // Consume and validate board interaction
            if (e->type == BOARDCMD_PULL_STACK) attempt_to_pull_stack(event_buf, st);
            if (e->type == BOARDCMD_PUSH_STACK) attempt_to_push_stack(event_buf, st, sc);
        }

        // TODO: Add special handling for other conditions here if they should be allowed
        //       send commands
    }

    // Resolve board updates
    for (u32 p = PLAYER1; p < MAX_PLAYER_COUNT; ++p) {
        if (boardstate->players[p].condition < PLAYER_ACTIVE) continue;
        resolve_board_reactions(&boardstate->players[p], &scratch.players[p], event_buf);
        apply_combo_rewards(server, &scratch.players[p].combo, event_buf);
    }

    // Timeout updates
    BoardControl *control = &server->control;
    if (control->random_spawn_tick_interval) {
        if (current_tick >= control->next_random_spawn_tick) {
            enqueue_pending_effect(server, BOARDEFFECT_SPAWN_RANDOM_ROW, PLAYER1);
            enqueue_pending_effect(server, BOARDEFFECT_SPAWN_RANDOM_ROW, PLAYER2);
        }
    }

    resolve_pending_effects(server, event_buf);

    // Check deadzones and eliminations
    u32 win_candidate_count = 0;
    u32 eliminated_count = 0;
    PlayerState* win_candidates[MAX_PLAYER_COUNT] = {0};
    for (u32 i = 0; i < boardstate->player_count; ++i) {
        PlayerState *ps = &boardstate->players[i];

        // Eliminate player if any orbs in deadzone
        if (!deadzone_is_empty(ps)) eliminate_player(ps, event_buf);

        if      (ps->condition == PLAYER_ACTIVE)     win_candidates[win_candidate_count++] = ps;
        else if (ps->condition == PLAYER_ELIMINATED) eliminated_count++;
    }

    if (eliminated_count > 0 && win_candidate_count == 1) {
        // Win condition for 1v1
        PlayerState *winner = win_candidates[0];
        ASSERT(winner);

        GameResult result = {};
        result.game_over = true;
        result.winner_count = 1;
        result.winners[0] = winner->id;

        push_game_over_events(server, event_buf, result);
        server->result = result;
    } else if (eliminated_count == boardstate->player_count) {
        GameResult result = {};
        result.game_over = true;
        result.is_draw = true;
        result.winner_count = 0;

        push_game_over_events(server, event_buf, result);
        server->result = result;
    }
}

static BoardSnapshot make_board_snapshot(GameServer *server) {
    ASSERT(server);

    BoardSnapshot result = {};
    memcpy(&result, &server->boardstate, sizeof(result));
    return result;
}

static void apply_snapshot(GameClient *client, BoardSnapshot *snap) {
    ASSERT(client && snap);
    memcpy(&client->boardstate, snap, sizeof(client->boardstate));
}

static inline b32 board_states_match(BoardState *a, BoardState *b) {
    ASSERT(a && b);
    return memcmp(a, b, sizeof(BoardState)) == 0;
}

static void apply_events(GameClient *client, BoardEventBuffer *events) {
    ASSERT(client && events);

    BoardState *state = &client->boardstate;

    for (u32 i = 0; i < events->count; ++i) {
        BoardEvent *e = &events->events[i];

        PlayerState *st = boardstate_get_player(state, e->player_id);

        switch (e->type) {
            case BOARDEVENT_ORB_MOVED: {
                ASSERT(st);
                ASSERT(e->from_col < PLAYERFIELD_COLS);
                ASSERT(e->from_row < PLAYERFIELD_ROWS);
                ASSERT(e->to_col < PLAYERFIELD_COLS);
                ASSERT(e->to_row < PLAYERFIELD_ROWS);

                Orb *src = board_get_cell(st, e->from_col, e->from_row);
                Orb *dst = board_get_cell(st, e->to_col, e->to_row);

                ASSERT(orb_exists(*src));
                ASSERT(src->id == e->orb_id);
                ASSERT(!orb_exists(*dst));

                *dst = *src;
                *src = {};
            } break;

            case BOARDEVENT_ORB_REMOVED: {
                ASSERT(st);
                ASSERT(e->from_col < PLAYERFIELD_COLS);
                ASSERT(e->from_row < PLAYERFIELD_ROWS);

                Orb *cell = board_get_cell(st, e->from_col, e->from_row);

                ASSERT(orb_exists(*cell));
                ASSERT(cell->id == e->orb_id);

                *cell = {};
            } break;

            case BOARDEVENT_ORB_HELD: {
                ASSERT(st);
                ASSERT(e->from_col < PLAYERFIELD_COLS);
                ASSERT(e->from_row < PLAYERFIELD_ROWS);
                ASSERT(st->hold_count < PLAYER_HOLD_SIZE);

                Orb *src = board_get_cell(st, e->from_col, e->from_row);

                ASSERT(orb_exists(*src));
                ASSERT(src->id == e->orb_id);

                st->hold[st->hold_count++] = *src;
                *src = {};
            } break;

            case BOARDEVENT_ORB_RELEASED: {
                ASSERT(st);
                ASSERT(e->to_col < PLAYERFIELD_COLS);
                ASSERT(e->to_row < PLAYERFIELD_ROWS);
                ASSERT(st->hold_count > 0);

                Orb o = st->hold[--st->hold_count];
                Orb *dst = board_get_cell(st, e->to_col, e->to_row);

                ASSERT(o.id == e->orb_id);
                ASSERT(!orb_exists(*dst));

                *dst = o;
            } break;

            case BOARDEVENT_ORB_DEADZONED: {
                ASSERT(st);
                ASSERT(e->to_col < PLAYERFIELD_COLS);
                ASSERT(e->to_row >= PLAYERFIELD_ROWS);

                Orb *src = board_get_cell(st, e->from_col, e->from_row);
                Orb *dst = &st->deadzone[e->to_col];

                ASSERT(orb_exists(*src));
                ASSERT(src->id == e->orb_id);

                *dst = *src;
                *src = {};
            } break;

            case BOARDEVENT_ORB_SPAWNED: {
                ASSERT(st);
                ASSERT(e->to_col < PLAYERFIELD_COLS);
                ASSERT(e->to_row < PLAYERFIELD_ROWS);

                Orb new_orb  = {};
                new_orb.id   = e->orb_id;
                new_orb.type = e->orb_type;

                Orb *dst = board_get_cell(st, e->to_col, e->to_row);
                ASSERT(!orb_exists(*dst));
                *dst = new_orb;
            } break;

            case BOARDEVENT_CURSOR_MOVED: {
                ASSERT(st);
                ASSERT(e->to_col < PLAYERFIELD_COLS);
                st->at_col = e->to_col;
            } break;

            case BOARDEVENT_PLAYER_ELIMINATED: {
                ASSERT(st);
                st->condition = PLAYER_ELIMINATED;
            } break;

            case BOARDEVENT_GAME_OVER: {
                client->result = e->result;
            } break;
        }
    }
}

static void log_events(EventLog *log, BoardEventBuffer *events, u64 tick) {
    for (u32 i = 0; i < events->count; ++i) {
        BoardEvent *e = &events->events[i];
        char msg[64] = {0};

        switch (e->type) {
            case BOARDEVENT_CURSOR_MOVED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Cursor moved from [%d] to [%d]", tick, e->player_id, e->from_col, e->to_col);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_MOVED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] moved from (%d, %d) to (%d, %d)", tick, e->player_id, e->orb_id, e->from_col, e->from_row, e->to_col, e->to_row);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_REMOVED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] removed!", tick, e->player_id, e->orb_id);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_HELD: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] held!", tick, e->player_id, e->orb_id);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_RELEASED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] released to (%d, %d)", tick, e->player_id, e->orb_id, e->to_col, e->to_row);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_DEADZONED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] deadzoned!!", tick, e->player_id, e->orb_id);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_ORB_SPAWNED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Orb [%d] spawned!!", tick, e->player_id, e->orb_id);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_PLAYER_ELIMINATED: {
                u32 count = snprintf(msg, 63, "[%zu][P_%zu] Player eliminated", tick, e->player_id);
                event_push(log, msg, count);
            } break;

            case BOARDEVENT_GAME_OVER: {
                u32 count = 0;
                if (e->result.is_draw) {
                    count = snprintf(msg, 63, "[%zu] Game over: draw", tick);
                } else {
                    count = snprintf(msg, 63, "[%zu] Game over: P_%zu wins", tick, e->result.winners[0]);
                }
                event_push(log, msg, count);
            } break;
        }
    }
}

static void update_game(GameState *game, GameInput *new_input, EventLog *log) {
    // Temp game initialization in here for now
    if (!game->initialized) {
        BoardControl *server_control = &game->server.control;
        server_control->next_orb_id = 1;
        server_control->random_spawn_tick_interval = 7.5f;
        server_control->next_random_spawn_tick = game->tick + estimate_ticks_for_duration(server_control->random_spawn_tick_interval, game->target_tick_rate);

        game->server.boardstate.player_count = 2;

        clear_player_state(&game->server.boardstate.players[PLAYER1], PLAYER_1_ID);
        clear_player_state(&game->server.boardstate.players[PLAYER2], PLAYER_2_ID);

        populate_random_rows(&game->server, PLAYER1, 4);
        populate_random_rows(&game->server, PLAYER2, 4);

        BoardSnapshot snap = make_board_snapshot(&game->server);
        apply_snapshot(&game->client, &snap);

        game->initialized = true;
    }

    BoardCommandBuffer commands = {};
    commands.tick = game->tick;
    commands.id = PLAYER_1_ID;

    // Gather input into a list of command
    {
        u32 controller = new_input->controller;
        if (controller & GAMEINPUT_MOV_LEFT)  generate_board_command(&commands, BOARDCMD_MOVE_CURSOR_LEFT);
        if (controller & GAMEINPUT_MOV_RIGHT) generate_board_command(&commands, BOARDCMD_MOVE_CURSOR_RIGHT);
        if (controller & GAMEINPUT_MOV_DOWN)  generate_board_command(&commands, BOARDCMD_PULL_STACK);
        if (controller & GAMEINPUT_MOV_UP)    generate_board_command(&commands, BOARDCMD_PUSH_STACK);
    }

    // Authoritative board state (server) should process commands
    // and give back the resulting events, and/or the current board state in full
    BoardEventBuffer events = {};
    events.tick = game->tick;
    if (!game->server.result.game_over)
        gamestate_tick(&game->server, &commands, &events);

    // Apply events to client
    apply_events(&game->client, &events);

#if DEBUG
    ASSERT(board_states_match(&game->server.boardstate, &game->client.boardstate));
#endif

#if 1
    log_events(log, &events, game->tick);
#endif

    // Server control updates
    BoardControl *control = &game->server.control;
    if (game->tick >= control->next_random_spawn_tick) {
        control->next_random_spawn_tick = game->tick + estimate_ticks_for_duration(control->random_spawn_tick_interval, game->target_tick_rate);
    }
    game->tick++;
}

static void draw_event_log(EventLog *log, Font font) {
    ASSERT(log);

    u32 offset_x  = 10;
    u32 offset_y  = 10;
    u32 font_size = 20;

    Event *event = log->events;
    while (event) {
        Vector2 pos = (Vector2){ (f32) offset_x, (f32) offset_y };

        f32 a = event->time_left / (EVENT_TIMEOUT * .25f);
        Color use_color = ColorAlpha(MAROON, a);

        DrawTextEx(font, event->buf, pos, font_size, 1.f, use_color);

        offset_y += font_size;
        event = event->next;
    }
}

static void render_game(GameState *game, EventLog *log) {
    BeginDrawing();

    ClearBackground(CLEARCOLOR);

    const GameRenderLayout *layout = &GAME_RENDER_LAYOUT;
    draw_player_field(&game->client.boardstate.players[PLAYER1], &layout->player_1);
    draw_player_field(&game->client.boardstate.players[PLAYER2], &layout->player_2);

#if 0
    draw_event_log(log, game->debugfont);
#endif

    EndDrawing();
}
