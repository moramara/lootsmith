#include "ui_internal.h"
#include <stdio.h>
#include <string.h>

/* =====================================================================
 * ギルド: 掲示板、受注済みの契約、納品。ルールはすべて core/ (guild_*) にあり、ここでは表示と、プレイヤーの
 * 選択 (契約スロット + 実際の在庫添字) を API に渡すだけ。
 * ===================================================================== */
enum { GL_ROWS = 3 }; /* 一度に見える納品候補の数 (それ以上はスクロールする) */

static Color diff_color(int d) {
    return d == GD_EASY ? C_GOOD : d == GD_STANDARD ? C_GOLD : C_BAD;
}

const char *upper_txt(const char *s) { /* バナーやランク行用に、ASCII を大文字にして循環バッファに入れる */
    static char buf[4][64];
    static int k;
    char *o = buf[k++ & 3];
    size_t i = 0;
    for (; s[i] && i < 63; i++) {
        o[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    }
    o[i] = 0;
    return o;
}

/* 契約の特別報酬 ("+1 Dragon Scale")。報酬のない契約 (通常契約) では空文字列 */
static const char *reward_text(const GuildContract *c) {
    if (c->reward_qty < 1 || c->reward_mat < 0 || c->reward_mat >= MAT_COUNT) {
        return "";
    }
    return TextFormat("+%d %s", c->reward_qty, MATERIALS[c->reward_mat].name);
}

/* ギルド画面と設計図画面で共有するランク行 (すべて guild_done から導出する) */
static void draw_rank_strip(const GameState *g, int x, int y, int w) {
    int done = g->guild_done, rank = guild_rank(done), rem = guild_next_rank_remaining(done),
        nb = guild_next_blueprint(done);
    panel(x, y, w, 24, NULL);
    txt_fit(TextFormat("RANK: %s", upper_txt(guild_rank_name(rank))), x + 8, y + 8, 146, C_GOLD);
    if (rem >= 0) {
        int lo = guild_rank_need(rank), hi = guild_rank_need(rank + 1);
        txt(TextFormat("CONTRACTS: %d / %d", done, hi), x + 162, y + 8, C_TEXT);
        DrawRectangle(x + 280, y + 8, 56, 8, C_BLACK);
        DrawRectangle(x + 280, y + 8, 56 * (done - lo) / (hi - lo > 0 ? hi - lo : 1), 8, C_GOLD);
        DrawRectangleLines(x + 280, y + 8, 56, 8, C_BORDER_D);
        if (nb >= 0) {
            txt_fit(TextFormat("NEXT BLUEPRINT: %s", EQUIPS[BLUEPRINTS[nb].equip].name), x + 346, y + 8,
                    w - 346 - (w > 400 ? 106 : 8), C_TEXT);
        }
    } else {
        txt(TextFormat("CONTRACTS: %d", done), x + 162, y + 8, C_TEXT);
        txt("MAX RANK", x + 280, y + 8, C_GOLD);
    }
}

/* ランクアップを起こした納品が表示するバナー。UI にだけ存在し、時間が経てば自然に消える */
static void rank_up_banner(App *a, int before, int after) {
    snprintf(a->banner_a, sizeof a->banner_a, "GUILD RANK UP! %s", upper_txt(guild_rank_name(after)));
    a->banner_b[0] = 0;
    for (int b = 0; b < BP_COUNT; b++) {
        if (BLUEPRINTS[b].rank <= before || BLUEPRINTS[b].rank > after) {
            continue;
        }
        size_t l = strlen(a->banner_b);
        snprintf(a->banner_b + l, sizeof a->banner_b - l, "%s%s%s", l ? ", " : "BLUEPRINT UNLOCKED: ", "",
                 upper_txt(EQUIPS[BLUEPRINTS[b].equip].name));
    }
    a->banner_t = 5.0f;
}

void draw_banner(App *a) {
    if (a->banner_t <= 0) {
        return;
    }
    float al = a->banner_t < 0.6f ? a->banner_t / 0.6f : 1.0f;
    int h = a->banner_b[0] ? 44 : 30, y = 66;
    DrawRectangle(110, y, 420, h, Fade(C_BLACK, 0.88f * al));
    DrawRectangleLines(110, y, 420, h, Fade(C_GOLD, al));
    DrawRectangleLines(111, y + 1, 418, h - 2, Fade(C_BORDER_D, al));
    txt_c(a->banner_a, VW / 2, y + 7, 10, Fade(C_GOLD, al));
    if (a->banner_b[0]) {
        txt_c(a->banner_b, VW / 2, y + 25, 10, Fade(WHITE, al));
    }
}

void screen_guild(App *a) {
    GameState *g = &a->gs;
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        goto_screen(a, screen_for_state(g));
        return;
    }
    draw_header(a);
    draw_rank_strip(g, 10, 48, 620);
    if (btn(a, 532, 50, 94, 20, "BLUEPRINTS [B]", 0, KEY_B)) {
        nav_to(a, SCR_BLUEPRINT);
        return;
    }

    /* ---- 掲示板 ---- */
    panel(10, 74, 300, 264, "NOTICE BOARD");
    txt_r(TextFormat("held %d/%d", g->guild_active_count, GUILD_ACTIVE_MAX), 304, 76, C_DIM);
    static const int acc_keys[GUILD_OFFER_COUNT] = {KEY_Q, KEY_W, KEY_E};
    if (g->guild_board_count == 0) {
        txt_c("No more contracts today.", 160, 160, 10, C_DIM);
    }
    for (int i = 0; i < g->guild_board_count; i++) {
        const GuildContract *c = &g->guild_board[i];
        const CustomerDef *cd = &CUSTOMER_DEFS[c->customer_type];
        int y = 90 + i * 82;
        bool hunt = c->kind == GUILD_CONTRACT_DRAGON_HUNT; /* 特別契約: 色と表示で通常契約と区別する */
        DrawRectangle(18, y, 284, 78, hunt ? RGB(58, 30, 24) : C_PANEL2);
        DrawRectangleLines(18, y, 284, 78, hunt ? C_ORANGE : C_BORDER_D);
        draw_cust_icon(c->customer_type, 24, y + 8, 4);
        if (hunt) {
            txt(T_DRAGON_HUNT_TAG, 80, y + 4, C_ORANGE);
            txt_fit(cd->name, 152, y + 4, 80, C_DIM);
        } else {
            txt_fit(cd->name, 80, y + 4, 130, C_GOLD);
        }
        txt_fit(TextFormat("wants %s", EQUIPS[cd->wants_eq].name), 80, y + 15, 150, C_TEXT);
        draw_equip_icon(cd->wants_eq, -1, -1, 270, y + 4, 2);
        txt(TextFormat("FIT %d%%+", c->min_fit), 80, y + 27, C_TEXT);
        txt(GUILD_DIFFS[c->difficulty].name, 150, y + 27, diff_color(c->difficulty));
        txt(TextFormat("%d gold + %d rep", c->gold, c->rep), 80, y + 39, C_GOLD);
        txt(TextFormat("%d days", c->days), 80, y + 51, C_DIM);
        if (hunt) {
            txt_fit(reward_text(c), 80, y + 63, 120, C_GOOD); /* 納品に成功したときだけ受け取れる特別報酬 */
        }
        bool full = g->guild_active_count >= GUILD_ACTIVE_MAX;
        if (btn(a, 206, y + 52, 90, 20, TextFormat("ACCEPT [%c]", "QWE"[i]), full ? BF_DIM : 0, acc_keys[i])) {
            GuildResult r = guild_accept(g, i); /* ルール (と上限) はシミュレーション側で検査する */
            if (r == GUILD_OK) {
                a->gl_sel = g->guild_active_count - 1;
                a->gl_item = -1;
                a->gl_scroll = 0;
                say(a, true, "%s accepted: %s wants a %s. Due day %d.", hunt ? GUILD_DRAGON_HUNT.name : "Contract",
                    cd->name, EQUIPS[cd->wants_eq].name, g->guild_active[a->gl_sel].deadline_day);
                spark(a, 250, (float)y + 40, 8, C_GOLD);
            } else {
                say(a, false, "%s", text_guild(r));
            }
            break; /* 掲示板がずれた: このフレームでは再び触らない */
        }
    }

    /* ---- 受注済みの契約 ---- */
    panel(320, 74, 310, 90, "MY CONTRACTS");
    if (a->gl_sel >= g->guild_active_count) {
        a->gl_sel = g->guild_active_count - 1;
    }
    if (a->gl_sel < 0) {
        a->gl_sel = 0;
    }
    for (int i = 0; i < GUILD_ACTIVE_MAX; i++) {
        int y = 90 + i * 36;
        bool have = i < g->guild_active_count, sel = have && a->gl_sel == i;
        bool hunt_row = have && g->guild_active[i].kind == GUILD_CONTRACT_DRAGON_HUNT;
        DrawRectangle(326, y, 298, 33, sel ? C_BTN_SEL : hunt_row ? RGB(58, 30, 24) : C_PANEL2);
        DrawRectangleLines(326, y, 298, 33, sel ? C_GOLD : hunt_row ? C_ORANGE : C_BORDER_D);
        if (!have) {
            txt("(empty slot)", 334, y + 12, C_DIM);
            continue;
        }
        const GuildContract *c = &g->guild_active[i];
        const CustomerDef *cd = &CUSTOMER_DEFS[c->customer_type];
        if (a->click && !a->consumed && in_rect(a->mouse, 326, y, 298, 33)) {
            a->gl_sel = i;
            a->gl_item = -1;
            a->gl_scroll = 0;
            a->consumed = true;
        }
        draw_cust_icon(c->customer_type, 330, y + 4, 2);
        if (hunt_row) {
            txt_fit(TextFormat("%s  FIT %d%%+", T_DRAGON_HUNT_TAG, c->min_fit), 360, y + 5, 170, C_ORANGE);
        } else {
            txt_fit(TextFormat("%s: %s  FIT %d%%+", cd->name, EQUIPS[cd->wants_eq].name, c->min_fit), 360, y + 5, 170,
                    C_TEXT);
        }
        txt(TextFormat("DUE DAY %d", c->deadline_day), 360, y + 18,
            c->deadline_day <= g->day ? (sel ? WHITE : C_ORANGE) : (sel ? C_TEXT : C_DIM));
        txt_r(TextFormat("%dg +%d", c->gold, c->rep), 618, y + 5, C_GOLD);
        if (hunt_row) {
            txt_r(reward_text(c), 618, y + 18, C_GOOD);
        } else {
            txt_r(GUILD_DIFFS[c->difficulty].name, 618, y + 18, diff_color(c->difficulty));
        }
    }

    /* ---- 納品 ---- */
    panel(320, 168, 310, 170, "DELIVER");
    bool have = g->guild_active_count > 0;
    int cand[MAX_INVENTORY], nc = 0, cur = -1;
    if (!have) {
        txt("Accept a contract from the notice board.", 332, 188, C_DIM);
    } else {
        const GuildContract *k = &g->guild_active[a->gl_sel];
        const CustomerDef *cd = &CUSTOMER_DEFS[k->customer_type];
        bool hunt_k = k->kind == GUILD_CONTRACT_DRAGON_HUNT;
        if (hunt_k) { /* 受注後の詳細: 依頼主・必要装備・最低 FIT・残り期限・特別報酬 */
            int left = k->deadline_day - g->day + 1;
            bool blocked = k->reward_mat >= 0 && k->reward_mat < MAT_COUNT &&
                           g->mat_stock[k->reward_mat] + k->reward_qty > BAL.stock_cap;
            txt_fit(TextFormat("%s: %s wants a %s, FIT %d%%+", T_DRAGON_HUNT_TAG, cd->name, EQUIPS[cd->wants_eq].name,
                               k->min_fit),
                    332, 184, 290, C_ORANGE);
            txt_fit(TextFormat("Due day %d (%d day%s left)", k->deadline_day, left, left == 1 ? "" : "s"), 332, 197,
                    128, left <= 1 ? C_ORANGE : C_DIM);
            if (blocked) { /* 在庫上限: この状態では納品できない (guild_deliver が GUILD_ERR_REWARD_FULL を返す) */
                txt_r(TextFormat("%s STOCK FULL!", upper_txt(MATERIALS[k->reward_mat].name)), 622, 197, C_BAD);
            } else {
                txt_r(TextFormat("%dg +%d rep  %s", k->gold, k->rep, reward_text(k)), 622, 197, C_GOOD);
            }
        } else {
            txt_fit(TextFormat("%s wants a %s with FIT %d%%+", cd->name, EQUIPS[cd->wants_eq].name, k->min_fit), 332,
                    184, 290, C_TEXT);
            txt(TextFormat("Due day %d (today is day %d).  Reward: %d gold + %d rep", k->deadline_day, g->day, k->gold,
                           k->rep),
                332, 197, C_DIM);
        }
        for (int i = 0; i < g->inv_count; i++) {
            if (guild_item_fit(g, a->gl_sel, i) >= k->min_fit) {
                cand[nc++] = i; /* 納品できるものだけ */
            }
        }
        for (int j = 0; j < nc; j++) {
            if (cand[j] == a->gl_item) {
                cur = j;
            }
        }
        if (cur < 0) {
            cur = nc > 0 ? 0 : -1;
            a->gl_item = nc > 0 ? cand[0] : -1;
        }
        if (nc > 0) {
            if (key_hit(a, KEY_DOWN)) {
                cur = (cur + 1) % nc;
            }
            if (key_hit(a, KEY_UP)) {
                cur = (cur + nc - 1) % nc;
            }
            a->gl_item = cand[cur];
            int maxs = nc > GL_ROWS ? nc - GL_ROWS : 0;
            float w = GetMouseWheelMove();
            if (w != 0 && in_rect(a->mouse, 326, 210, 298, 54)) {
                a->gl_scroll -= w > 0 ? 1 : -1;
            }
            if (cur < a->gl_scroll) {
                a->gl_scroll = cur;
            }
            if (cur >= a->gl_scroll + GL_ROWS) {
                a->gl_scroll = cur - GL_ROWS + 1;
            }
            if (a->gl_scroll > maxs) {
                a->gl_scroll = maxs;
            }
            if (a->gl_scroll < 0) {
                a->gl_scroll = 0;
            }
            txt("ITEM", 332, 212, C_DIM);
            txt("QUAL", 428, 212, C_DIM);
            txt("TRAIT", 466, 212, C_DIM);
            txt_r("FIT", 612, 212, C_DIM);
            txt_r(TextFormat("%d qualify", nc), 560, 212, C_DIM);
            for (int r = 0; r < GL_ROWS && a->gl_scroll + r < nc; r++) {
                int j = a->gl_scroll + r, ry = 225 + r * 13;
                const Equipment *e = &g->inv[cand[j]];
                if (j == cur) {
                    DrawRectangle(326, ry - 1, 292, 12, C_BTN_SEL);
                }
                if (a->click && !a->consumed && in_rect(a->mouse, 326, ry - 1, 292, 12)) {
                    a->gl_item = cand[j];
                    a->consumed = true;
                }
                Color tc = j == cur ? WHITE : C_TEXT;
                txt_fit(equip_name(e), 332, ry, 92, tc);
                txt(quality_tag(e->quality), 428, ry, e->quality > QUALITY_COMMON ? quality_color(e->quality) : C_DIM);
                {
                    const char *lab = trait_label(e, 110);
                    txt_fit(lab[0] ? lab : "-", 466, ry, 110, lab[0] ? quality_color(e->quality) : C_DIM);
                }
                txt_r(TextFormat("%d%%", guild_item_fit(g, a->gl_sel, cand[j])), 612, ry, tc);
            }
            if (maxs > 0) {
                int th = 40 * GL_ROWS / nc, ty = 224 + (40 - th) * a->gl_scroll / maxs;
                DrawRectangle(620, 224, 4, 40, C_BLACK);
                DrawRectangle(620, ty, 4, th, C_BORDER);
            }
            { /* ハイライトされた候補の全情報 */
                const Equipment *ce = &g->inv[a->gl_item];
                txt_fit(equip_full_name(ce), 332, 267, 286,
                        ce->quality > QUALITY_COMMON ? quality_color(ce->quality) : C_TEXT);
                if (ce->trait) {
                    txt_fit(trait_line(ce), 332, 277, 286, quality_color(ce->quality));
                } else {
                    txt("No special trait", 332, 277, C_DIM);
                }
                txt_fit(TextFormat("ATK %d  DEF %d  MAG %d  DUR %d  WT %d", ce->atk, ce->def, ce->mag, ce->dur, ce->wt),
                        332, 287, 286, C_DIM);
            }
        } else {
            int best = -1;
            for (int i = 0; i < g->inv_count; i++) {
                int f = guild_item_fit(g, a->gl_sel, i);
                if (f > best) {
                    best = f;
                }
            }
            txt("No suitable item in stock.", 332, 214, C_BAD);
            txt_fit(
                TextFormat("Needs: %s with FIT %d%%+ (for the %s).", EQUIPS[cd->wants_eq].name, k->min_fit, cd->name),
                332, 227, 290, C_TEXT);
            if (best >= 0) {
                txt(TextFormat("Best %s in stock: FIT %d%%", EQUIPS[cd->wants_eq].name, best), 332, 240, C_DIM);
            }
            txt("Craft one on the CRAFT screen, then come back.", 332, 253, C_DIM);
            if (btn(a, 332, 268, 150, 20, "TO CRAFT [C]", 0, KEY_C)) {
                a->cr_eq = cd->wants_eq;
                nav_to(a, SCR_CRAFT);
            }
        }
    }
    if (btn(a, 332, 300, 190, 30, "DELIVER [Enter]", nc > 0 ? 0 : BF_DIM, KEY_ENTER)) {
        if (!have) {
            say(a, false, "Accept a contract first.");
        } else if (nc == 0) {
            say(a, false, "No suitable item - craft one first.");
        } else {
            Equipment e = g->inv[a->gl_item];
            GuildOutcome o; /* 候補リストから取った、実際の在庫添字 */
            int rank_before = guild_rank(g->guild_done);
            GuildResult r = guild_deliver(g, a->gl_sel, a->gl_item, &o);
            if (r == GUILD_OK) {
                if (o.reward_qty > 0) { /* Dragon Hunt: 特別報酬の獲得を知らせる (納品が成功したときだけ) */
                    const MaterialDef *rm = &MATERIALS[o.reward_mat];
                    say(a, true, "Delivered %s! +%d gold, +%d rep, +%d %s!", equip_full_name(&e), o.gold, o.rep_gain,
                        o.reward_qty, rm->name);
                    snprintf(a->banner_a, sizeof a->banner_a, "%s COMPLETE!", T_DRAGON_HUNT_TAG);
                    snprintf(a->banner_b, sizeof a->banner_b, "+%d %s (now %d)", o.reward_qty, upper_txt(rm->name),
                             g->mat_stock[o.reward_mat]);
                    a->banner_t = 5.0f;
                    spark(a, 420, 300, 40, icol(rm->color));
                } else {
                    say(a, true, "Delivered %s! +%d gold, +%d rep.", equip_full_name(&e), o.gold, o.rep_gain);
                }
                spark(a, 420, 300, 24, C_GOLD);
                int rank_after = guild_rank(g->guild_done);
                if (rank_after > rank_before) {
                    rank_up_banner(a, rank_before, rank_after);
                    spark(a, 320, 90, 40, C_GOLD);
                } /* 前後を比較する: 何も保存しない */
                sfx_request(
                    &a->sfx,
                    rank_after > rank_before
                        ? SE_RANK_UP
                        : SE_CONTRACT); /* ランクアップは契約のファンファーレの代わりに鳴る。両方鳴ることはない */
                a->gl_item = -1;
                a->gl_scroll = 0; /* 添字がずれた: 新しいリストから選び直す */
            } else {
                say(a, false, "%s", text_guild(r));
            }
        }
    }
    if (btn(a, 530, 300, 94, 30, T_BACK, BF_BACK, 0)) {
        goto_screen(a, SCR_MAIN); /* Esc はすべてのサブ画面について ui_frame で処理する */
    }
    draw_footer(a);
}

