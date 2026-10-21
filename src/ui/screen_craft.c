#include "ui_internal.h"
#include <stdio.h>
#include <string.h>

static const char *need_text(int main, int sub, char *buf, size_t n) {
    if (main == sub) {
        snprintf(buf, n, "%dx %s", 3, MATERIALS[main].name);
    } else {
        snprintf(buf, n, "2x %s + 1x %s", MATERIALS[main].name, MATERIALS[sub].name);
    }
    return buf;
}

/* 足りない素材のうち、市場で買えないもの (hunt_only) の ID。なければ -1 (2 main + 1 sub の必要数は craft_need と同じ)
 */
static int missing_hunt_mat(const GameState *g, int main, int sub) {
    for (int m = 0; m < MAT_COUNT; m++) {
        int need = (main == m ? 2 : 0) + (sub == m ? 1 : 0);
        if (need && MATERIALS[m].hunt_only && g->mat_stock[m] < need) {
            return m;
        }
    }
    return -1;
}

static void stat_row(int x, int y, const char *label, int v, int cap, Color c) {
    txt(label, x, y, C_TEXT);
    bar(x + 34, y, 120, 9, v, cap, c);
    txt(TextFormat("%d", v), x + 162, y, C_TEXT);
}

/* 選択ボタンの 1 行: 3 スロット (ホットキー keys[0..2]) で、*sel を含むページを表示する。選択肢が 3 つを超える場合は
 * "PG n/m" ボタン (ホットキー `pkey`) が次のページへ移り、その先頭の項目を選択する。 */
enum { SEL_EQUIP, SEL_MAIN, SEL_SUB };

static void craft_selector(App *a, int kind, int count, int *sel, int yb, int ylabel, const int keys[3], int pkey,
                           const char *pname) {
    int pages = page_count(count, 3);
    if (*sel < 0 || *sel >= count) {
        *sel = 0;
    }
    int pg = *sel / 3;
    if (pages > 1 && pager_btn(a, 300, ylabel - 2, pg, pages, pkey, pname)) {
        pg = (pg + 1) % pages;
        *sel = pg * 3;
    }
    for (int i = 0; i < 3; i++) {
        int id = pg * 3 + i;
        if (id >= count) {
            break;
        }
        int x = 20 + i * 94;
        if (btn(a, x, yb, 90, 36, "", *sel == id ? BF_SEL : 0, keys[i])) {
            *sel = id;
        }
        if (kind == SEL_EQUIP) {
            bool open = equip_unlocked(a->gs.guild_done, id);
            if (open) {
                draw_equip_icon(id, a->cr_main, a->cr_sub, x + 4, yb + 6, 2);
            } else {
                draw_silhouette(EQUIPS[id].icon, x + 4, yb + 6, 2);
            }
            txt_fit(EQUIPS[id].name, x + 32, open ? yb + 14 : yb + 8, 56, open ? C_TEXT : C_DIM);
            if (!open) {
                txt("LOCKED", x + 32, yb + 20, C_BAD);
            }
        } else {
            draw_mat_icon(id, x + 4, yb + 6, 2);
            txt_fit(MATERIALS[id].name, x + 32, yb + 8, 56, C_TEXT);
            txt(TextFormat("have %d", a->gs.mat_stock[id]), x + 32, yb + 20, C_DIM);
        }
    }
}

