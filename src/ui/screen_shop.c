#include "ui_internal.h"

static int max_affordable(const GameState *g, int m) {
    int q = 0;
    while (q < BAL.stock_cap - g->mat_stock[m] && market_quote(g, m, q + 1) <= g->money) {
        q++;
    }
    return q;
}

#define MK_VISIBLE 3 /* 市場画面に収まる素材の行 (1 行 88 px) */

void screen_market(App *a) {
    GameState *g = &a->gs;
    draw_header(a);
    panel(10, 48, 620, 290, "MATERIAL MARKET");
    txt("Up/Down: row   Left/Right: amount   Enter: buy", 134, 50, C_DIM);
    /* 素材が行数より多い場合: リストがスクロールする (ホイール、矢印、^ / v ボタン)。選択行は常に見える。 */
    int maxs = MAT_COUNT > MK_VISIBLE ? MAT_COUNT - MK_VISIBLE : 0;
    {
        int sup = upgrade_effect(UP_SUPPLY, g->up_level[UP_SUPPLY]), ev = event_market_discount_pct(g->day);
        if (ev > 0) { /* Market Caravan: Supplier の割引と併用 (最終価格は 2 つを掛け合わせて 1 回だけ丸める) */
            txt_r(TextFormat("Supplier %d%% + CARAVAN %d%%", sup, ev), maxs > 0 ? 540 : 580, 50,
                  event_color(EVENT_MARKET_CARAVAN));
        } else {
            txt(TextFormat("Supplier discount: %d%%", sup), maxs > 0 ? 404 : 440, 50, C_TEXT);
        }
    }
    if (maxs > 0) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0 && in_rect(a->mouse, 10, 48, 620, 290)) {
            a->mk_scroll -= wheel > 0 ? 1 : -1;
        }
        txt_r(TextFormat("%d-%d/%d", a->mk_scroll + 1, a->mk_scroll + MK_VISIBLE, MAT_COUNT), 580, 50, C_DIM);
        if (btn(a, 586, 49, 12, 12, "^", a->mk_scroll <= 0 ? BF_DIM : 0, 0)) {
            a->mk_scroll--;
        }
        if (btn(a, 602, 49, 12, 12, "v", a->mk_scroll >= maxs ? BF_DIM : 0, 0)) {
            a->mk_scroll++;
        }
    }
    if (a->mk_scroll > maxs) {
        a->mk_scroll = maxs;
    }
    if (a->mk_scroll < 0) {
        a->mk_scroll = 0;
    }
    if (a->mk_row < a->mk_scroll) {
        a->mk_row = a->mk_scroll; /* ホイールがウィンドウを動かした: 選択をその中に保つ */
    }
    if (a->mk_row >= a->mk_scroll + MK_VISIBLE) {
        a->mk_row = a->mk_scroll + MK_VISIBLE - 1;
    }
    if (a->mk_row >= MAT_COUNT) {
        a->mk_row = MAT_COUNT - 1;
    }
    if (key_hit(a, KEY_DOWN)) {
        a->mk_row = (a->mk_row + 1) % MAT_COUNT;
    }
    if (key_hit(a, KEY_UP)) {
        a->mk_row = (a->mk_row + MAT_COUNT - 1) % MAT_COUNT;
    }
    if (a->mk_row < a->mk_scroll) {
        a->mk_scroll = a->mk_row; /* キーが選択を動かした: ウィンドウが追従する */
    }
    if (a->mk_row >= a->mk_scroll + MK_VISIBLE) {
        a->mk_scroll = a->mk_row - MK_VISIBLE + 1;
    }
    if (maxs > 0) { /* スクロールバー */
        int th = 258 * MK_VISIBLE / MAT_COUNT, ty = 66 + (258 - th) * a->mk_scroll / maxs;
        DrawRectangle(624, 66, 4, 258, C_BLACK);
        DrawRectangle(624, ty, 4, th, C_BORDER);
    }

    for (int m = a->mk_scroll; m < MAT_COUNT && m < a->mk_scroll + MK_VISIBLE; m++) {
        int y = 66 + (m - a->mk_scroll) * 88, sel = (a->mk_row == m);
        DrawRectangle(18, y, 604, 82, sel ? C_PANEL2 : C_PANEL);
        DrawRectangleLines(18, y, 604, 82, sel ? C_GOLD : C_BORDER_D);
        if (a->click && !a->consumed && in_rect(a->mouse, 18, y, 604, 82)) {
            a->mk_row = m;
        }
        draw_mat_icon(m, 28, y + 14, 4);
        txt_fit(MATERIALS[m].name, 84, y + 8, 110, C_TEXT);
        if (MATERIALS[m].hunt_only) { /* 市場では買えない素材: 価格も購入操作もなく、入手経路だけを見せる */
            txt_big(T_HUNT_ONLY, 84, y + 22, 20, C_ORANGE);
            txt("Not for sale.", 84, y + 46, C_DIM);
            const SpecialContractDef *src = guild_reward_source(m); /* この素材を報酬にする特別契約 (表から引く) */
            txt("Guild reward", 84, y + 60, C_GOLD);
            txt(TextFormat("In stock: %d", g->mat_stock[m]), 200, y + 14, g->mat_stock[m] ? C_GOOD : C_TEXT);
            txt(g->mat_stock[m] ? "Avg cost: 0 (free)" : "Avg cost: -", 200, y + 28, C_DIM);
            txt_fit(MATERIALS[m].desc, 200, y + 66, 410, C_DIM);
            txt_fit(TextFormat("%s: deliver a %s", src ? src->name : "?",
                               src ? EQUIPS[CUSTOMER_DEFS[src->customer].wants_eq].name : "?"),
                    320, y + 14, 290, C_TEXT);
            txt_fit(TextFormat("(%s or higher, %d%% chance per day)",
                               src ? GUILD_RANKS[guild_rank(src->min_done)].name : "?", src ? src->spawn_pct : 0),
                    320, y + 28, 290, C_DIM);
            /* ボタンは見かけだけ: 押しても (Enter でも) core の market_buy() が R_ERR_NOT_FOR_SALE を返すだけで、
             * 在庫・所持金・価格には何も起きない */
            if (btn(a, 320, y + 38, 120, 22, T_HUNT_ONLY, BF_DIM, 0) || (sel && key_hit(a, KEY_ENTER))) {
                say(a, false, "%s", text_result(market_buy(g, m, 1)));
            }
            continue;
        }
        txt_big(TextFormat("%d", g->price[m]), 84, y + 22, 20, trend_col(g->price[m], MATERIALS[m].base_price));
        txt(TextFormat("base %d  min %d", MATERIALS[m].base_price, MATERIALS[m].min_price), 84, y + 46, C_DIM);
        int cheap = g->price[m] * 100 <= MATERIALS[m].base_price * 93,
            dear = g->price[m] * 100 >= MATERIALS[m].base_price * 107;
        txt(TextFormat("%s (%s)",
                       cheap  ? "CHEAP!"
                       : dear ? "PRICEY"
                              : "normal",
                       trend_txt(g->price[m], g->prev_price[m])),
            84, y + 60,
            cheap  ? C_GOOD
            : dear ? C_BAD
                   : C_TEXT);
        int avg = g->mat_stock[m] ? g->mat_cost[m] / g->mat_stock[m] : 0;
        txt(TextFormat("In stock: %d", g->mat_stock[m]), 200, y + 14, C_TEXT);
        txt(g->mat_stock[m] ? TextFormat("Avg cost: %d", avg) : "Avg cost: -", 200, y + 28, C_DIM);
        txt_fit(MATERIALS[m].desc, 200, y + 66, 410, C_DIM);
        { /* 通常価格 -> 最終価格。表示・見積もり・購入は同じ core の式 (market_unit_price / market_quote) を通る */
            int sup = upgrade_effect(UP_SUPPLY, g->up_level[UP_SUPPLY]), ev = event_market_discount_pct(g->day),
                fin = market_unit_price(g, m);
            txt(TextFormat("Final: %d each", fin), 200, y + 42, fin < g->price[m] ? C_GOOD : C_TEXT);
            if (sup > 0 || ev > 0) {
                txt_fit(sup > 0 && ev > 0 ? TextFormat("Sup -%d%%  Event -%d%%", sup, ev)
                        : ev > 0          ? TextFormat("Event -%d%%", ev)
                                          : TextFormat("Supplier -%d%%", sup),
                        200, y + 54, 116, event_color(ev > 0 ? EVENT_MARKET_CARAVAN : EVENT_NONE));
            }
        }

        int *q = &a->mk_qty[m];
        int bx = 320;
        if (btn(a, bx, y + 10, 24, 18, "-", 0, 0) || (sel && key_hit(a, KEY_LEFT))) {
            if (*q > 1) {
                (*q)--;
            }
        }
        txt_c(TextFormat("%d", *q), bx + 52, y + 14, 10, C_GOLD);
        if (btn(a, bx + 80, y + 10, 24, 18, "+", 0, 0) || (sel && key_hit(a, KEY_RIGHT))) {
            if (*q < BAL.stock_cap) {
                (*q)++;
            }
        }
        if (btn(a, bx + 110, y + 10, 34, 18, "+5", 0, 0)) {
            *q += 5;
            if (*q > BAL.stock_cap) {
                *q = BAL.stock_cap;
            }
        }
        if (btn(a, bx + 148, y + 10, 40, 18, "MAX", 0, 0)) {
            int mx = max_affordable(g, m);
            *q = mx > 0 ? mx : 1;
        }
        int total = market_quote(g, m, *q);
        bool afford = total >= 0 && total <= g->money && g->mat_stock[m] + *q <= BAL.stock_cap;
        txt(TextFormat("Total: %d gold", total), bx, y + 40, afford ? C_GOLD : C_BAD);
        if (btn(a, bx + 150, y + 38, 100, 22, "BUY", afford ? 0 : BF_DIM, 0) || (sel && key_hit(a, KEY_ENTER))) {
            Result r = market_buy(g, m, *q);
            if (r == R_OK) {
                say(a, true, "Bought %d %s for %d gold.", *q, MATERIALS[m].name, total);
                spark(a, 250, (float)y + 40, 6, icol(MATERIALS[m].color));
                sfx_request(&a->sfx, SE_COIN);
            } else {
                say(a, false, "%s", text_result(r));
            }
        }
    }
    draw_footer(a);
}

