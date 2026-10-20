#include "ui_internal.h"
#include <math.h>

void options_button(App *a, int x, int y, int w, int h) {
    if (btn(a, x, y, w, h, T_OPTIONS, 0, KEY_F2)) {
        open_options(a);
    }
}

void draw_header(App *a) {
    GameState *g = &a->gs;
    DrawRectangle(0, 0, VW, 22, C_PANEL2);
    DrawRectangle(0, 22, VW, 1, C_BORDER);
    txt_big(T_TITLE, 8, 5, 10, C_ORANGE);
    txt(g->status == ST_SANDBOX ? TextFormat("DAY %d  SANDBOX", g->day) : TextFormat("DAY %d/%d", g->day, BAL.max_days),
        90, 6, C_TEXT);
    txt(TextFormat("GOLD %d", g->money), 190, 6, g->money < 0 ? C_BAD : C_GOLD);
    txt(TextFormat("REP %d/%d", g->rep, BAL.rep_cap), 290, 6, C_GOOD);
    txt(TextFormat("PHASE: %s", T_PHASE[g->phase]), 390, 6, g->phase == PH_OPEN ? C_ORANGE : C_TEXT);
    txt(TextFormat("GOAL %d / %d", BAL.goal_money, BAL.goal_rep), 520, 6, C_DIM);

    static const int scr_of[5] = {SCR_MAIN, SCR_MARKET, SCR_CRAFT, SCR_INV, SCR_UPG};
    static const int key_of[5] = {KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE};
    for (int i = 0; i < 5; i++) {
        int fl = (a->screen == scr_of[i] ? BF_SEL : 0) | BF_NAV;
        if (btn(a, 6 + i * 69, 26, 68, 15, T_NAV[i], fl, key_of[i])) {
            nav_to(a, scr_of[i]);
        }
    }
    if (btn(a, 352, 26, 50, 15, T_NAV[5], 0, KEY_F1)) {
        a->help_return = a->screen;
        a->help_scroll = 0;
        goto_screen(a, SCR_HELP);
    }
    if (btn(a, 404, 26, 55, 15, T_NAV[6], 0, KEY_F5)) {
        do_save(a, false);
    }
    options_button(a, 461, 26, 74, 15); /* F2 */
    bool open = g->phase == PH_OPEN;
    if (btn(a, 537, 26, 94, 15, open ? T_RESUME : T_OPEN_SHOP, g->phase == PH_PREP || open ? 0 : BF_OFF, KEY_O)) {
        try_open_shop(a);
    }
}

void draw_footer(App *a) {
    DrawRectangle(0, 344, VW, 16, C_PANEL2);
    DrawRectangle(0, 344, VW, 1, C_BORDER);
    if (a->msg_t > 0 && a->msg[0]) {
        txt_fit(a->msg, 8, 349, VW - 16, a->msg_good ? C_GOOD : C_BAD);
    } else {
        txt("Tip: buy materials when their price is below normal (green).", 8, 349, C_DIM);
    }
}

void draw_anvil(const App *a, int x, int y) {
    Color top = RGB(150, 156, 170), mid = RGB(100, 105, 118), low = RGB(66, 68, 80);
    DrawRectangle(x - 12, y + 1, 14, 5, top); /* 角笛 */
    DrawRectangle(x, y, 56, 8, top);
    DrawRectangle(x, y, 56, 2, RGB(205, 210, 224));
    DrawRectangle(x + 14, y + 8, 28, 10, mid);
    DrawRectangle(x + 8, y + 18, 40, 7, low);
    DrawRectangle(x + 10, y + 25, 36, 15, RGB(96, 62, 32)); /* 切り株 */
    DrawRectangle(x + 10, y + 25, 36, 3, RGB(130, 88, 48));
    if (a->anvil_t > 0) { /* ハンマーの振り + 光 */
        bool down = a->anvil_t < 0.3f;
        int hy = down ? y - 12 : y - 30;
        DrawRectangle(x + 30, hy + 12, 3, 16, RGB(120, 78, 40));
        DrawRectangle(x + 24, hy + 6, 15, 8, RGB(130, 134, 148));
        if (down) {
            DrawRectangle(x + 8, y - 2, 40, 2, Fade(C_GOLD, 0.8f));
        }
    } else {
        DrawRectangle(x + 40, y - 8, 3, 14, RGB(120, 78, 40));
        DrawRectangle(x + 34, y - 12, 15, 6, RGB(130, 134, 148));
    }
}

