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

static inline void clear_player_state(PlayerState *p) {
    // Clear everything
    memset(p, 0, sizeof(PlayerState));

    // Set initial position to be middle col
    p->at_col = (u8) PLAYERFIELD_COLS / 2;

    // Initialize hold stack and field stacks
    p->hold_count = 0;
    p->hold_color = CELL_EMPTY;
    for (u32 s = 0; s < PLAYERFIELD_COLS; ++s) {
        p->field_stacks[s].count = 0;
        p->field_stacks[s].cap = PLAYERFIELD_STACK_SIZE;
    }
}

static inline CellState random_cell(void)  {
    return (CellState) GetRandomValue(CELL_PINK, CELL_GREEN);
}

static inline CellState stack_peek(CellStack *s) {
    ASSERT(s->count > 0);
    CellState result = s->cells[s->count - 1];
    return result;
}

static inline CellState stack_pop(CellStack *s) {
    ASSERT(s->count > 0);
    CellState result = s->cells[--s->count];
    return result;
}

static inline CellState stack_pop_n(CellStack *s, u32 n) {
    ASSERT(s->count >= n);

    CellState result = stack_peek(s);
    s->count -= n;

    return result;
}

static inline void stack_push(CellStack *s, CellState c) {
    ASSERT(s->count < s->cap);
    s->cells[s->count++] = c;
}

static inline void stack_push_n(CellStack *s, CellState c, u32 n) {
    ASSERT((s->count + n) <= s->cap);

    for (u32 i = 0; i < n; ++i) {
        stack_push(s, c);
    }
}

static inline void initialize_playfield(PlayerState *p, u32 rows) {
    for (u32 i = 0; i < PLAYERFIELD_COLS; ++i) {
        CellStack *s = &p->field_stacks[i];
        for (u32 r = 0; r < rows && r < PLAYERFIELD_STACK_SIZE; ++r) {
            if (s->count >= s->cap) break;
            stack_push(s, random_cell());
        }
    }
}

static inline void attempt_to_pull_stack(PlayerState *p) {
    ASSERT(p->at_col >= 0 && p->at_col < PLAYERFIELD_COLS);

    CellStack *fs = &p->field_stacks[p->at_col];
    if (fs->count < 1) return; // Nothing to pull

    // Get the reference cell
    CellState ref;
    if (p->hold_count > 0) ref = p->hold_color;
    else                   ref = stack_peek(fs);

    // Track how far back we can pull
    CellStack copy = *fs;
    u32 pull_count = 0;
    while (copy.count > 0) {
        CellState comp = stack_pop(&copy);
        if (comp != ref) break;
        pull_count++;
    }

    // Pull and update player hold
    if (pull_count > 0) {
        stack_pop_n(fs, pull_count);
        p->hold_count += pull_count;
        p->hold_color = ref;
    }
}

static inline void attempt_to_push_stack(PlayerState *p) {
    ASSERT(p->at_col >= 0 && p->at_col < PLAYERFIELD_COLS);

    if (p->hold_count < 1) return; // Nothing to push

    CellStack *fs = &p->field_stacks[p->at_col];
    if ((fs->count + p->hold_count) >= fs->cap) return; // Disallow pushing past stack fill

    // Push and update player hold
    stack_push_n(fs, p->hold_color, p->hold_count);
    p->hold_count = 0;
    p->hold_color = CELL_EMPTY;
}

static void update_game(GameState *game, GameInput *new_input) {
    if (!game->initialized) {
        clear_player_state(&game->player_1);
        clear_player_state(&game->player_2);

        initialize_playfield(&game->player_1, 4);
        initialize_playfield(&game->player_2, 4);

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
    v2 pf_gutter_wh = make_v2(pf_wh.x, pf_wh.y - (cell_wh.y * PLAYERFIELD_STACK_SIZE));

    // Visualize grid
    {
        for (u32 row = 0; row < PLAYERFIELD_STACK_SIZE; ++row) {
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

        f32 pf_gutter_offset  = pf_wh.y - pf_gutter_wh.y;
        v2  center            = cell_wh * 0.5f;
        v2  player_rect       = make_v2(10, 10);
        v2  player_rect_center = player_rect * -0.5f;

        v2 player1_pos = pf1_tl + make_v2(p1->at_col * cell_wh.x, pf_gutter_offset) + center + player_rect_center;
        Rectangle player1_rect = make_rect(player1_pos, player_rect);
        DrawRectangleRec(player1_rect, ORANGE);

        v2 cs_pad = make_v2(4, 4);
        v2 cs_dim = cell_wh - (cs_pad * 2.);

        for (u32 col = 0; col < PLAYERFIELD_COLS; ++col) {
            CellStack *s = &p1->field_stacks[col];
            for (u32 cell = 0; cell < s->count; ++cell) {
                v2 pos = pf1_tl + hadamard(make_v2(col, cell), cell_wh);
                Rectangle cell_rect = make_rect(pos + cs_pad, cs_dim);
                DrawRectangleRec(cell_rect, get_cell_render_color(s->cells[cell]));
            }
        }
    }

    EndDrawing();
}
