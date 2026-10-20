#include "simulation.h"
#include "sim_internal.h"
#include "icons.h"
#include <string.h>

/* ---------------------------------------------------------------------------------------------
 * content_validate: すべてのコンテンツテーブルの妥当性を検査する。新しい行の誤り (名前の抜け、アイコン ID の
 * タイプミス、min_price が base_price を超える、アップグレードのレベル数が多すぎる、ホットキーの重複など) を、
 * UI の不具合や範囲外読み出しとして現れる前に `make test` で報告するためのもの。
 * 問題がなければ NULL を返し、あれば静的バッファ内のメッセージを返す。
 * --------------------------------------------------------------------------------------------- */
static char g_content_err[160];

static const char *cerr(const char *kind, int id, const char *what) {
    snprintf(g_content_err, sizeof g_content_err, "%s[%d]: %s", kind, id, what);
    return g_content_err;
}

static int name_ok(const char *s) {
    return s && s[0] && strlen(s) <= NAME_MAX_LEN;
}

static int icon_ok(const char *id) {
    return !id || !id[0] || icon_exists(id);
} /* 未設定は許可する (フォールバックスプライトを使う) */

static int fmt_specs(const char *f) { /* % 変換の数 */
    int n = 0;
    for (; f && *f; f++) {
        if (*f != '%') {
            continue;
        }
        if (f[1] == '%') {
            f++;
        } else {
            n++;
        }
    }
    return n;
}

