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

static void attempt_to_pull_stack(PlayerState *p) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);

    // Stackview for the settled column, from the visual top down.
    StackView s = make_stack_view(&p->board[board_get_index(p->at_col, 0)], STACKDIR_DOWN);
    if (s.count < 1) return; // Nothing to pull

    OrbType ref;
    if (p->hold_count > 0) ref = p->hold_type;
    else                   ref = s.top->orb.type;

    // Count how many to pull
    u32 pull_count = 0;
    StackView copy = s;
    while (copy.top && copy.top->orb.type == ref) {
        stack_view_pop(&copy);
        pull_count++;
    }

    // If we can fit the stack...
    if (p->hold_count + pull_count < PLAYER_HOLD_SIZE) {
        // ... We pull the stack
        p->hold_type = ref;
        for (u32 i = 0; i < pull_count; ++i) {
            Orb o = stack_view_pop_commit(&s);
            p->hold[p->hold_count++] = o;
        }
    }
}

static void mark_matching_recursive_depth_first(GridOrb *gorb, OrbType type, StackDirection from_dir) {
    // NULL or empty, or incorrect type should stop recursion
    if (!gorb || gorb->orb.type == ORB_NONE || gorb->orb.type != type) return;

    // Do not visit already marked gorbs, otherwise we may end up with infinite recursion loops
    if (gorb->marked) return;

    gorb->marked = true;

    // Recurse over all neighbors except originator
    for (u32 i = 0; i < STACKDIR_INVALID; ++i) {
        if (i == from_dir) continue;
        mark_matching_recursive_depth_first(gorb->neighbor[i], type, opposite_dir((StackDirection) i));
    }
}

static inline bool should_trigger_chain(GridOrb *gorb) {
    if (!gorb || gorb->orb.type == ORB_NONE) return false;

    // Just check if any neighbor matches this color
    for (u32 i = 0; i < STACKDIR_INVALID; ++i) {
        GridOrb *n = gorb->neighbor[i];
        if (n && !orb_has_flags(&n->orb, ORB_JUST_DROPPED) && n->orb.type == gorb->orb.type) return true;
    }

    return false;
}

static void attempt_to_push_stack(PlayerState *p) {
    ASSERT(p->at_col < PLAYERFIELD_COLS);
    if (p->hold_count < 1 || p->hold_type == ORB_NONE) return; // Nothing to push

    // Stackview for the settled column, from the visual top down.
    StackView s = make_stack_view(&p->board[board_get_index(p->at_col, 0)], STACKDIR_DOWN);
    if (s.count + p->hold_count > PLAYERFIELD_ROWS) return; // Can't exceed column stack max size

    OrbType ref = p->hold_type;

    bool trigger_chain = false;
    while (p->hold_count > 0) {
        Orb o = p->hold[--p->hold_count];
        stack_view_push_commit(&s, o);
        s.top->orb.flags |= ORB_JUST_DROPPED;
        if (!trigger_chain) trigger_chain = should_trigger_chain(s.top);
    }
    p->hold_type = ORB_NONE;

    if (trigger_chain) {
        // Come at the nodes from the top (i.e. below), meaning
        // that the initial one should ignore going down
        StackDirection initial_from_dir = STACKDIR_DOWN;
        mark_matching_recursive_depth_first(s.top, ref, initial_from_dir);
    }
}


static inline void clear_player_state(PlayerState *p) {
    // Clear everything
    memset(p, 0, sizeof(PlayerState));

    // Set initial position to be middle col
    p->at_col = (u8) PLAYERFIELD_COLS / 2;
}

static inline OrbType random_orb(void)  {
    return (OrbType) GetRandomValue(ORB_NONE + 1, ORB_MAX - 1);
}

static inline void populate_random_rows(PlayerState *p, u32 rows) {
    for (u32 row = 0; row < rows; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            GridOrb *orb  = &p->board[board_get_index(col, row)];
            orb->orb.type = random_orb();
        }
    }
}

static inline void build_board_links(PlayerState *p) {
    for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            GridOrb *o = &p->board[board_get_index(col, row)];

            o->neighbor_up   = (row > 0) ? &p->board[board_get_index(col, row - 1)] : 0;
            o->neighbor_left = (col > 0) ? &p->board[board_get_index(col - 1, row)] : 0;
            o->neighbor_right = (col + 1 < PLAYERFIELD_COLS) ? &p->board[board_get_index(col + 1, row)] : 0;
            o->neighbor_down  = (row + 1 < PLAYERFIELD_ROWS) ? &p->board[board_get_index(col, row + 1)] : 0;
        }
    }
}

static void apply_board_gravity(PlayerState *p) {
    for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
        u32 write_row = 0;

        for (u32 read_row = 0; read_row < PLAYERFIELD_ROWS; ++read_row) {
            GridOrb *src = &p->board[board_get_index(col, read_row)];
            if (src->orb.type == ORB_NONE) continue;

            GridOrb *dst = &p->board[board_get_index(col, write_row)];
            if (read_row != write_row) {
                dst->orb = src->orb;
                dst->orb.flags |= ORB_MOVING;
                dst->move_from_row = read_row;
                dst->marked = false;

                src->orb = {};
                src->marked = false;
                src->move_from_row = read_row;
            } else {
                dst->move_from_row = write_row;
            }

            write_row++;
        }
    }
}

