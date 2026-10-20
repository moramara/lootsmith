#include "ui_internal.h"

static const float BOT_STEP_SECONDS[3] = {0.72f, 0.32f, 0.11f};

static const char *bot_phase_name(const GameState *g) {
    if (g->status == ST_CLEARED) {
        return "CAMPAIGN CLEARED";
    }
    if (g->status == ST_FAILED) {
        return "CAMPAIGN FAILED";
    }
    if (g->phase == PH_PREP) {
        return "PREPARING STOCK";
    }
    if (g->phase == PH_OPEN) {
        return "SERVING CUSTOMERS";
    }
    return "DAY REPORT";
}

void screen_bot(App *a) {
    BotRunner *bot = &a->bot;
    GameState *g = &bot->game;
    DrawRectangle(0, 0, VW, VH, C_BG);
    txt_c("BOT WATCH  /  BALANCED STRATEGY + GUILD", VW / 2, 8, 12, C_GOLD);
    txt(TextFormat("SEED %u", bot->seed), 12, 21, C_DIM);
    txt(TextFormat("SPEED %s", a->bot_speed == 0 ? "SLOW" : a->bot_speed == 1 ? "NORMAL" : "FAST"), 520, 21,
        C_DIM);

    panel(8, 36, 270, 278, "CAMPAIGN STATUS");
    txt(TextFormat("DAY %02d / %d", g->day, BAL.max_days), 18, 57, C_TEXT);
    txt(bot_phase_name(g), 18, 73, g->status == ST_CLEARED ? C_GOOD : g->status == ST_FAILED ? C_BAD : C_GOLD);
    txt(TextFormat("GOLD  %d / %d", g->money, BAL.goal_money), 18, 96, C_GOLD);
    bar(18, 109, 244, 8, g->money, BAL.goal_money, C_GOLD);
    txt(TextFormat("REP   %d / %d", g->rep, BAL.goal_rep), 18, 127, C_GOOD);
    bar(18, 140, 244, 8, g->rep, BAL.goal_rep, C_GOOD);
    txt(TextFormat("Items in stock: %d / %d", g->inv_count, MAX_INVENTORY), 18, 159, C_TEXT);
    txt(TextFormat("Customers served: %d", g->total_sold), 18, 175, C_TEXT);
    txt(TextFormat("Total revenue: %d", g->total_revenue), 18, 191, C_TEXT);
    txt(TextFormat("Guild contracts: %d done / %d expired", g->guild_done, g->guild_expired), 18, 207, C_TEXT);
    txt(TextFormat("Forge Lv.%d   Store Lv.%d   Supply Lv.%d", g->up_level[UP_FORGE], g->up_level[UP_STORE],
                   g->up_level[UP_SUPPLY]),
        18, 223, C_TEXT);
    txt("MATERIALS", 18, 245, C_DIM);
    for (int m = 0; m < MAT_COUNT; ++m) {
        int col = m % 3, row = m / 3;
        int x = 18 + col * 83, y = 263 + row * 17;
        txt_fit(TextFormat("%s x%d", MATERIALS[m].name, g->mat_stock[m]), x, y, 80, C_TEXT);
    }
    if (g->phase == PH_OPEN && g->queue_pos < g->queue_count) {
        txt_fit(TextFormat("Next: %s", CUSTOMER_DEFS[g->queue[g->queue_pos].type].name), 18, 300, 240, C_ORANGE);
    }

    panel(286, 36, 346, 278, "RECENT BOT ACTIONS");
    for (int row = 0; row < bot->log_count; ++row) {
        int idx = bot->log_count - 1 - row; /* 古いログから表示 */
        Color c = row == bot->log_count - 1 ? C_GOLD : C_TEXT;
        txt_fit(bot->log[idx], 296, 61 + row * 28, 326, c);
    }
    if (bot->finished) {
        txt_c("Playback finished. Restart or return to title.", 459, 299, 9, C_DIM);
    } else if (a->bot_paused) {
        txt_c("PAUSED - use STEP to advance one action.", 459, 299, 9, C_ORANGE);
    } else {
        txt_c("Simulation runs independently from your save.", 459, 299, 9, C_DIM);
    }

    const char *pause_label = bot->finished ? "FINISHED" : a->bot_paused ? "RESUME [SPACE]" : "PAUSE [SPACE]";
    if (btn(a, 10, 326, 108, 22, pause_label, bot->finished ? BF_OFF : 0, KEY_SPACE)) {
        a->bot_paused = !a->bot_paused;
    }
    if (bot->finished) {
        if (btn(a, 124, 326, 92, 22, "RESTART [R]", 0, KEY_R)) {
            bot_init(&a->bot, bot->seed);
            a->bot_paused = false;
            a->bot_accum = 0;
        }
    } else if (btn(a, 124, 326, 92, 22, "STEP [RIGHT]", a->bot_paused ? 0 : BF_OFF, KEY_RIGHT)) {
        bot_step(bot);
        a->bot_accum = 0;
    }
    if (btn(a, 222, 326, 88, 22, "SLOWER [-]", a->bot_speed == 0 ? BF_OFF : 0, KEY_MINUS)) {
        a->bot_speed--;
        a->bot_accum = 0;
    }
    if (btn(a, 316, 326, 88, 22, "FASTER [=]", a->bot_speed == 2 ? BF_OFF : 0, KEY_EQUAL)) {
        a->bot_speed++;
        a->bot_accum = 0;
    }
    if (btn(a, 520, 326, 110, 22, "TITLE [ESC]", 0, KEY_ESCAPE)) {
        a->bot_paused = true;
        goto_screen(a, SCR_TITLE);
    }

    if (!a->bot_paused && !bot->finished) {
        a->bot_accum += GetFrameTime();
        if (a->bot_accum >= BOT_STEP_SECONDS[a->bot_speed]) {
            a->bot_accum = 0;
            bot_step(bot);
        }
    }
}