const char *content_validate_tables(const MaterialDef *mats, int n_mat, const EquipDef *eqs, int n_eq,
                                    const CustomerDef *custs, int n_cust, const UpgradeDef *ups, int n_up) {
    for (int i = 0; i < n_mat; i++) {
        const MaterialDef *d = &mats[i];
        if (!name_ok(d->name)) {
            return cerr("MATERIALS", i, "name missing or longer than NAME_MAX_LEN");
        }
        if (!d->desc || !d->desc[0]) {
            return cerr("MATERIALS", i, "desc missing");
        }
        if (!icon_ok(d->icon)) {
            return cerr("MATERIALS", i, "unknown icon id");
        }
        if (d->min_price < 1 || d->base_price < d->min_price) {
            return cerr("MATERIALS", i, "need 1 <= min_price <= base_price");
        }
        if (d->atk < 0 || d->def < 0 || d->mag < 0 || d->dur < 0 || d->wt < 0) {
            return cerr("MATERIALS", i, "negative stat");
        }
        if (d->hunt_only != 0 && d->hunt_only != 1) {
            return cerr("MATERIALS", i, "hunt_only must be 0 or 1");
        }
        if (d->rarity_value < 0 ||
            d->rarity_value > 200) { /* 3 個分でも価値の上限 (1000000) や価格の上限に届かない範囲 */
            return cerr("MATERIALS", i, "rarity_value must be 0..200");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(mats[j].name, d->name)) {
                return cerr("MATERIALS", i, "duplicate name");
            }
        }
    }
    {
        int buyable = 0; /* 市場の画面・BOT・最初の仕入れは、買える素材が 1 つはあることを前提にする */
        for (int i = 0; i < n_mat; i++) {
            buyable |= !mats[i].hunt_only;
        }
        if (n_mat > 0 && !buyable) {
            return cerr("MATERIALS", 0, "at least one material must be sold in the market");
        }
    }
    for (int i = 0; i < n_eq; i++) {
        const EquipDef *d = &eqs[i];
        if (!name_ok(d->name)) {
            return cerr("EQUIPS", i, "name missing or longer than NAME_MAX_LEN");
        }
        if (!d->desc || !d->desc[0]) {
            return cerr("EQUIPS", i, "desc missing");
        }
        if (!icon_ok(d->icon)) {
            return cerr("EQUIPS", i, "unknown icon id");
        }
        if (d->atk < 0 || d->def < 0 || d->mag < 0 || d->dur < 0 || d->wt < 0) {
            return cerr("EQUIPS", i, "negative stat");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(eqs[j].name, d->name)) {
                return cerr("EQUIPS", i, "duplicate name");
            }
        }
    }
    for (int i = 0; i < n_cust; i++) {
        const CustomerDef *d = &custs[i];
        if (!name_ok(d->name)) {
            return cerr("CUSTOMER_DEFS", i, "name missing or longer than NAME_MAX_LEN");
        }
        if (!d->desc || !d->desc[0] || !d->priority || !d->priority[0]) {
            return cerr("CUSTOMER_DEFS", i, "desc/priority missing");
        }
        if (!icon_ok(d->icon)) {
            return cerr("CUSTOMER_DEFS", i, "unknown icon id");
        }
        if (d->wants_eq < 0 || d->wants_eq >= n_eq) {
            return cerr("CUSTOMER_DEFS", i, "wants_eq is not a valid equipment id");
        }
        if (d->w_atk < 0 || d->w_def < 0 || d->w_mag < 0 || d->w_dur < 0 || d->w_light < 0) {
            return cerr("CUSTOMER_DEFS", i, "negative weight");
        }
        if (d->w_atk + d->w_def + d->w_mag + d->w_dur + d->w_light != 100) {
            return cerr("CUSTOMER_DEFS", i, "priority weights must add up to 100");
        }
        if (d->budget_min < 1 || d->budget_max < d->budget_min || d->budget_max > 10000) {
            return cerr("CUSTOMER_DEFS", i, "bad budget range");
        }
        if (d->fit_min_lo < 0 || d->fit_min_hi < d->fit_min_lo || d->fit_min_hi > 100) {
            return cerr("CUSTOMER_DEFS", i, "bad fit range");
        }
        if (d->tol_lo < 100 || d->tol_hi < d->tol_lo || d->tol_hi > 5000) {
            return cerr("CUSTOMER_DEFS", i, "bad tolerance range");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(custs[j].name, d->name)) {
                return cerr("CUSTOMER_DEFS", i, "duplicate name");
            }
        }
    }
    for (int e = 0; e < n_eq; e++) { /* 売る相手がいなければ無意味になる */
        int wanted = 0;
        for (int c = 0; c < n_cust; c++) {
            wanted |= custs[c].wants_eq == e;
        }
        if (!wanted) {
            return cerr("EQUIPS", e, "no customer type wants this equipment");
        }
    }
    for (int i = 0; i < n_up; i++) {
        const UpgradeDef *d = &ups[i];
        if (!name_ok(d->name)) {
            return cerr("UPGRADES", i, "name missing or longer than NAME_MAX_LEN");
        }
        if (!d->desc || !d->desc[0]) {
            return cerr("UPGRADES", i, "desc missing");
        }
        if (d->max_level < 1 || d->max_level > MAX_UP_LEVEL) {
            return cerr("UPGRADES", i, "max_level must be 1..MAX_UP_LEVEL");
        }
        for (int l = 0; l < d->max_level; l++) {
            if (d->cost[l] < 1) {
                return cerr("UPGRADES", i, "cost[] must be >= 1 below max_level");
            }
        }
        if (d->fx_style != UPFX_INT && d->fx_style != UPFX_MULT_PM) {
            return cerr("UPGRADES", i, "bad fx_style");
        }
        if (!d->effect_fmt || fmt_specs(d->effect_fmt) != (d->fx_style == UPFX_MULT_PM ? 2 : 1)) {
            return cerr("UPGRADES", i, "effect_fmt does not match fx_style");
        }
        if (d->hotkey != 0 && (d->hotkey < 'A' || d->hotkey > 'Z')) {
            return cerr("UPGRADES", i, "hotkey must be 0 or 'A'..'Z'");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(ups[j].name, d->name)) {
                return cerr("UPGRADES", i, "duplicate name");
            }
            if (d->hotkey && ups[j].hotkey == d->hotkey) {
                return cerr("UPGRADES", i, "duplicate hotkey");
            }
        }
    }
    return NULL;
}

