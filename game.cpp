#include <string.h> // For memset etc., consider replacing with something else to avoid pulling in all the string stuff
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

static inline f32 clamp01(f32 value) {
    if (value < 0.f) return 0.f;
    if (value > 1.f) return 1.f;
    return value;
}

static inline v2 lerp_v2(v2 a, v2 b, f32 t) {
    return a + ((b - a) * t);
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

static inline BoardCell *board_get_cell(PlayerState *p, u32 x, u32 y) {
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

static inline StackView make_stack_view(PlayerState *p, u32 col, StackDirection dir, u32 max = PLAYERFIELD_ROWS) {
    ASSERT(dir == STACKDIR_DOWN); // For now, however possibly can use this to make stacks in any direction
    ASSERT(col < PLAYERFIELD_COLS);

    StackView result = {};

    u32 count = 0;
    while (count < max && count < PLAYERFIELD_ROWS) {
        BoardCell *cell = board_get_cell(p, col, count);
        if (!orb_exists(cell->orb)) break;
        count++;
    }

    result.player = p;
    result.col = col;
    result.top_row = count > 0 ? (i32) count - 1 : -1;
    result.count = count;
    result.max = max;
    result.dir = dir;

    return result;
}

static inline Orb stack_view_pop(StackView *s) {
    ASSERT(s->count > 0 && s->top_row >= 0);

    BoardCell *cell = board_get_cell(s->player, s->col, (u32) s->top_row);
    Orb result = cell->orb;
    s->top_row--;
    s->count--;
    return result;
}

static inline Orb stack_view_pop_commit(StackView *s) {
    ASSERT(s->count > 0 && s->top_row >= 0);

    BoardCell *cell = board_get_cell(s->player, s->col, (u32) s->top_row);
    Orb result = cell->orb;
    cell->orb = {};
    s->top_row--;
    s->count--;
    return result;
}

static inline u32 stack_view_push_commit(StackView *s, Orb orb) {
    ASSERT(s->count < s->max);

    u32 dst_row = s->count;
    BoardCell *dst = board_get_cell(s->player, s->col, dst_row);
    ASSERT(!orb_exists(dst->orb));

    dst->orb = orb;
    s->top_row = (i32) dst_row;

    s->count++;
    return dst_row;
}

static void attempt_to_pull_stack(PlayerState *p, BoardEventBuffer *events) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);

    // Stackview for the settled column, from the visual top down
    StackView s = make_stack_view(p, p->at_col, STACKDIR_DOWN);
    if (s.count < 1) return; // Nothing to pull

    OrbType ref;
    if (p->hold.count > 0) ref = p->hold.type;
    else                   ref = board_get_cell(p, p->at_col, (u32) s.top_row)->orb.type;

    // Count how many to pull
    u32 pull_count = 0;
    StackView copy = s;
    while (copy.top_row >= 0 && board_get_cell(p, p->at_col, (u32) copy.top_row)->orb.type == ref) {
        stack_view_pop(&copy);
        pull_count++;
    }

    // If we can fit the stack...
    if (p->hold.count + pull_count < PLAYER_HOLD_SIZE) {
        // ... We pull the stack
        p->hold.type = ref;
        for (u32 i = 0; i < pull_count; ++i) {
            u32 from_row = (u32) s.top_row;
            Orb o = stack_view_pop_commit(&s);
            p->hold.orbs[p->hold.count++] = o;

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
    BoardCell *cell = &p->board[index];
    if (cell->orb.type == ORB_NONE || cell->orb.type != type) return;

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
    BoardCell *cell = board_get_cell(p, col, row);
    if (cell->orb.type == ORB_NONE) return false;

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
        BoardCell *neighbor = &p->board[neighbor_index];

        // Should trigger a chain reaction if neighbor wasn't placed just now, and it is of mathing type
        if (!scratch->just_dropped[neighbor_index] && neighbor->orb.type == cell->orb.type) return true;
    }

    return false;
}

static void attempt_to_push_stack(PlayerState *p, BoardResolveScratch *scratch, BoardEventBuffer *events) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);
    if (p->hold.count < 1 || p->hold.type == ORB_NONE) return; // Nothing to push

    // Stackview for the settled column, from the visual top down
    StackView s = make_stack_view(p, p->at_col, STACKDIR_DOWN);
    if (s.count + p->hold.count > PLAYERFIELD_ROWS) return; // Can't exceed column stack max size

    OrbType ref = p->hold.type;

    bool trigger_chain = false;
    u32 last_row = 0;
    while (p->hold.count > 0) {
        Orb o = p->hold.orbs[--p->hold.count];
        u32 dst_row = stack_view_push_commit(&s, o);
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
    p->hold.type = ORB_NONE;

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
            BoardCell *cell = board_get_cell(p, col, row);
            cell->orb = make_orb(p, random_orb());
        }
    }
}

