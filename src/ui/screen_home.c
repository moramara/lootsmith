#include "ui_internal.h"

void screen_title(App *a) {
    DrawRectangle(0, 0, VW, VH, C_BG);
    for (int i = 0; i < 40; i++) { /* 残り火 */
        if (GetRandomValue(0, 1000) < 6) {
            spark(a, (float)GetRandomValue(150, 490), 300, 1, GetRandomValue(0, 1) ? C_ORANGE : C_GOLD);
        }
    }
    draw_furnace(a, 40, 200, 110, 110);
    draw_anvil(a, 470, 262);
    draw_equip_icon(EQ_SWORD, MAT_IRON, MAT_WOOD, 120, 110, 6);
    draw_equip_icon(EQ_SHIELD, MAT_WOOD, MAT_IRON, 430, 110, 6);
    draw_equip_icon(EQ_STAFF, MAT_MANA, MAT_IRON, 520, 120, 5);
    txt_c(T_TITLE, VW / 2 + 3, 43, 40, C_BLACK);
    txt_c(T_TITLE, VW / 2, 40, 40, C_ORANGE);
    txt_c(T_SUBTITLE, VW / 2, 86, 10, C_GOLD);
    int bx = VW / 2 - 70;
    if (btn(a, bx, 122, 140, 24, T_NEW_GAME, 0, KEY_N)) {
        new_game(a);
    }
    if (btn(a, bx, 150, 140, 24, T_CONTINUE, a->has_save ? 0 : BF_OFF, KEY_C)) {
        do_load(a);
    }
    if (btn(a, bx, 178, 140, 24, T_HELP, 0, KEY_H)) {
        a->help_return = SCR_TITLE;
        a->help_scroll = 0;
        goto_screen(a, SCR_HELP);
    }
    options_button(a, bx, 206, 140, 24); /* F2 */
    if (btn(a, bx, 234, 140, 24, T_QUIT, 0, KEY_Q)) {
        a->quit = true;
    }
    if (btn(a, bx, 262, 140, 24, "WATCH BOT [B]", 0, KEY_B)) {
        app_start_bot(a);
    }
    if (!a->has_save) {
        txt_c("(no save file yet)", VW / 2, 294, 10, C_DIM);
    }
    if (a->msg_t > 0) {
        txt_c(a->msg, VW / 2, 306, 10, a->msg_good ? C_GOOD : C_BAD);
    }
    txt_c("Reach 1500 gold and 80 reputation in 30 days.", VW / 2, 330, 10, C_DIM);
}