#define UP_PER_PAGE 3 /* 1 ページあたりのアップグレードカード (各 204 px 幅) */

void screen_upgrade(App *a) {
    GameState *g = &a->gs;
    draw_header(a);
    int pages = page_count(UP_COUNT, UP_PER_PAGE);
    a->pg_up = clamp_page(a->pg_up, pages);
    if (pages > 1) { /* 左右でページをめくる。別ページのカードのホットキーはそのページへジャンプする */
        if (key_hit(a, KEY_RIGHT)) {
            a->pg_up = (a->pg_up + 1) % pages;
        }
        if (key_hit(a, KEY_LEFT)) {
            a->pg_up = (a->pg_up + pages - 1) % pages;
        }
        for (int u = 0; u < UP_COUNT; u++) {
            if (UPGRADES[u].hotkey && u / UP_PER_PAGE != a->pg_up && key_hit(a, UPGRADES[u].hotkey)) {
                a->pg_up = u / UP_PER_PAGE;
            }
        }
    }
    for (int k = 0; k < UP_PER_PAGE; k++) {
        int u = a->pg_up * UP_PER_PAGE + k;
        if (u >= UP_COUNT) {
            break;
        }
        const UpgradeDef *d = &UPGRADES[u];
        int x = 10 + k * 208;
        panel(x, 48, 204, 290, d->name);
        int lv = g->up_level[u], mx = d->max_level;
        txt_fit(TextFormat("%s:", d->desc), x + 10, 68, 184, C_DIM);
        txt(TextFormat("LEVEL %d / %d", lv, mx), x + 10, 90, C_TEXT);
        draw_pips(x + 90, 91, lv, mx, C_GOLD);
        char now[48], nxt[48];
        upgrade_effect_text(u, lv, now, sizeof now);
        txt("NOW", x + 10, 120, C_DIM);
        txt_fit(now, x + 10, 132, 184, C_GOOD);
        int cost = upgrade_cost(g, u);
        if (cost >= 0) {
            upgrade_effect_text(u, lv + 1, nxt, sizeof nxt);
            txt("NEXT LEVEL", x + 10, 160, C_DIM);
            txt_fit(nxt, x + 10, 172, 184, C_GOLD);
            txt(TextFormat("Cost: %d gold", cost), x + 10, 204, g->money >= cost ? C_TEXT : C_BAD);
            if (g->money < cost) {
                txt(TextFormat("Need %d more gold", cost - g->money), x + 10, 218, C_BAD);
            }
        } else {
            txt("MAX LEVEL REACHED", x + 10, 172, C_GOLD);
        }
        const char *lbl = cost < 0 ? "MAX LEVEL" : d->hotkey ? TextFormat("UPGRADE [%c]", d->hotkey) : "UPGRADE";
        if (btn(a, x + 10, 290, 184, 30, lbl, (cost < 0 || g->money < cost) ? BF_DIM : 0,
                d->hotkey)) { /* ホットキー 'A'..'Z' == raylib の KEY_A..KEY_Z */
            Result r = upgrade_buy(g, u);
            if (r == R_OK) {
                say(a, true, "%s upgraded to level %d!", d->name, g->up_level[u]);
                spark(a, (float)x + 100, 300, 30, C_GOLD);
                sfx_request(&a->sfx, SE_UPGRADE);
            } else {
                say(a, false, "%s", text_result(r));
            }
        }
    }
    if (pages > 1 && pager_btn(a, 626, 49, a->pg_up, pages, 0, NULL)) {
        a->pg_up = (a->pg_up + 1) % pages;
    }
    draw_footer(a);
}
