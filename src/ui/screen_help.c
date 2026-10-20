#include "ui_internal.h"
#include <stdio.h>
#include <string.h>

/* ヘルプの文章は T_HELP_LINES (text.h) から取る。コンテンツの一覧はそこへ直接は書かない: マーカー
 * "@CUSTOMERS" / "@UPGRADES" (単独の行: テーブルの 1 行につき 1 行へ展開される) と
 * "@MATS" (行の途中: 素材名) を、コンテンツテーブルから埋める。 */
#define HELP_GEN_LINES                                                                                                 \
    (CUST_TYPE_COUNT + UP_COUNT + GD_COUNT + BP_COUNT + QUALITY_COUNT + EQ_TRAIT_COUNT + RANK_COUNT + 8)

#define HELP_LINE_LEN 160

static void help_append(char *dst, size_t n, const char *src) {
    size_t l = strlen(dst);
    if (l + 1 < n) {
        snprintf(dst + l, n - l, "%s", src);
    }
}

static int help_build(const char **out, char (*store)[HELP_LINE_LEN]) {
    int n = 0, sn = 0;
    for (int i = 0; i < T_HELP_LINE_COUNT; i++) {
        const char *l = T_HELP_LINES[i];
        if (!strcmp(l, "@CUSTOMERS")) {
            for (int c = 0; c < CUST_TYPE_COUNT; c++) {
                const CustomerDef *d = &CUSTOMER_DEFS[c];
                snprintf(store[sn], HELP_LINE_LEN, "  %s: %s (%s)", d->name, EQUIPS[d->wants_eq].name, d->priority);
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@UPGRADES")) {
            for (int u = 0; u < UP_COUNT; u++) {
                char lo[48], hi[48];
                upgrade_effect_text(u, 0, lo, sizeof lo);
                upgrade_effect_text(u, UPGRADES[u].max_level, hi, sizeof hi);
                snprintf(store[sn], HELP_LINE_LEN, "             %s: %s (%s -> %s)", UPGRADES[u].name, UPGRADES[u].desc,
                         lo, hi);
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@GUILDDIFFS")) {
            for (int d = 0; d < GD_COUNT; d++) {
                const GuildDifficultyDef *x = &GUILD_DIFFS[d];
                snprintf(store[sn], HELP_LINE_LEN, "             %s: FIT %d+, %d gold + %d rep, %d days", x->name,
                         x->min_fit, x->gold, x->rep, x->days);
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@DRAGONHUNT")) {
            const DragonHuntDef *h = &GUILD_DRAGON_HUNT;
            snprintf(store[sn], HELP_LINE_LEN,
                     "  %-10s Mythic Smith only: %d%% chance per morning, 1 at most on the board.", "DRAGON HUNT",
                     h->spawn_pct);
            out[n++] = store[sn++];
            snprintf(store[sn], HELP_LINE_LEN, "             %s wants a %s, FIT %d+, %d gold + %d rep, %d days.",
                     CUSTOMER_DEFS[h->customer].name, EQUIPS[CUSTOMER_DEFS[h->customer].wants_eq].name, h->min_fit,
                     h->gold, h->rep, h->days);
            out[n++] = store[sn++];
            snprintf(store[sn], HELP_LINE_LEN, "             Reward: +%d %s on delivery (not sold; free, cost 0).",
                     h->reward_qty, MATERIALS[h->reward_mat].name);
            out[n++] = store[sn++];
            snprintf(store[sn], HELP_LINE_LEN,
                     "             No room (%s at %d) = delivery refused. Use it as craft material.",
                     MATERIALS[h->reward_mat].name, BAL.stock_cap);
            out[n++] = store[sn++];
        } else if (!strcmp(l, "@QUALITIES")) {
            for (int q = 0; q < QUALITY_COUNT; q++) {
                snprintf(store[sn], HELP_LINE_LEN, "             %s: x%d.%02d%s", QUALITY_DEFS[q].name,
                         QUALITY_DEFS[q].mult_pm / 1000, QUALITY_DEFS[q].mult_pm % 1000 / 10,
                         QUALITY_DEFS[q].has_trait ? "  + one trait" : "");
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@TRAITS")) {
            for (int t = EQ_TRAIT_NONE + 1; t < EQ_TRAIT_COUNT; t++) {
                char lo[24] = "", hi[24] = "";
                for (int q = 0; q < QUALITY_COUNT; q++) {
                    if (QUALITY_DEFS[q].has_trait) {
                        char *dst = (q == QUALITY_MASTERWORK) ? lo : hi;
                        trait_effect_text(t, q, dst, 24);
                    }
                }
                snprintf(store[sn], HELP_LINE_LEN, "             %s: %s  (%s %s, %s %s)", TRAIT_DEFS[t].name,
                         TRAIT_DEFS[t].desc, QUALITY_DEFS[QUALITY_MASTERWORK].name, lo,
                         QUALITY_DEFS[QUALITY_LEGENDARY].name, hi);
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@QUALITYODDS")) {
            for (int r = 0; r < RANK_COUNT; r++) {
                snprintf(store[sn], HELP_LINE_LEN, "             %s:", GUILD_RANKS[r].name);
                for (int q = 0; q < QUALITY_COUNT; q++) {
                    char part[24];
                    snprintf(part, sizeof part, "%s%d", q ? " / " : " ", QUALITY_RATE_PCT[r][q]);
                    help_append(store[sn], HELP_LINE_LEN, part);
                }
                out[n++] = store[sn++];
            }
        } else if (!strcmp(l, "@BLUEPRINTS")) {
            for (int b = 0; b < BP_COUNT; b++) {
                const BlueprintDef *x = &BLUEPRINTS[b];
                snprintf(store[sn], HELP_LINE_LEN, "             %s (%d contracts): %s -> %s, customer %s",
                         GUILD_RANKS[x->rank].name, GUILD_RANKS[x->rank].need_done, x->name, EQUIPS[x->equip].name,
                         CUSTOMER_DEFS[x->customer].name);
                out[n++] = store[sn++];
            }
        } else if (strstr(l, "@MATS")) {
            const char *at = strstr(l, "@MATS");
            snprintf(store[sn], HELP_LINE_LEN, "%.*s", (int)(at - l), l);
            for (int m = 0; m < MAT_COUNT; m++) {
                if (m) {
                    help_append(store[sn], HELP_LINE_LEN, " / ");
                }
                help_append(store[sn], HELP_LINE_LEN, MATERIALS[m].name);
            }
            help_append(store[sn], HELP_LINE_LEN, at + 5);
            out[n++] = store[sn++];
        } else {
            out[n++] = l;
        }
    }
    return n;
}

void screen_help(App *a) {
    static char store[HELP_GEN_LINES][HELP_LINE_LEN];
    static const char *lines[T_HELP_LINE_COUNT + HELP_GEN_LINES];
    int count = help_build(lines, store);
    DrawRectangle(0, 0, VW, VH, C_BG);
    panel(10, 10, 620, 340, "HELP");
    int per = 28, maxs = count - per;
    if (maxs < 0) {
        maxs = 0;
    }
    float w = GetMouseWheelMove();
    if (w != 0) {
        a->help_scroll -= (int)w * 2;
    }
    if (key_hit(a, KEY_DOWN)) {
        a->help_scroll++;
    }
    if (key_hit(a, KEY_UP)) {
        a->help_scroll--;
    }
    if (a->help_scroll > maxs) {
        a->help_scroll = maxs;
    }
    if (a->help_scroll < 0) {
        a->help_scroll = 0;
    }
    for (int i = 0; i < per && a->help_scroll + i < count; i++) {
        const char *l = lines[a->help_scroll + i];
        txt_fit(l, 24, 30 + i * 10, 592, l[0] != ' ' && l[0] ? C_GOLD : C_TEXT);
    }
    if (maxs > 0) {
        txt("(scroll with wheel or Up/Down)", 400, 336, C_DIM);
    }
    if (btn(a, 24, 326, 100, 18, T_BACK, BF_BACK, KEY_ESCAPE)) {
        goto_screen(a, a->help_return);
    }
}
