#include "ui_internal.h"

void screen_settle(App *a) {
    GameState *g = &a->gs;
    const DayReport *r = &g->report;
    DrawRectangle(0, 0, VW, VH, C_BG);
    options_button(a, 528, 6, 104, 15);
    panel(120, 24, 400, 312, TextFormat("DAY %d - SETTLEMENT", g->day));
    int x = 140, y = 48;
    txt(TextFormat("Customers served ......... %d", r->customers), x, y, C_TEXT);
    txt(TextFormat("Items sold ............... %d", r->sold), x, y + 16, C_TEXT);
    txt(TextFormat("Revenue .................. %d", r->revenue), x, y + 32, C_GOLD);
    txt(TextFormat("Cost of goods ............ -%d", r->cogs), x, y + 48, C_TEXT);
    txt(TextFormat("Upkeep ................... -%d", r->upkeep), x, y + 64, C_BAD);
    int net = net_of(r);
    txt_big(TextFormat("NET %s%d", net >= 0 ? "+" : "", net), x, y + 88, 20, net >= 0 ? C_GOOD : C_BAD);
    txt(TextFormat("Reputation change ........ %+d  (now %d)", r->rep_delta, g->rep), x, y + 118, C_TEXT);
    txt(TextFormat("Gold: %d -> %d", r->money_start, g->money), x, y + 134, C_GOLD);
    if (r->guild_gold > 0 || r->guild_done > 0 || r->guild_expired > 0) { /* ギルドの数値。販売とは分けて扱う */
        txt("GUILD", x + 215, y, C_DIM);
        txt(TextFormat("Rewards  +%d", r->guild_gold), x + 215, y + 16, C_GOLD);
        txt(TextFormat("Delivered  %d", r->guild_done), x + 215, y + 32, C_TEXT);
        txt(TextFormat("Expired  %d", r->guild_expired), x + 215, y + 48, r->guild_expired ? C_BAD : C_TEXT);
    }
    txt("TOMORROW'S MARKET", x, y + 160, C_DIM);
    int nmk = market_mat_count(); /* 市場に並ぶ素材だけ (買えない素材に価格はない) */
    bool compact =
        nmk > 3; /* 素材は最大 3 つまで: 大きなアイコンで 1 行。それ以上は 2 行のコンパクト表示でページ送り */
    int per = compact ? 6 : 3, pitch = compact ? 14 : 26, pages = page_count(nmk, per);
    a->pg_settle = clamp_page(a->pg_settle, pages);
    if (pages > 1 && pager_btn(a, x + 360, y + 157, a->pg_settle, pages, 0, NULL)) {
        a->pg_settle = (a->pg_settle + 1) % pages;
    }
    for (int k = 0; k < per; k++) {
        if (a->pg_settle * per + k >= nmk) {
            break;
        }
        int m = market_mat_at(a->pg_settle * per + k);
        int cx = x + (k % 3) * 118, cy = y + 174 + (k / 3) * pitch;
        const char *label =
            TextFormat("%s %d (%s)", MATERIALS[m].name, g->price[m], trend_txt(g->price[m], g->prev_price[m]));
        draw_mat_icon(m, cx, cy, compact ? 1 : 2);
        txt_fit(label, cx + (compact ? 16 : 26), cy + (compact ? 1 : 6), compact ? 100 : 90,
                trend_col(g->price[m], MATERIALS[m].base_price));
    }
    int lift = compact ? 2 : 0; /* 下の 2 行はグリッドに合わせて動く */
    if (g->status == ST_PLAYING && g->day >= BAL.max_days - 4) {
        txt(TextFormat("Only %d day(s) left!", BAL.max_days - g->day), x, y + 208 + lift, C_ORANGE);
    }
    if (g->money < 0) {
        txt("You are in debt! Liquidate stock if things get worse.", x, y + 224 + lift, C_BAD);
    }
    const char *lbl = g->status == ST_CLEARED ? "VICTORY! [Enter]" : g->status == ST_FAILED ? T_RESULT : T_NEXT_DAY;
    if (btn(a, 220, 296, 200, 30, lbl, 0, KEY_ENTER)) {
        if (g->status == ST_CLEARED) {
            goto_screen(a, SCR_CLEAR);
        } else if (g->status == ST_FAILED) {
            goto_screen(a, SCR_OVER);
        } else {
            Result res = day_advance(g);
            if (res == R_OK) {
                do_save(a, true);
                say(a, true, "Day %d begins. (autosaved)", g->day);
                goto_screen(a, SCR_MAIN);
                sfx_request(&a->sfx, SE_DAY_START);
            } else {
                say(a, false, "%s", text_result(res));
            }
        }
    }
    if (a->msg_t > 0) {
        txt_fit(a->msg, 140, 330 - 6, 360, a->msg_good ? C_GOOD : C_BAD);
    }
}

