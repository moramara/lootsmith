#include "ui_internal.h"
#include <stdio.h>

void stock_list_headers(int x, int y, bool fit) {
    txt("ITEM", x + 20, y, C_DIM);
    txt("QUAL", x + 118, y, C_DIM);
    txt_r("ATK", x + 172, y, C_DIM);
    txt_r("DEF", x + 200, y, C_DIM);
    txt_r("MAG", x + 228, y, C_DIM);
    txt_r("DUR", x + 256, y, C_DIM);
    txt_r("WT", x + 280, y, C_DIM);
    if (fit) {
        txt_r("FIT", x + 322, y, C_DIM);
    } else {
        txt_r("COST", x + 322, y, C_DIM);
    }
    txt_r("PRICE", x + 382, y, C_DIM);
}

void stock_row(const Equipment *e, int x, int y, bool sel, bool dim, int fit, bool show_fit) {
    if (sel) {
        DrawRectangle(x, y - 2, 392, 15, C_BTN_SEL);
    }
    Color c = (dim && !sel) ? C_DIM : C_TEXT;
    draw_equip_icon(e->type, e->main, e->sub, x + 3, y - 2, 1);
    quality_frame(e->quality, x + 3, y - 2, 1);
    txt_fit(equip_name(e), x + 20, y, 96, c);
    txt(quality_tag(e->quality), x + 118, y,
        e->quality > QUALITY_COMMON ? quality_color(e->quality)
        : sel                       ? C_TEXT
                                    : C_DIM);
    txt_r(TextFormat("%d", e->atk), x + 172, y, c);
    txt_r(TextFormat("%d", e->def), x + 200, y, c);
    txt_r(TextFormat("%d", e->mag), x + 228, y, c);
    txt_r(TextFormat("%d", e->dur), x + 256, y, c);
    txt_r(TextFormat("%d", e->wt), x + 280, y, c);
    if (show_fit) {
        txt_r(TextFormat("%d%%", fit), x + 322, y, fit >= 60 ? C_GOOD : fit >= 35 ? C_GOLD : C_BAD);
    } else {
        txt_r(TextFormat("%d", e->cost), x + 322, y, c);
    }
    txt_r(TextFormat("%d", e->price), x + 382, y,
          e->price > e->rec_price   ? C_ORANGE
          : e->price < e->rec_price ? C_GOOD
                                    : c);
}

/* ホイール / キーボード / クリックによる選択を処理する。戻り値はなく、a->sel_inv と a->inv_scroll を更新する */
void stock_list_input(App *a, int x, int y0, bool keys) {
    GameState *g = &a->gs;
    if (g->inv_count == 0) {
        a->sel_inv = 0;
        a->inv_scroll = 0;
        return;
    }
    if (keys && key_hit(a, KEY_DOWN)) {
        a->sel_inv++;
    }
    if (keys && key_hit(a, KEY_UP)) {
        a->sel_inv--;
    }
    float wheel = GetMouseWheelMove();
    if (in_rect(a->mouse, x, y0, 392, INV_ROWS * 16) && wheel != 0) {
        a->inv_scroll -= (int)wheel;
    }
    if (a->click && !a->consumed && in_rect(a->mouse, x, y0 - 2, 392, INV_ROWS * 16)) {
        int row = ((int)a->mouse.y - (y0 - 2)) / 16, idx = a->inv_scroll + row;
        if (idx < g->inv_count) {
            a->sel_inv = idx;
            a->consumed = true;
        }
    }
    if (a->sel_inv >= g->inv_count) {
        a->sel_inv = g->inv_count - 1;
    }
    if (a->sel_inv < 0) {
        a->sel_inv = 0;
    }
    if (a->sel_inv < a->inv_scroll) {
        a->inv_scroll = a->sel_inv;
    }
    if (a->sel_inv >= a->inv_scroll + INV_ROWS) {
        a->inv_scroll = a->sel_inv - INV_ROWS + 1;
    }
    int maxs = g->inv_count - INV_ROWS;
    if (maxs < 0) {
        maxs = 0;
    }
    if (a->inv_scroll > maxs) {
        a->inv_scroll = maxs;
    }
    if (a->inv_scroll < 0) {
        a->inv_scroll = 0;
    }
}