/* ---------- 設計図の一覧: 未解放のものも見える ---------- */
enum { BP_PER_PAGE = 4 };

void screen_blueprints(App *a) {
    GameState *g = &a->gs;
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        goto_screen(a, screen_for_state(g));
        return;
    }
    draw_header(a);
    draw_rank_strip(g, 10, 48, 620);
    panel(10, 74, 620, 264, "BLUEPRINTS");
    int pages = page_count(BP_COUNT, BP_PER_PAGE);
    a->pg_bp = clamp_page(a->pg_bp, pages);
    if (pages > 1 && pager_btn(a, 626, 75, a->pg_bp, pages, 0, NULL)) {
        a->pg_bp = (a->pg_bp + 1) % pages;
    }
    for (int k = 0; k < BP_PER_PAGE; k++) {
        int b = a->pg_bp * BP_PER_PAGE + k;
        if (b >= BP_COUNT) {
            break;
        }
        const BlueprintDef *d = &BLUEPRINTS[b];
        bool open = blueprint_unlocked(g->guild_done, b);
        int y = 90 + k * 58, need = GUILD_RANKS[d->rank].need_done;
        DrawRectangle(18, y, 604, 54, open ? C_PANEL2 : C_PANEL);
        DrawRectangleLines(18, y, 604, 54, open ? C_GOLD : C_BORDER_D);
        if (open) {
            draw_equip_icon(d->equip, MAT_IRON, MAT_WOOD, 26, y + 3, 4);
        } else {
            draw_silhouette(EQUIPS[d->equip].icon, 26, y + 3, 4);
        }
        txt_fit(d->name, 84, y + 4, 200, open ? C_GOLD : C_DIM);
        txt_r(open ? "UNLOCKED" : "LOCKED", 610, y + 4, open ? C_GOOD : C_BAD);
        txt_fit(TextFormat("Equipment: %s     Customer: %s", EQUIPS[d->equip].name, CUSTOMER_DEFS[d->customer].name),
                84, y + 15, 440, C_TEXT);
        txt_fit(TextFormat("Unlocks at: %s rank (%d contracts)", GUILD_RANKS[d->rank].name, need), 84, y + 26, 440,
                C_TEXT);
        if (open) {
            txt("Ready: craft it on the CRAFT screen; its customers visit your shop.", 84, y + 37, C_GOOD);
        } else {
            txt_fit(TextFormat("%d more contract(s) to deliver (%d / %d)", need - g->guild_done, g->guild_done, need),
                    84, y + 37, 440, C_BAD);
        }
        if (open) {
            draw_cust_icon(d->customer, 580, y + 18, 2);
        } else {
            draw_silhouette(CUSTOMER_DEFS[d->customer].icon, 580, y + 18, 2);
        }
    }
    if (btn(a, 16, 312, 100, 22, T_BACK, BF_BACK, 0)) {
        goto_screen(a, SCR_GUILD); /* Esc も (ui_frame) */
    }
    draw_footer(a);
}