void screen_craft(App *a) {
    GameState *g = &a->gs;
    draw_header(a);
    panel(10, 48, 300, 290, "RECIPE");
    static const int eq_keys[3] = {KEY_Q, KEY_W, KEY_E}, main_keys[3] = {KEY_A, KEY_S, KEY_D},
                     sub_keys[3] = {KEY_Z, KEY_X, KEY_C};
    txt("EQUIPMENT", 20, 66, C_DIM);
    craft_selector(a, SEL_EQUIP, EQ_COUNT, &a->cr_eq, 78, 66, eq_keys, KEY_R, "R");
    txt("MAIN MATERIAL (x2)", 20, 124, C_DIM);
    craft_selector(a, SEL_MAIN, MAT_COUNT, &a->cr_main, 136, 124, main_keys, KEY_F, "F");
    txt("SUB MATERIAL (x1)", 20, 182, C_DIM);
    craft_selector(a, SEL_SUB, MAT_COUNT, &a->cr_sub, 194, 182, sub_keys, KEY_V, "V");
    /* 金床アニメーションの隅 */
    DrawRectangle(14, 246, 292, 88, RGB(32, 23, 18));
    draw_anvil(a, 120, 276);
    draw_furnace(a, 24, 262, 56, 56);
    if (a->has_last_craft) { /* 直近の CRAFT が作ったもの (品質と特性はそのときに抽選済み) */
        const Equipment *lc = &a->last_craft;
        Color qc = quality_color(lc->quality);
        DrawRectangle(184, 250, 122, 82, C_PANEL2);
        DrawRectangleLines(184, 250, 122, 82, lc->quality > QUALITY_COMMON ? qc : C_BORDER_D);
        txt("LAST CRAFTED", 190, 254, C_DIM);
        draw_equip_icon(lc->type, lc->main, lc->sub, 192, 268, 3);
        quality_frame(lc->quality, 192, 268, 3);
        txt_fit(upper_txt(quality_name(lc->quality)), 234, 268, 68, qc);
        txt(TextFormat("Rec. %dg", lc->rec_price), 234, 282, C_GOLD);
        txt_fit(equip_name(lc), 190, 308, 112, C_TEXT);
        {
            char fx[24];
            trait_effect_text(lc->trait, lc->quality, fx, sizeof fx);
            if (fx[0]) {
                txt_fit(trait_label(lc, 112), 190, 320, 112, qc);
            } else {
                txt("no special trait", 190, 320, C_DIM);
            }
        }
    }

    panel(320, 48, 310, 290, "PREVIEW (BASE STATS)");
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = a->cr_eq;
    e.main = a->cr_main;
    e.sub = a->cr_sub;
    equipment_compute_stats(&e, g->up_level[UP_FORGE]);
    e.cost = estimate_craft_cost(g, e.main, e.sub);
    equipment_price(&e);
    draw_equip_icon(e.type, e.main, e.sub, 334, 70, 6);
    txt_fit(equip_name(&e), 414, 72, 210, C_GOLD);
    txt_fit(TextFormat("sub: %s", MATERIALS[e.sub].name), 414, 86, 210, C_DIM);
    txt(TextFormat("Forge Lv %d (x%.2f stats)", g->up_level[UP_FORGE],
                   upgrade_effect(UP_FORGE, g->up_level[UP_FORGE]) / 1000.0),
        414, 100, C_DIM);
    txt_fit(EQUIPS[e.type].desc, 414, 114, 210, C_DIM);
    txt_fit(TextFormat("%s: %s", MATERIALS[e.main].name, MATERIALS[e.main].desc), 414, 126, 210, C_DIM);
    stat_row(334, 150, "ATK", e.atk, 30, C_BAD);
    stat_row(334, 164, "DEF", e.def, 30, RGB(90, 140, 230));
    stat_row(334, 178, "MAG", e.mag, 30, C_MANA);
    stat_row(334, 192, "DUR", e.dur, 30, C_GOOD);
    stat_row(334, 206, "WT", e.wt, 14, C_STEEL);
    { /* 表示するのは確率であって結果ではない: 品質と特性はアイテムをクラフトしたときに初めて抽選される */
        int rank = guild_rank(g->guild_done), forge = g->up_level[UP_FORGE], od[QUALITY_COUNT];
        quality_rates_on_day(rank, forge, g->day, od); /* 今日のイベント (Arcane Convergence) を含む確率 */
        txt("QUALITY ODDS", 334, 218, C_DIM);
        txt_r(TextFormat("%s, Forge Lv %d", guild_rank_name(rank), forge), 618, 218, C_DIM);
        for (int q = 0; q < QUALITY_COUNT; q++) {
            txt(TextFormat("%s %d%%", quality_tag(q), od[q]), 334 + q * 72, 229, od[q] > 0 ? quality_color(q) : C_DIM);
        }
        if (day_event(g->day) == EVENT_ARCANE_CONVERGENCE) {
            const DayEventDef *ev = &DAY_EVENTS[EVENT_ARCANE_CONVERGENCE]; /* 補正の数値は表から: Common から移して
                                                                              Masterwork / Legendary へ */
            txt_fit(TextFormat("ARCANE CONVERGENCE: COM -%d  MSTR +%d  LGND +%d", ev->quality_shift_common,
                               ev->quality_add_masterwork, ev->quality_add_legendary),
                    334, 241, 286, event_color(EVENT_ARCANE_CONVERGENCE));
        }
    }
    char nb[64];
    txt(TextFormat("Needs: %s", need_text(e.main, e.sub, nb, sizeof nb)), 334, 253, C_TEXT);
    txt(TextFormat("Est. cost: %d", e.cost), 334, 264, C_TEXT);
    if (equipment_rarity_bonus(&e) > 0) { /* 希少素材 (Dragon Scale / Astral Shard) の分は価値に直接加わる */
        txt_r(TextFormat("incl. rarity +%d", equipment_rarity_bonus(&e)), 618, 264, C_GOOD);
    }
    txt(TextFormat("Rec. price (Common): %d  (profit %d)", e.rec_price, e.rec_price - e.cost), 334, 275, C_GOLD);
    Result can = can_craft(g, a->cr_eq, a->cr_main, a->cr_sub);
    if (can == R_ERR_MATERIAL) {
        int hm = missing_hunt_mat(g, a->cr_main, a->cr_sub);
        if (hm >= 0) {
            const SpecialContractDef *src = guild_reward_source(hm);
            txt_fit(
                TextFormat("Not enough %s - win it in %s contracts.", MATERIALS[hm].name, src ? src->name : "guild"),
                334, 290, 286, C_BAD);
        } else {
            txt("Not enough materials - buy more at the MARKET.", 334, 290, C_BAD);
        }
    }
    if (can == R_ERR_FULL) {
        txt("Storage is full - sell or liquidate something.", 334, 290, C_BAD);
    }
    if (can == R_ERR_LOCKED) {
        int bp = blueprint_of_equip(a->cr_eq);
        txt_fit(bp >= 0 ? TextFormat("LOCKED: %s rank (%d contracts)", GUILD_RANKS[BLUEPRINTS[bp].rank].name,
                                     GUILD_RANKS[BLUEPRINTS[bp].rank].need_done)
                        : "LOCKED.",
                334, 290, 286, C_BAD);
    }
    if (btn(a, 334, 304, 282, 28, "CRAFT [Enter]", can == R_OK ? 0 : BF_DIM, KEY_ENTER)) {
        int idx = -1;
        Result r = craft_equipment(g, a->cr_eq, a->cr_main, a->cr_sub, &idx);
        if (r == R_OK) {
            const Equipment *ne = &g->inv[idx];
            a->last_craft = *ne;
            a->has_last_craft = true;
            a->anvil_t = 0.7f;
            spark(a, 150, 282, 28, C_GOLD);
            spark(a, 150, 282, 10, C_ORANGE);
            if (ne->quality > QUALITY_FINE) {
                spark(a, 245, 290, ne->quality == QUALITY_LEGENDARY ? 40 : 20, quality_color(ne->quality));
            }
            sfx_request(&a->sfx, ne->quality >= QUALITY_MASTERWORK ? SE_CRAFT_RARE : SE_CRAFT);
            if (ne->trait) {
                say(a, true, "Crafted %s! %s. Rec. price %d gold.", equip_full_name(ne), trait_line(ne), ne->rec_price);
            } else {
                say(a, true, "Crafted %s! Rec. price %d gold.", equip_full_name(ne), ne->rec_price);
            }
        } else if (r == R_ERR_MATERIAL) {
            say(a, false, "Need %s.", need_text(a->cr_main, a->cr_sub, nb, sizeof nb));
        } else if (r == R_ERR_LOCKED) {
            int bp = blueprint_of_equip(a->cr_eq);
            say(a, false, "%s is locked: reach %s rank (%d contracts).", EQUIPS[a->cr_eq].name,
                bp >= 0 ? GUILD_RANKS[BLUEPRINTS[bp].rank].name : "?",
                bp >= 0 ? GUILD_RANKS[BLUEPRINTS[bp].rank].need_done : 0);
        } else {
            say(a, false, "%s", text_result(r));
        }
    }
    draw_footer(a);
}