const char *content_validate_blueprints(const GuildRankDef *ranks, int n_rank, const BlueprintDef *bps, int n_bp) {
    if (n_rank < 1 || ranks[0].need_done != 0) {
        return cerr("GUILD_RANKS", 0, "the first rank must need 0 contracts");
    }
    for (int r = 0; r < n_rank; r++) {
        if (!name_ok(ranks[r].name)) {
            return cerr("GUILD_RANKS", r, "name missing or too long");
        }
        if (r > 0 && ranks[r].need_done <= ranks[r - 1].need_done) {
            return cerr("GUILD_RANKS", r, "need_done must increase");
        }
    }
    for (int b = 0; b < n_bp; b++) {
        const BlueprintDef *d = &bps[b];
        if (!name_ok(d->name)) {
            return cerr("BLUEPRINTS", b, "name missing or too long");
        }
        if (!d->desc || !d->desc[0]) {
            return cerr("BLUEPRINTS", b, "desc missing");
        }
        if (!icon_ok(d->icon)) {
            return cerr("BLUEPRINTS", b, "unknown icon id");
        }
        if (d->rank < 1 || d->rank >= n_rank) {
            return cerr("BLUEPRINTS", b, "rank must be 1..top rank");
        }
        if (d->equip < 0 || d->equip >= EQ_COUNT) {
            return cerr("BLUEPRINTS", b, "equip is not a valid equipment id");
        }
        if (d->customer < 0 || d->customer >= CUST_TYPE_COUNT) {
            return cerr("BLUEPRINTS", b, "customer is not a valid customer id");
        }
        if (CUSTOMER_DEFS[d->customer].wants_eq != d->equip) {
            return cerr("BLUEPRINTS", b, "customer does not want the unlocked equipment");
        }
        for (int j = 0; j < b; j++) {
            if (bps[j].equip == d->equip) {
                return cerr("BLUEPRINTS", b, "equipment is unlocked by two blueprints");
            }
            if (!strcmp(bps[j].name, d->name)) {
                return cerr("BLUEPRINTS", b, "duplicate name");
            }
        }
    }
    return NULL;
}

static const char *content_validate_quality(void) {
    for (int q = 0; q < QUALITY_COUNT; q++) {
        const QualityDef *d = &QUALITY_DEFS[q];
        if (!name_ok(d->name)) {
            return cerr("QUALITY_DEFS", q, "name missing or too long");
        }
        if (!d->tag || !d->tag[0] || strlen(d->tag) > 4) {
            return cerr("QUALITY_DEFS", q, "tag must be 1..4 characters");
        }
        if (d->mult_pm < 1000 || d->mult_pm > 3000) {
            return cerr("QUALITY_DEFS", q, "mult_pm must be 1000..3000");
        }
        if (q == 0 && (d->mult_pm != 1000 || d->has_trait)) {
            return cerr("QUALITY_DEFS", q, "the first quality must be plain (x1000, no trait)");
        }
        if (q > 0 && d->mult_pm <= QUALITY_DEFS[q - 1].mult_pm) {
            return cerr("QUALITY_DEFS", q, "multipliers must increase");
        }
        for (int j = 0; j < q; j++) {
            if (!strcmp(QUALITY_DEFS[j].name, d->name)) {
                return cerr("QUALITY_DEFS", q, "duplicate name");
            }
            if (!strcmp(QUALITY_DEFS[j].tag, d->tag)) {
                return cerr("QUALITY_DEFS", q, "duplicate tag");
            }
        }
    }
    {
        const char *e = content_validate_traits(TRAIT_DEFS);
        if (e) {
            return e;
        }
    }
    {
        const char *e = content_validate_dragon_forging(DRAGON_TRAIT_PCT, DRAGON_TRAIT_IDS);
        if (e) {
            return e;
        }
    }
    for (int r = 0; r < RANK_COUNT; r++) {
        int sum = 0;
        for (int q = 0; q < QUALITY_COUNT; q++) {
            if (QUALITY_RATE_PCT[r][q] < 0) {
                return cerr("QUALITY_RATE_PCT", r, "negative percentage");
            }
            sum += QUALITY_RATE_PCT[r][q];
        }
        if (sum != 100) {
            return cerr("QUALITY_RATE_PCT", r, "percentages must add up to 100");
        }
    }
    if (BAL.quality_forge_shift_pct < 0 || BAL.quality_forge_shift_pct > 100) {
        return cerr("BAL", 0, "quality_forge_shift_pct out of range");
    }
    return NULL;
}