static void update_board(PlayerState *p) {
    for (u32 i = 0; i < TOTAL_BOARD_SIZE; ++i) {
        GridOrb *o = p->board + i;
        ORB_CLEAR_FLAGS(&o->orb, ORB_MOVING);

        if (o->marked) {
            o->orb = {};
            o->marked = false;
        }

        ORB_CLEAR_FLAGS(&o->orb, ORB_JUST_DROPPED);
    }

    apply_board_gravity(p);
}

static void update_game(GameState *game, GameInput *new_input) {
    if (!game->initialized) {
        clear_player_state(&game->player_1);
        clear_player_state(&game->player_2);

        build_board_links(&game->player_1);
        build_board_links(&game->player_2);

        populate_random_rows(&game->player_1, 4);
        populate_random_rows(&game->player_2, 4);

        // Temp hack
        {
            GridOrb *o = &game->player_1.board[board_get_index(3, 0)];
            for (u32 i = 0; i < 4; ++i) {
                o->orb.type = ORB_PINK;
                o = o->neighbor_down;
            }

            o = &game->player_1.board[board_get_index(2, 2)];
            o->orb.type = ORB_PINK;
            o = o->neighbor_down;
            o->orb.type = ORB_PINK;

            o = &game->player_1.board[board_get_index(4, 3)];
            o->orb.type = ORB_PINK;
        }

        game->initialized = true;
    }

    PlayerState *p1 = &game->player_1;
    i8 player_at = p1->at_col;

    u32 controller = new_input->controller;

    // Movement
    if (controller & GAMEINPUT_MOV_LEFT)  player_at -= 1;
    if (controller & GAMEINPUT_MOV_RIGHT) player_at += 1;

    if (player_at < 0)                 player_at = 0;
    if (player_at >= PLAYERFIELD_COLS) player_at = PLAYERFIELD_COLS - 1;

    p1->at_col = player_at;

    // Stack interaction
    if (controller & GAMEINPUT_MOV_DOWN) attempt_to_pull_stack(p1);
    if (controller & GAMEINPUT_MOV_UP)   attempt_to_push_stack(p1);

    update_board(&game->player_1);
    update_board(&game->player_2);
}

static void render_game(GameState *game) {
    BeginDrawing();

    ClearBackground(CLEARCOLOR);

    int inner_w = TARGET_WIDTH * 0.5;
    int inner_h = TARGET_HEIGHT * 0.9;
    int inner_x_offset = (TARGET_WIDTH - inner_w) / 2;
    int inner_y_offset = (TARGET_HEIGHT - inner_h) / 2;

    v2 inner_tl = make_v2(inner_x_offset, inner_y_offset);
    v2 inner_wh = make_v2(inner_w, inner_h);

    f32 pf_gap = inner_wh.x * 0.1;
    f32 pf_bot = 40.f;
    v2  pf_wh  = make_v2((inner_w - pf_gap) * 0.5f, inner_h - pf_bot);

    v2 pf1_tl = inner_tl;
    v2 pf2_tl = inner_tl + make_v2(pf_gap + pf_wh.x, 0);

    v2 cell_wh = make_v2(pf_wh.x / PLAYERFIELD_COLS);
    v2 pf_gutter_wh = make_v2(pf_wh.x, pf_wh.y - (cell_wh.y * PLAYERFIELD_ROWS));

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
        v2 orb_pad = make_v2(4, 4);
        v2 orb_dim = cell_wh - (orb_pad * 2.);
        for (u32 row = 0; row < PLAYERFIELD_ROWS; ++row) {
            for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
                v2 grid_pos = hadamard(make_v2(col, row), cell_wh);

                v2 p1_orb_pos = pf1_tl + grid_pos + orb_pad;
                v2 p2_orb_pos = pf2_tl + grid_pos + orb_pad;

                Rectangle p1_orb_rect = make_rect(p1_orb_pos, orb_dim);
                Rectangle p2_orb_rect = make_rect(p2_orb_pos, orb_dim);

                GridOrb *p1_go = &p1->board[board_get_index(col, row)];
                GridOrb *p2_go = &p2->board[board_get_index(col, row)];

                Color p1_c = p1_go->marked ? BLACK : get_orb_render_color(p1_go->orb.type);
                Color p2_c = p2_go->marked ? BLACK : get_orb_render_color(p2_go->orb.type);

                if (p1_go->orb.type > ORB_NONE) DrawRectangleRec(p1_orb_rect, p1_c);
                if (p2_go->orb.type > ORB_NONE) DrawRectangleRec(p2_orb_rect, p2_c);
            }
        }

        // Hold
        {
            v2 padding = make_v2(10.f, 0.f);

            v2 p1_stack_pos = pf1_tl + make_v2(0., pf_wh.y) - padding;
            // v2 p2_stack_pos = pf2_tl + pf_wh                + padding;

            v2 r_dim = make_v2(10.f, 10.f);
            f32 gap = 5.f;
            for (u32 i = 0; i < p1->hold_count; ++i) {
                v2 r_offs = make_v2(-r_dim.x, -r_dim.y * i - gap);

                Rectangle r = make_rect(p1_stack_pos + r_offs, r_dim);
                DrawRectangleRec(r, get_orb_render_color(p1->hold_type));
            }
            // DrawRectangleRec(p2_hold_rect, ORANGE);
        }
    }

    EndDrawing();
}
