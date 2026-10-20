#include "ui_internal.h"
#include <stdio.h>

void draw_pips(int x, int y, int n, int max, Color c) {
    for (int i = 0; i < max; i++) {
        DrawRectangle(x + i * 10, y, 7, 7, i < n ? c : C_BTN_OFF);
    }
}

void screen_cust(App *a) {
    GameState *g = &a->gs;
    if (g->phase != PH_OPEN) {
        goto_screen(a, g->phase == PH_SETTLE ? SCR_SETTLE : SCR_MAIN);
        return;
    }
    draw_header(a);
    const Customer *c = &g->queue[g->queue_pos];
    const CustomerDef *d = &CUSTOMER_DEFS[c->type];

    panel(10, 48, 204, 290, TextFormat("CUSTOMER %d / %d", g->queue_pos + 1, g->queue_count));
    DrawRectangle(18, 66, 88, 88, RGB(32, 23, 18));
    draw_cust_icon(c->type, 24, 72, 6);
    txt_fit(d->name, 114, 70, 96, C_GOLD); /* 肖像が 96 px を使うので、名前は最大 NAME_MAX_LEN まで */
    txt("WANTS", 114, 88, C_DIM);
    txt_fit(EQUIPS[d->wants_eq].name, 114, 99, 96, C_TEXT);
    txt("CARES ABOUT", 114, 116, C_DIM);
    txt_fit(d->priority, 114, 127, 96, C_TEXT);
    txt(TextFormat("BUDGET  %d gold", c->budget), 20, 164, C_GOLD);
    txt("PATIENCE", 20, 180, C_DIM);
    draw_pips(80, 181, c->patience, BAL.customer_patience, C_ORANGE);
    txt(TextFormat("Required fit: %d%%+", c->min_fit), 20, 196, C_TEXT);
    txt_fit(d->desc, 20, 208, 184, C_DIM);
    if (a->react_pos == g->queue_pos && a->react[0]) {
        DrawRectangle(18, 220, 188, 52, C_TEXT);
        DrawRectangle(18, 220, 188, 52, Fade(C_TEXT, 1));
        DrawRectangleLines(18, 220, 188, 52, C_BLACK);
        txt(a->react, 24, 228, C_BLACK);
        txt(a->react_good ? "" : "Try another item or price?", 24, 248, RGB(90, 60, 40));
    } else {
        txt("\"Show me what you've got!\"", 20, 232, C_DIM);
    }

    panel(222, 48, 408, 240, "YOUR STOCK  (FIT = how well it suits this customer)");
    stock_list_input(a, 230, 82, true);
    stock_list_headers(230, 66, true);
    for (int i = 0; i < INV_ROWS; i++) {
        int idx = a->inv_scroll + i;
        if (idx >= g->inv_count) {
            break;
        }
        const Equipment *e = &g->inv[idx];
        bool match = e->type == d->wants_eq;
        stock_row(e, 230, 82 + i * 16, idx == a->sel_inv, !match, equip_fit(c->type, e), true);
    }
    if (g->inv_count == 0) {
        txt("You have nothing to sell! Send the customer away.", 240, 110, C_BAD);
    }
    if (g->inv_count > 0) { /* 選択したアイテムの全情報: 品質、最終的な FIT、特性 */
        const Equipment *se = &g->inv[a->sel_inv];
        int sfit = equip_fit(c->type, se);
        txt_fit(equip_full_name(se), 232, 260, 250, se->quality > QUALITY_COMMON ? quality_color(se->quality) : C_TEXT);
        txt_r(TextFormat("FIT %d%%  (dim rows: not wanted)", sfit), 622, 260, C_DIM);
        if (se->trait) {
            txt_fit(trait_line(se), 232, 272, 380, quality_color(se->quality));
        } else {
            txt("No special trait", 232, 272, C_DIM);
        }
    } else {
        txt("Dim rows are not what this customer wants.", 232, 274, C_DIM);
    }

    bool can_sell = g->inv_count > 0;
    if (btn(a, 222, 296, 130, 30, "SELL [Enter]", can_sell ? 0 : BF_DIM, KEY_ENTER)) {
        SaleOutcome o;
        Result r = customer_offer(g, a->sel_inv, &o);
        if (r != R_OK) {
            say(a, false, "%s", text_result(r));
        } else if (o.sold) {
            a->react[0] = 0;
            say(a, true, "Sold for %d gold! Profit %+d, reputation +%d.", o.price, o.profit, o.rep_delta);
            spark(a, 110, 110, 24, C_GOLD);
            floaty(a, 60, 150, C_GOOD, "SOLD +%d", o.price);
            sfx_request(&a->sfx, SE_SELL);
        } else {
            snprintf(a->react, sizeof a->react, "%s", text_sale(o.result));
            a->react_pos = g->queue_pos;
            a->react_good = false;
            if (o.customer_left) {
                a->react[0] = 0;
                say(a, false, "%s The customer leaves unhappy (rep %d).", text_sale(o.result), o.rep_delta);
            } else {
                say(a, false, "%s", text_sale(o.result));
            }
        }
        follow_phase(a);
    }
    if (btn(a, 360, 296, 130, 30, "SEND AWAY [D]", 0, KEY_D)) {
        customer_decline(g);
        say(a, true, "You politely send the customer away.");
        follow_phase(a);
    }
    if (btn(a, 498, 296, 132, 30, "CLOSE SHOP [E]", 0, KEY_E)) {
        day_close_early(g);
        say(a, true, "You close up early.");
        follow_phase(a);
    }
    draw_footer(a);
}