/* Dragon Hunt の定義を検査する (任意の定義を渡せるので、不正な値が拒否されることをテストで示せる) */
const char *content_validate_dragon_hunt(const DragonHuntDef *h) {
    if (!h || !name_ok(h->name)) {
        return cerr("GUILD_DRAGON_HUNT", 0, "name missing or too long");
    }
    if (h->customer < 0 || h->customer >= CUST_TYPE_COUNT) {
        return cerr("GUILD_DRAGON_HUNT", 0, "customer is not a valid customer id");
    }
    if (h->min_fit < 0 || h->min_fit > 100) {
        return cerr("GUILD_DRAGON_HUNT", 0, "min_fit must be 0..100");
    }
    if (h->difficulty < 0 || h->difficulty >= GD_COUNT) {
        return cerr("GUILD_DRAGON_HUNT", 0, "bad difficulty");
    }
    if (h->gold < 0 || h->gold > 100000 || h->rep < 0 || h->rep > BAL.rep_cap || h->days < 1 || h->days > 99) {
        return cerr("GUILD_DRAGON_HUNT", 0, "gold / rep / days out of range");
    }
    if (h->reward_mat < 0 || h->reward_mat >= MAT_COUNT || !MATERIALS[h->reward_mat].hunt_only) {
        return cerr("GUILD_DRAGON_HUNT", 0, "reward_mat must be a hunt_only material");
    }
    if (h->reward_qty < 1 || h->reward_qty > BAL.stock_cap) {
        return cerr("GUILD_DRAGON_HUNT", 0, "reward_qty must be 1..stock_cap");
    }
    if (h->spawn_pct < 0 || h->spawn_pct > 100) {
        return cerr("GUILD_DRAGON_HUNT", 0, "spawn_pct must be 0..100");
    }
    if (h->min_done < 0 || !customer_unlocked(h->min_done, h->customer)) {
        return cerr("GUILD_DRAGON_HUNT", 0, "the client must be unlocked at min_done");
    }
    return NULL;
}

/* 特性テーブルの検査 (任意の表を渡せるので、不正な値が拒否されることをテストで示せる) */
const char *content_validate_traits(const TraitDef defs[EQ_TRAIT_COUNT]) {
    for (int t = 0; t < EQ_TRAIT_COUNT; t++) {
        const TraitDef *d = &defs[t];
        if (!name_ok(d->name) || !d->desc || !d->desc[0]) {
            return cerr("TRAIT_DEFS", t, "name / desc missing");
        }
        if (d->stat < TS_ATK || d->stat > TS_WT_DOWN) {
            return cerr("TRAIT_DEFS", t, "bad stat");
        }
        for (int q = 0; q < QUALITY_COUNT; q++) {
            int want = t != EQ_TRAIT_NONE && QUALITY_DEFS[q].has_trait;
            if ((d->amount[q] > 0) != want || d->amount[q] < 0) {
                return cerr("TRAIT_DEFS", t, "amount must be > 0 exactly for qualities with traits");
            }
        }
        for (int j = 0; j < t; j++) {
            if (!strcmp(defs[j].name, d->name)) {
                return cerr("TRAIT_DEFS", t, "duplicate name");
            }
        }
        if (d->dragon != 0 && d->dragon != 1) {
            return cerr("TRAIT_DEFS", t, "dragon must be 0 or 1");
        }
        if ((t >= EQ_TRAIT_STANDARD_COUNT) != (d->dragon != 0)) {
            return cerr("TRAIT_DEFS", t, "dragon traits are exactly the IDs from EQ_TRAIT_STANDARD_COUNT on");
        }
    }
    return NULL;
}