void screen_inv(App *a) {
    GameState *g = &a->gs;
    draw_header(a);
    panel(10, 48, 404, 290, TextFormat("STOCK  (%d/%d)", g->inv_count, MAX_INVENTORY));
    stock_list_input(a, 16, 82, true);
    stock_list_headers(16, 66, false);
    for (int i = 0; i < INV_ROWS; i++) {
        int idx = a->inv_scroll + i;
        if (idx >= g->inv_count) {
            break;
        }
        stock_row(&g->inv[idx], 16, 82 + i * 16, idx == a->sel_inv, false, 0, false);
    }
    if (g->inv_count == 0) {
        txt("Nothing in stock. Craft something first!", 30, 100, C_DIM);
    }
    if (g->inv_count > INV_ROWS) {
        txt("(mouse wheel / arrows scroll)", 20, 326 - 4, C_DIM);
    }

    panel(422, 48, 208, 290, "PRICE SETTING");
    if (g->inv_count > 0) {
        Equipment *e = &g->inv[a->sel_inv];
        int idx = a->sel_inv;
        draw_equip_icon(e->type, e->main, e->sub, 432, 68, 4);
        quality_frame(e->quality, 432, 68, 4);
        txt_fit(equip_name(e), 486, 70, 138, C_GOLD);
        txt_fit(TextFormat("sub: %s", MATERIALS[e->sub].name), 486, 84, 138, C_DIM);
        txt_fit(upper_txt(quality_name(e->quality)), 486, 98, 138,
                e->quality > QUALITY_COMMON ? quality_color(e->quality) : C_TEXT);
        if (e->trait) {
            txt_fit(trait_line(e), 432, 120, 190, quality_color(e->quality));
        } else {
            txt("No special trait", 432, 120, C_DIM);
        }
        txt(TextFormat("Cost:         %d", e->cost), 432, 136, C_TEXT);
        txt(TextFormat("Recommended:  %d", e->rec_price), 432, 150, C_TEXT);
        txt_big(TextFormat("%d", e->price), 432, 168, 20, e->price > e->rec_price ? C_ORANGE : C_GOLD);
        txt(TextFormat("profit %+d  (%+d%% vs rec.)", e->price - e->cost,
                       (e->price - e->rec_price) * 100 / e->rec_price),
            432, 194, e->price >= e->cost ? C_GOOD : C_BAD);
        bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        int fine = shift ? 10 : 1;
        int d = 0;
        if (btn(a, 432, 212, 44, 20, "-10", 0, 0)) {
            d = -10;
        }
        if (btn(a, 480, 212, 44, 20, "-1", 0, 0)) {
            d = -1;
        }
        if (btn(a, 528, 212, 44, 20, "+1", 0, 0)) {
            d = 1;
        }
        if (btn(a, 576, 212, 44, 20, "+10", 0, 0)) {
            d = 10;
        }
        if (!a->consumed && (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT))) {
            d = -fine;
            a->consumed = true;
        }
        if (!a->consumed && (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT))) {
            d = fine;
            a->consumed = true;
        }
        if (d) {
            Result r = inv_adjust_price(g, idx, d);
            if (r != R_OK) {
                say(a, false, "%s", text_result(r));
            }
        }
        if (btn(a, 432, 238, 188, 20, "RECOMMENDED [R]", e->price == e->rec_price ? BF_DIM : 0, KEY_R)) {
            inv_reset_price(g, idx);
            say(a, true, "Price reset to %d.", g->inv[idx].rec_price);
        }
        int lv = liquidation_value(e);
        if (btn(a, 432, 262, 188, 20, TextFormat("LIQUIDATE +%d [L]", lv), 0, KEY_L)) {
            int got = 0;
            const char *nm = TextFormat("%s", equip_name(e));
            char nbuf[48];
            snprintf(nbuf, sizeof nbuf, "%s", nm);
            Result r = inv_liquidate(g, idx, &got);
            if (r == R_OK) {
                say(a, true, "Liquidated %s for %d gold (emergency cash).", nbuf, got);
                sfx_request(&a->sfx, SE_COIN);
            } else {
                say(a, false, "%s", text_result(r));
            }
        }
        txt("Left/Right: price (Shift = x10)", 432, 292, C_DIM);
        txt("Liquidation pays 50% of cost.", 432, 304, C_DIM);
        if (equipment_rarity_bonus(e) > 0) { /* 希少素材の価値ボーナスは Recommended に含まれる */
            txt_fit(TextFormat("Value includes rarity +%d", equipment_rarity_bonus(e)), 432, 316, 190, C_GOOD);
        }
    }
    draw_footer(a);
}