static void apply_board_gravity(PlayerState *p, BoardEventBuffer *events) {
    for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
        u32 write_row = 0;

        for (u32 read_row = 0; read_row < PLAYERFIELD_ROWS; ++read_row) {
            BoardCell *src = board_get_cell(p, col, read_row);
            if (!orb_exists(src->orb)) continue;

            BoardCell *dst = board_get_cell(p, col, write_row);
            if (read_row != write_row) {
                Orb moved_orb = src->orb;
                dst->orb = src->orb;
                src->orb = {};

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

        BoardCell *cell = &p->board[i];
        if (!orb_exists(cell->orb)) continue;

        BoardEvent event = {};
        event.type = BOARDEVENT_ORB_REMOVED;
        event.orb = cell->orb;
        event.from_col = i % PLAYERFIELD_COLS;
        event.from_row = i / PLAYERFIELD_COLS;
        event.to_col = event.from_col;
        event.to_row = event.from_row;
        push_board_event(events, event);

        cell->orb = {};
    }

    apply_board_gravity(p, events);
}

// ---
// animation helpers
// ---

static OrbAnimation* get_or_create_animation(PlayerAnimationState *animation_state, OrbId id) {
    ASSERT(animation_state && id > 0);

    // TODO: Grab from free list?

    i32 first_free_index = -1;
    for (u32 i = 0; i < MAX_PLAYER_ORBS; ++i) {
        OrbAnimation *o = &animation_state->animations[i];

        if (first_free_index < 0 && !o->active) {
            first_free_index = i;
        }

        if (o->active && o->id == id) return o;
    }

    ASSERT(first_free_index >= 0);
    OrbAnimation *animation = &animation_state->animations[first_free_index];
    memzero_struct(animation);

    animation->active = true;
    animation->id = id;
    return animation;
}

static OrbAnimation *find_active_animation(PlayerAnimationState *animation_state, OrbId id) {
    if (!animation_state || id == ORB_ID_NONE) return 0;

    for (u32 i = 0; i < MAX_PLAYER_ORBS; ++i) {
        OrbAnimation *animation = &animation_state->animations[i];
        if (animation->active && animation->id == id) return animation;
    }

    return 0;
}

static void update_animation_state(PlayerAnimationState *animation_state, f32 dt) {
    for (u32 i = 0; i < MAX_PLAYER_ORBS; ++i) {
        OrbAnimation *animation = &animation_state->animations[i];
        if (!animation->active) continue;

        if (animation->duration > 0.f) {
            animation->t = clamp01(animation->t + (dt / animation->duration));
        } else {
            animation->t = 1.f;
        }

        switch (animation->state) {
            case ORBANIM_MOVING: {
                if (animation->t >= 1.f) {
                    animation->active = false;
                    animation->state = ORBANIM_IDLE;
                }
            } break;

            case ORBANIM_REMOVING: {
                if (animation->t >= 1.f) {
                    animation->active = false;
                    animation->state = ORBANIM_IDLE;
                }
            } break;

            default: {
                animation->active = false;
                animation->state = ORBANIM_IDLE;
            } break;
        }
    }
}

static void draw_orb_rect(v2 pos, v2 dim, OrbType type) {
    DrawRectangleRec(make_rect(pos, dim), get_orb_render_color(type));
}

static void draw_animation_state(PlayerAnimationState *animation_state, const PlayerFieldLayout *layout) {
    for (u32 i = 0; i < MAX_PLAYER_ORBS; ++i) {
        OrbAnimation *animation = &animation_state->animations[i];
        if (!animation->active) continue;

        v2 from_pos = board_cell_to_screen(layout, animation->from_col, animation->from_row);
        v2 to_pos   = board_cell_to_screen(layout, animation->to_col, animation->to_row);
        v2 pos = lerp_v2(from_pos, to_pos, animation->t);
        v2 dim = layout->orb_dim;
        Color color = get_orb_render_color(animation->type);

        if (animation->state == ORBANIM_REMOVING) {
            f32 scale = 1.f - (0.35f * clamp01(animation->t));
            dim *= scale;
            pos += (layout->orb_dim - dim) * 0.5f;
            color.a = (unsigned char) (255.f * (1.f - clamp01(animation->t)));
        }

        DrawRectangleRec(make_rect(pos, dim), color);
    }
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

static void apply_board_events_to_animations(BoardEventBuffer *event_buffer, PlayerAnimationState *animation_state) {
    for (u32 i = 0; i < event_buffer->count; ++i) {
        BoardEvent *event = &event_buffer->events[i];

        OrbId id = event->orb.id;
        if (id < 1) continue; // Invalid OrbId

        switch (event->type) {
            case BOARDEVENT_ORB_MOVED: {
                OrbAnimation *animation = get_or_create_animation(animation_state, id);
                animation->type      = event->orb.type;
                animation->from_col  = event->from_col;
                animation->from_row  = event->from_row;
                animation->to_col    = event->to_col;
                animation->to_row    = event->to_row;
                animation->t         = 0;
                animation->duration  = ORB_MOVE_DURATION;
                animation->state     = ORBANIM_MOVING;
            } break;

            case BOARDEVENT_ORB_REMOVED: {
                OrbAnimation *animation = get_or_create_animation(animation_state, id);
                animation->type      = event->orb.type;
                animation->from_col  = event->from_col;
                animation->from_row  = event->from_row;
                animation->to_col    = event->from_col;
                animation->to_row    = event->from_row;
                animation->t         = 0;
                animation->duration  = ORB_REMOVE_DURATION;
                animation->state     = ORBANIM_REMOVING;
            } break;

            // case BOARDEVENT_ORB_HELD: {
            //     OrbAnimation *animation = get_or_create_animation(animation_state, id);
            //     animation->from_col  = event->from_col;
            //     animation->from_row  = event->from_row;
            //     animation->to_col    = event->to_col;
            //     animation->to_row    = event->to_row;
            //     animation->t         = 0;
            //     animation->duration  = ORB_MOVE_DURATION;
            //     animation->state     = ORBANIM_MOVING;
            // } break;

            default: continue;
        }
    }
}

static void update_game(GameState *game, GameInput *new_input) {
    if (!game->initialized) {
        clear_player_state(&game->player_1);
        clear_player_state(&game->player_2);

        populate_random_rows(&game->player_1, 4);
        populate_random_rows(&game->player_2, 4);

        game->initialized = true;
    }

    BoardResolveScratch p1_scratch = {};
    BoardResolveScratch p2_scratch = {};
    BoardEventBuffer p1_events = {};
    BoardEventBuffer p2_events = {};

    PlayerState *p1 = &game->player_1;
    PlayerState *p2 = &game->player_2;

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
    if (controller & GAMEINPUT_MOV_DOWN) attempt_to_pull_stack(p1, &p1_events);
    if (controller & GAMEINPUT_MOV_UP)   attempt_to_push_stack(p1, &p1_scratch, &p1_events);

    // Update board and generate events
    update_board(&game->player_1, &p1_scratch, &p1_events);
    update_board(&game->player_2, &p2_scratch, &p2_events);

    apply_board_events_to_animations(&p1_events, &p1->animation_state);
    apply_board_events_to_animations(&p2_events, &p2->animation_state);

    update_animation_state(&p1->animation_state, new_input->time_delta_seconds);
    update_animation_state(&p2->animation_state, new_input->time_delta_seconds);
}

static void render_game(GameState *game) {
    BeginDrawing();

    ClearBackground(CLEARCOLOR);

    const GameRenderLayout *layout = &GAME_RENDER_LAYOUT;
    v2 pf1_tl = layout->player_1.field_tl;
    v2 pf2_tl = layout->player_2.field_tl;
    v2 pf_wh = layout->player_1.field_wh;
    v2 cell_wh = layout->player_1.cell_wh;
    v2 pf_gutter_wh = layout->player_1.gutter_wh;

    // Visualize grid
    {
        for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
            v2 row_offset = make_v2(0, (f32) (row + 1) * cell_wh.y);
            v2 len = make_v2(pf_wh.x, 0);
            DrawLineEx(make_vector2(pf1_tl + row_offset), make_vector2(pf1_tl + row_offset + len), 1, GRAY);
            DrawLineEx(make_vector2(pf2_tl + row_offset), make_vector2(pf2_tl + row_offset + len), 1, GRAY);
        }

        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            v2 col_offset = make_v2((f32) (col + 1) * cell_wh.x, 0);
            v2 height     = make_v2(0, pf_wh.y);
            DrawLineEx(make_vector2(pf1_tl + col_offset), make_vector2(pf1_tl + col_offset + height), 1, GRAY);
            DrawLineEx(make_vector2(pf2_tl + col_offset), make_vector2(pf2_tl + col_offset + height), 1, GRAY);
        }

        // Visualize Gutter
        v2 gutter_offset = make_v2(0, pf_wh.y - pf_gutter_wh.y) + make_v2(1, 1); // 1 pixel offset
        v2 gutter_dim    = pf_gutter_wh - make_v2(2, 2); // 2 pixel inset
        v2 pf1_gutter_tl = pf1_tl + gutter_offset;
        v2 pf2_gutter_tl = pf2_tl + gutter_offset;
        DrawRectangleRec(make_rect(pf1_gutter_tl, gutter_dim), GRAY);
        DrawRectangleRec(make_rect(pf2_gutter_tl, gutter_dim), GRAY);
    }

    // Player fields
    {
        Rectangle pf1_rect = make_rect(pf1_tl, pf_wh);
        Rectangle pf2_rect = make_rect(pf2_tl, pf_wh);

        // Border
        DrawRectangleLinesEx(pf1_rect, 1, DARKBLUE);
        DrawRectangleLinesEx(pf2_rect, 1, DARKBLUE);
    }

    // PlayerFields
    {
        PlayerState *p1 = &game->player_1;
        PlayerState *p2 = &game->player_2;

        f32 pf_gutter_offset  = pf_wh.y - pf_gutter_wh.y;
        v2  center            = cell_wh * 0.5f;
        v2  player_rect       = make_v2(10, 10);
        v2  player_rect_center = player_rect * -0.5f;

        // Cursor
        v2 player1_pos = pf1_tl + make_v2(p1->at_col * cell_wh.x, pf_gutter_offset) + center + player_rect_center;
        v2 player2_pos = pf2_tl + make_v2(p2->at_col * cell_wh.x, pf_gutter_offset) + center + player_rect_center;
        Rectangle player1_rect = make_rect(player1_pos, player_rect);
        Rectangle player2_rect = make_rect(player2_pos, player_rect);
        DrawRectangleRec(player1_rect, ORANGE);
        DrawRectangleRec(player2_rect, ORANGE);

        // Board
        for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
            for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
                v2 p1_orb_pos = board_cell_to_screen(&layout->player_1, col, row);
                v2 p2_orb_pos = board_cell_to_screen(&layout->player_2, col, row);

                BoardCell *p1_cell = board_get_cell(p1, col, row);
                BoardCell *p2_cell = board_get_cell(p2, col, row);

                if (orb_exists(p1_cell->orb) && !find_active_animation(&p1->animation_state, p1_cell->orb.id)) {
                    draw_orb_rect(p1_orb_pos, layout->player_1.orb_dim, p1_cell->orb.type);
                }

                if (orb_exists(p2_cell->orb) && !find_active_animation(&p2->animation_state, p2_cell->orb.id)) {
                    draw_orb_rect(p2_orb_pos, layout->player_2.orb_dim, p2_cell->orb.type);
                }
            }
        }

        draw_animation_state(&p1->animation_state, &layout->player_1);
        draw_animation_state(&p2->animation_state, &layout->player_2);

        // Hold
        {
            v2 padding = make_v2(10.f, 0.f);

            v2 p1_stack_pos = pf1_tl + make_v2(0., pf_wh.y) - padding;
            // v2 p2_stack_pos = pf2_tl + pf_wh                + padding;

            v2 r_dim = make_v2(10.f, 10.f);
            f32 gap = 5.f;
            for (u32 i = 0; i < p1->hold.count; ++i) {
                v2 r_offs = make_v2(-r_dim.x, -r_dim.y * i - gap);

                Rectangle r = make_rect(p1_stack_pos + r_offs, r_dim);
                DrawRectangleRec(r, get_orb_render_color(p1->hold.type));
            }
            // DrawRectangleRec(p2_hold_rect, ORANGE);
        }
    }

    EndDrawing();
}