static void result_stats(const App *a, int x, int y) {
    const GameState *g = &a->gs;
    txt(TextFormat("Final gold ............ %d", g->money), x, y, C_GOLD);
    txt(TextFormat("Reputation ............ %d", g->rep), x, y + 16, C_GOOD);
    txt(TextFormat("Total revenue ......... %d", g->total_revenue), x, y + 32, C_TEXT);
    txt(TextFormat("Items sold ............ %d", g->total_sold), x, y + 48, C_TEXT);
    txt(TextFormat("Total net profit ...... %d", g->total_profit), x, y + 64, C_TEXT);
    txt(TextFormat("Days reached .......... %d / %d", g->day, BAL.max_days), x, y + 80, C_TEXT);
}

void screen_clear(App *a) {
    DrawRectangle(0, 0, VW, VH, C_BG);
    if (GetRandomValue(0, 100) < 40) {
        spark(a, (float)GetRandomValue(100, 540), 80, 3, GetRandomValue(0, 1) ? C_GOLD : C_ORANGE);
    }
    options_button(a, 528, 6, 104, 15);
    txt_c("CAMPAIGN CLEAR!", VW / 2 + 2, 42, 30, C_BLACK);
    txt_c("CAMPAIGN CLEAR!", VW / 2, 40, 30, C_GOLD);
    txt_c("Your forge is the finest in the land.", VW / 2, 82, 10, C_TEXT);
    panel(190, 110, 260, 124, "FINAL RESULTS");
    result_stats(a, 204, 132);
    if (btn(a, 190, 254, 260, 26, T_SANDBOX, 0, KEY_ENTER)) {
        Result r = campaign_continue_sandbox(&a->gs);
        if (r == R_OK) {
            say(a, true, "Sandbox mode: no deadline, no bankruptcy.");
            goto_screen(a, SCR_MAIN);
        }
    }
    if (btn(a, 190, 286, 260, 26, T_TO_TITLE, 0, KEY_ESCAPE)) {
        goto_screen(a, SCR_TITLE);
    }
}

void screen_over(App *a) {
    DrawRectangle(0, 0, VW, VH, C_BG);
    options_button(a, 528, 6, 104, 15);
    txt_c("GAME OVER", VW / 2 + 2, 42, 30, C_BLACK);
    txt_c("GAME OVER", VW / 2, 40, 30, C_BAD);
    const GameState *g = &a->gs;
    txt_c(g->fail_reason == FAIL_BANKRUPT ? "Bankrupt! The forge goes cold..."
                                          : "Time is up - the goals were not reached.",
          VW / 2, 82, 10, C_TEXT);
    panel(190, 110, 260, 124, "FINAL RESULTS");
    result_stats(a, 204, 132);
    if (btn(a, 190, 254, 260, 26, T_RETRY, 0, KEY_ENTER)) {
        new_game(a);
    }
    if (btn(a, 190, 286, 260, 26, T_TO_TITLE, 0, KEY_ESCAPE)) {
        goto_screen(a, SCR_TITLE);
    }
}