void screen_main(App *a) {
    GameState *g = &a->gs;
    draw_header(a);
    panel(10, 48, 300, 196, "THE FORGE");
    draw_scene(a, 11, 62, 298, 181);

    panel(320, 48, 310, 196, "SHOP STATUS");
    { /* 今日のイベント (日付から導く。イベントがない日は "No event" だけ) */
        int ev = day_event(g->day);
        txt_r(ev == EVENT_NONE ? "No event" : TextFormat("EVENT: %s", upper_txt(DAY_EVENTS[ev].name)), 624, 51,
              event_color(ev));
    }
    int x = 330, y = 66;
    txt(TextFormat("GOLD  %d / %d", g->money, BAL.goal_money), x, y, C_GOLD);
    bar(x + 130, y, 160, 9, g->money, BAL.goal_money, C_GOLD);
    txt(TextFormat("REP   %d / %d", g->rep, BAL.goal_rep), x, y + 16, C_GOOD);
    bar(x + 130, y + 16, 160, 9, g->rep, BAL.goal_rep, C_GOOD);
    txt(g->status == ST_SANDBOX ? "Sandbox: no deadline" : TextFormat("Days left: %d", BAL.max_days - g->day), x,
        y + 34, C_TEXT);
    txt(TextFormat("Upkeep: %d gold / day", BAL.upkeep), x + 130, y + 34, C_DIM);
    txt("MATERIALS", x, y + 54, C_DIM);
    int mrows = MAT_COUNT > 3 ? 2 : 1, mper = mrows * 3,
        mpages = page_count(MAT_COUNT, mper); /* 3 列、1〜2 行。それ以上はページ送り */
    a->pg_main = clamp_page(a->pg_main, mpages);
    if (mpages > 1 && pager_btn(a, x + 290, y + 51, a->pg_main, mpages, 0, NULL)) {
        a->pg_main = (a->pg_main + 1) % mpages;
    }
    for (int k = 0; k < mper; k++) {
        int m = a->pg_main * mper + k;
        if (m >= MAT_COUNT) {
            break;
        }
        int cx = x + (k % 3) * 98, cy = y + 66 + (k / 3) * 24;
        draw_mat_icon(m, cx, cy, 2);
        txt_fit(TextFormat("%s x%d", MATERIALS[m].name, g->mat_stock[m]), cx + 26, cy + 6, 70, C_TEXT);
    }
    int sh = (mrows - 1) * 24; /* 以下の行はグリッドに合わせて下へ動く */
    txt(TextFormat("Equipment in stock: %d / %d", g->inv_count, MAX_INVENTORY), x, y + 100 + sh, C_TEXT);
    txt(TextFormat("Customers per day: %d", customers_per_day(g)), x, y + 114 + sh, C_TEXT);
    txt(TextFormat("Today: sold %d, revenue %d", g->report.sold, g->report.revenue), x, y + 128 + sh, C_TEXT);
    txt(TextFormat("Total: %d sold, %d revenue", g->total_sold, g->total_revenue), x, y + 142 + sh, C_DIM);

    bool open = g->phase == PH_OPEN;
    if (btn(a, 10, 252, 150, 24, open ? T_RESUME : T_OPEN_SHOP, g->phase == PH_PREP || open ? 0 : BF_OFF, 0)) {
        try_open_shop(a);
    }
    if (btn(a, 168, 252, 90, 24, T_SAVE, 0, 0)) {
        do_save(a, false);
    }
    if (btn(a, 266, 252, 90, 24, T_TO_TITLE, 0, 0)) {
        do_save(a, true);
        goto_screen(a, SCR_TITLE);
    }
    if (btn(a, 364, 252, 90, 24, "GUILD [G]", g->phase == PH_PREP ? 0 : BF_DIM, KEY_G)) {
        nav_to(a, SCR_GUILD); /* キーはボタンの中にある: 二重に反応しない */
    }
    if (g->guild_active_count > 0) {
        int due = g->guild_active[0].deadline_day;
        for (int i = 1; i < g->guild_active_count; i++) {
            if (g->guild_active[i].deadline_day < due) {
                due = g->guild_active[i].deadline_day;
            }
        }
        txt_fit(TextFormat("Guild: %d held, due day %d", g->guild_active_count, due), 462, 260, 164,
                due <= g->day ? C_ORANGE : C_GOLD);
    } else {
        txt("Plan: buy, craft, price, open!", 462, 260, C_DIM);
    }
    if (day_event(g->day) != EVENT_NONE) { /* 効果の説明は 1 行で (固定幅に収める) */
        txt_fit(DAY_EVENTS[day_event(g->day)].effect, 462, 272, 168, event_color(day_event(g->day)));
    }

    panel(10, 284, 620, 54, "TODAY'S MARKET");
    int nmk = market_mat_count(), spages = page_count(nmk, 3); /* 1 ページに市場の素材 3 つ (買えない素材は並べない) */
    a->pg_strip = clamp_page(a->pg_strip, spages);
    if (spages > 1 && pager_btn(a, 626, 285, a->pg_strip, spages, 0, NULL)) {
        a->pg_strip = (a->pg_strip + 1) % spages;
    }
    for (int k = 0; k < 3; k++) {
        if (a->pg_strip * 3 + k >= nmk) {
            break;
        }
        int m = market_mat_at(a->pg_strip * 3 + k);
        int px = 20 + k * 205;
        draw_mat_icon(m, px, 302, 2);
        txt_fit(MATERIALS[m].name, px + 28, 301, 170, C_TEXT);
        txt_big(TextFormat("%d", g->price[m]), px + 28, 313, 20, trend_col(g->price[m], MATERIALS[m].base_price));
        txt(TextFormat("(%s vs yesterday)", trend_txt(g->price[m], g->prev_price[m])), px + 70, 318, C_DIM);
    }
}