void draw_furnace(App *a, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, RGB(78, 70, 66));
    for (int r = 0; r < h; r += 10) {
        DrawRectangle(x, y + r, w, 1, RGB(40, 36, 34));
        for (int c = (r / 10 % 2) * 9; c < w; c += 18) {
            DrawRectangle(x + c, y + r, 1, 10, RGB(40, 36, 34));
        }
    }
    int ox = x + w / 5, oy = y + h / 2, ow = w * 3 / 5, oh = h / 2 - 4;
    DrawRectangle(ox, oy, ow, oh, C_BLACK);
    float t = a->t * 9;
    for (int i = 0; i < 6; i++) { /* 揺らめく炎 */
        int fw = ow / 6, fh = (int)(oh * (0.45f + 0.35f * (0.5f + 0.5f * sinf(t + i * 1.9f))));
        DrawRectangle(ox + i * fw, oy + oh - fh, fw - 1, fh, i % 2 ? C_ORANGE : RGB(230, 90, 20));
        DrawRectangle(ox + i * fw + 1, oy + oh - fh / 2, fw - 3, fh / 2, C_GOLD);
    }
    DrawRectangle(x - 3, y - 4, w + 6, 6, RGB(60, 54, 52));
    DrawRectangle(x + w / 2 - 5, y - 24, 10, 22, RGB(66, 60, 58)); /* 煙突 */
    if (GetRandomValue(0, 100) < 5) {
        spark(a, (float)ox + GetRandomValue(0, ow), (float)oy, 1, C_GOLD);
    }
}

void draw_scene(App *a, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, RGB(32, 23, 18));
    for (int r = 0; r < h - 44; r += 12) { /* レンガの壁 */
        DrawRectangle(x, y + r, w, 1, RGB(24, 17, 13));
        for (int c = (r / 12 % 2) * 12; c < w; c += 24) {
            DrawRectangle(x + c, y + r, 1, 12, RGB(24, 17, 13));
        }
    }
    DrawRectangle(x, y + h - 44, w, 44, RGB(58, 40, 26)); /* 床 */
    for (int c = 0; c < w; c += 28) {
        DrawRectangle(x + c, y + h - 44, 1, 44, RGB(40, 28, 18));
    }
    DrawRectangle(x + 14, y + 52, w - 28, 4, RGB(110, 72, 38)); /* 棚 */
    DrawRectangle(x + 14, y + 56, w - 28, 2, RGB(60, 38, 20));
    GameState *g = &a->gs;
    for (int i = 0; i < g->inv_count && i < 9; i++) {
        draw_equip_icon(g->inv[i].type, g->inv[i].main, g->inv[i].sub, x + 20 + i * 30, y + 28, 2);
    }
    if (g->inv_count == 0) {
        txt("(shelf is empty - craft something!)", x + 20, y + 38, C_DIM);
    }
    draw_furnace(a, x + 18, y + h - 112, 76, 68);
    draw_anvil(a, x + 150, y + h - 76);
    for (int m = 0; m < MAT_COUNT && m < 8; m++) { /* 素材の木箱: 2x2 のブロックで、左へ増えていく */
        if (g->mat_stock[m] > 0) {
            draw_mat_icon(m, x + w - 44 - (m / 4) * 40 + (m % 2) * 18, y + h - 44 + ((m % 4) / 2) * 18, 1);
        }
    }
}