/* Dragonforging の確率表と候補表の検査 (任意の表を渡せるので、不正な値が拒否されることをテストで示せる) */
const char *content_validate_dragon_forging(const int pct[DRAGON_SCALE_USE_MAX + 1],
                                            const int ids[DRAGON_TRAIT_KINDS]) {
    if (!pct || !ids) {
        return cerr("DRAGON_TRAIT", 0, "table missing");
    }
    if (pct[0] != 0) {
        return cerr("DRAGON_TRAIT_PCT", 0, "no Dragon Scale means no dragon trait (must be 0)");
    }
    for (int u = 1; u <= DRAGON_SCALE_USE_MAX; u++) {
        if (pct[u] < 0 || pct[u] > 100) {
            return cerr("DRAGON_TRAIT_PCT", u, "must be 0..100");
        }
        if (pct[u] < pct[u - 1]) {
            return cerr("DRAGON_TRAIT_PCT", u, "more Dragon Scale must not lower the chance");
        }
    }
    int dragon_traits = 0;
    for (int t = 0; t < EQ_TRAIT_COUNT; t++) {
        dragon_traits += TRAIT_DEFS[t].dragon != 0;
    }
    if (dragon_traits != DRAGON_TRAIT_KINDS) {
        return cerr("DRAGON_TRAIT_IDS", 0, "every dragon trait must be a candidate (and nothing else)");
    }
    for (int i = 0; i < DRAGON_TRAIT_KINDS; i++) {
        if (ids[i] < 0 || ids[i] >= EQ_TRAIT_COUNT || !TRAIT_DEFS[ids[i]].dragon) {
            return cerr("DRAGON_TRAIT_IDS", i, "must be a dragon trait id");
        }
        for (int j = 0; j < i; j++) {
            if (ids[j] == ids[i]) {
                return cerr("DRAGON_TRAIT_IDS", i, "duplicate candidate");
            }
        }
    }
    return NULL;
}

const char *content_validate(void) {
    const char *e = content_validate_tables(MATERIALS, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT,
                                            UPGRADES, UP_COUNT);
    if (e) {
        return e;
    }
    e = content_validate_blueprints(GUILD_RANKS, RANK_COUNT, BLUEPRINTS, BP_COUNT);
    if (e) {
        return e;
    }
    e = content_validate_quality();
    if (e) {
        return e;
    }
    for (int d = 0; d < GD_COUNT; d++) {
        const GuildDifficultyDef *x = &GUILD_DIFFS[d];
        if (!name_ok(x->name)) {
            return cerr("GUILD_DIFFS", d, "name missing or too long");
        }
        if (x->min_fit < 0 || x->min_fit > 100 || x->gold < 0 || x->rep < 0 || x->rep > BAL.rep_cap || x->days < 1 ||
            x->days > 99) {
            return cerr("GUILD_DIFFS", d, "min_fit / rewards / days out of range");
        }
    }
    e = content_validate_dragon_hunt(&GUILD_DRAGON_HUNT);
    if (e) {
        return e;
    }
    for (int m = 0; m < MAT_COUNT; m++) { /* 買えない素材は、どこかの契約の報酬でなければ入手手段がない */
        if (MATERIALS[m].hunt_only && GUILD_DRAGON_HUNT.reward_mat != m) {
            return cerr("MATERIALS", m, "hunt_only material is not the reward of any contract");
        }
    }
    for (int b = 0; b < GUILD_SPAWN_BANDS; b++) {
        int sum = 0;
        for (int d = 0; d < GD_COUNT; d++) {
            if (GUILD_SPAWN[b].pct[d] < 0) {
                return cerr("GUILD_SPAWN", b, "negative percentage");
            }
            sum += GUILD_SPAWN[b].pct[d];
        }
        if (sum != 100) {
            return cerr("GUILD_SPAWN", b, "percentages must add up to 100");
        }
        if (b == 0 ? GUILD_SPAWN[b].from_day != 1 : GUILD_SPAWN[b].from_day <= GUILD_SPAWN[b - 1].from_day) {
            return cerr("GUILD_SPAWN", b, "from_day must start at 1 and increase");
        }
    }
    return NULL;
}
