#include "simulation.h"
#include "sim_internal.h"
#include <string.h>

static int contract_valid(const GuildContract *c, const GameState *g, int active) {
    if (c->id < 1 || c->id >= g->guild_next_id) {
        return 0; /* 正の値で、すでに発行済みであること */
    }
    if (c->customer_type < 0 || c->customer_type >= CUST_TYPE_COUNT ||
        !customer_unlocked(g->guild_done, c->customer_type)) {
        return 0;
    }
    if (c->min_fit < 0 || c->min_fit > 100) {
        return 0;
    }
    if (c->difficulty < 0 || c->difficulty >= GD_COUNT) {
        return 0;
    }
    if (c->gold < 0 || c->gold > 100000 || c->rep < 0 || c->rep > BAL.rep_cap) {
        return 0;
    }
    if (c->days < 1 || c->days > 99) {
        return 0;
    }
    if (c->kind < 0 || c->kind >= GUILD_CONTRACT_KIND_COUNT) {
        return 0;
    }
    if (c->kind == GUILD_CONTRACT_STANDARD) {
        if (c->reward_mat != 0 || c->reward_qty != 0) {
            return 0; /* 通常契約に特別報酬はない (正規形: 素材 0・数 0) */
        }
    } else { /* GUILD_CONTRACT_DRAGON_HUNT: 報酬は hunt_only の素材で、数は 1..stock_cap。依頼主は決まっている */
        if (c->reward_qty < 1 || c->reward_qty > BAL.stock_cap) {
            return 0;
        }
        if (c->reward_mat < 0 || c->reward_mat >= MAT_COUNT || !MATERIALS[c->reward_mat].hunt_only) {
            return 0;
        }
        if (c->customer_type != GUILD_DRAGON_HUNT.customer || !guild_dragon_hunt_eligible(g->guild_done)) {
            return 0; /* Mythic Smith 到達前には存在しえない */
        }
    }
    if (!active) {
        return c->deadline_day == 0;
    }
    long long accepted = (long long)c->deadline_day - c->days +
                         1; /* 受注した日 (64 ビット: 壊れた期限でオーバーフローしないようにする) */
    if (accepted < 1 || accepted > g->day) {
        return 0;
    }
    /* 期限日の精算ですでに取り除かれているはずなので、その精算の後であれば期限は未来になければならない */
    return g->phase == PH_SETTLE ? c->deadline_day > g->day : c->deadline_day >= g->day;
}

int guild_state_valid(const GameState *g) {
    if (g->guild_rng == 0) {
        return 0;
    }
    if (g->guild_next_id < 1 || g->guild_next_id > GUILD_ID_MAX) {
        return 0;
    }
    if (g->guild_board_count < 0 || g->guild_board_count > GUILD_OFFER_COUNT) {
        return 0;
    }
    if (g->guild_active_count < 0 || g->guild_active_count > GUILD_ACTIVE_MAX) {
        return 0;
    }
    if (g->guild_done < 0 || g->guild_done > STAT_CAP || g->guild_expired < 0 || g->guild_expired > STAT_CAP) {
        return 0;
    }
    if (g->report.guild_gold < 0 || g->report.guild_gold > DAY_CAP) {
        return 0;
    }
    if (g->report.guild_done < 0 || g->report.guild_done > STAT_CAP || g->report.guild_expired < 0 ||
        g->report.guild_expired > STAT_CAP) {
        return 0;
    }
    for (int i = 0; i < g->guild_board_count; i++) {
        if (!contract_valid(&g->guild_board[i], g, 0)) {
            return 0;
        }
    }
    for (int i = 0; i < g->guild_active_count; i++) {
        if (!contract_valid(&g->guild_active[i], g, 1)) {
            return 0;
        }
    }
    {
        int hunts = 0; /* 掲示板の Dragon Hunt は最大 1 件 */
        for (int i = 0; i < g->guild_board_count; i++) {
            hunts += g->guild_board[i].kind == GUILD_CONTRACT_DRAGON_HUNT;
        }
        if (hunts > 1) {
            return 0;
        }
    }
    int n = g->guild_board_count + g->guild_active_count; /* ID は両方の配列をまたいで一意 */
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < a; b++) {
            const GuildContract *ca =
                a < g->guild_board_count ? &g->guild_board[a] : &g->guild_active[a - g->guild_board_count];
            const GuildContract *cb =
                b < g->guild_board_count ? &g->guild_board[b] : &g->guild_active[b - g->guild_board_count];
            if (ca->id == cb->id) {
                return 0;
            }
        }
    }
    return 1;
}

int guild_dragon_hunt_eligible(int done) {
    return done >= GUILD_DRAGON_HUNT.min_done && customer_unlocked(done, GUILD_DRAGON_HUNT.customer);
}

int guild_dragon_hunt_roll_hits(int roll) {
    return clampi(roll, 0, 99) < GUILD_DRAGON_HUNT.spawn_pct;
}

const char *guild_kind_name(int kind) {
    return kind == GUILD_CONTRACT_STANDARD      ? "Contract"
           : kind == GUILD_CONTRACT_DRAGON_HUNT ? GUILD_DRAGON_HUNT.name
                                                : "?";
}

static int guild_pick_difficulty(uint32_t *rng, int day) {
    const GuildSpawnBand *band = &GUILD_SPAWN[0];
    for (int i = 0; i < GUILD_SPAWN_BANDS; i++) {
        if (day >= GUILD_SPAWN[i].from_day) {
            band = &GUILD_SPAWN[i];
        }
    }
    int roll = rng_range(rng, 0, 99), acc = 0;
    for (int d = 0; d < GD_COUNT; d++) {
        acc += band->pct[d];
        if (roll < acc) {
            return d;
        }
    }
    return GD_COUNT - 1;
}

void guild_refresh_board(GameState *g) {
    if (g->guild_next_id > GUILD_ID_MAX - GUILD_OFFER_COUNT) { /* 新しい ID が尽きた: 掲示板は直下で入れ替えるので、 */
        for (int i = 0; i < g->guild_active_count; i++) {
            g->guild_active[i].id = i + 1; /* 保持している契約にだけ ID が必要。1..n に振り直す */
        }
        g->guild_next_id = g->guild_active_count + 1;
    }
    memset(g->guild_board, 0, sizeof g->guild_board);
    g->guild_board_count = 0;
    int used[CUST_TYPE_COUNT] = {0};
    /* Dragon Hunt の抽選: 対象 (Mythic Smith 以上) のときだけ guild_rng を 1 回引く。対象外では何も引かないので、
     * 通常の掲示板の乱数列は従来のまま。出現すると通常契約が 1 件減り、掲示板の総数は常に GUILD_OFFER_COUNT になる */
    int hunt =
        guild_dragon_hunt_eligible(g->guild_done) && guild_dragon_hunt_roll_hits(rng_range(&g->guild_rng, 0, 99));
    if (hunt) {
        used[GUILD_DRAGON_HUNT.customer] = 1; /* 通常契約の依頼主が Dragon Hunt の依頼主と重ならない */
    }
    for (int i = 0; i < GUILD_OFFER_COUNT - hunt; i++) {
        int diff = guild_pick_difficulty(&g->guild_rng, g->day); /* 先に難易度を決め、その後で数値を決める */
        int avail[CUST_TYPE_COUNT], any[CUST_TYPE_COUNT], n = 0, n_any = 0;
        for (int t = 0; t < CUST_TYPE_COUNT; t++) {
            if (!customer_unlocked(g->guild_done, t)) {
                continue; /* 未解放の装備は決して求めない */
            }
            any[n_any++] = t;
            if (!used[t]) {
                avail[n++] = t; /* 可能なら別々の客にする */
            }
        }
        int ct = n > 0 ? avail[rng_range(&g->guild_rng, 0, n - 1)] : any[rng_range(&g->guild_rng, 0, n_any - 1)];
        used[ct] = 1;
        const GuildDifficultyDef *d = &GUILD_DIFFS[diff];
        GuildContract *c = &g->guild_board[g->guild_board_count++];
        c->id = g->guild_next_id++;
        c->customer_type = ct;
        c->min_fit = d->min_fit;
        c->difficulty = diff;
        c->gold = d->gold;
        c->rep = d->rep;
        c->days = d->days;
        c->deadline_day = 0;
        c->kind = GUILD_CONTRACT_STANDARD; /* 特別報酬なし */
        c->reward_mat = 0;
        c->reward_qty = 0;
    }
    if (hunt) {
        const DragonHuntDef *h = &GUILD_DRAGON_HUNT;
        GuildContract *c = &g->guild_board[g->guild_board_count++];
        c->id = g->guild_next_id++;
        c->customer_type = h->customer;
        c->min_fit = h->min_fit;
        c->difficulty = h->difficulty;
        c->gold = h->gold;
        c->rep = h->rep;
        c->days = h->days;
        c->deadline_day = 0;
        c->kind = GUILD_CONTRACT_DRAGON_HUNT;
        c->reward_mat = h->reward_mat;
        c->reward_qty = h->reward_qty;
    }
}

void guild_migrate_legacy(GameState *g) {
    g->guild_rng = guild_seed(g->rng, (uint32_t)g->day); /* 保存された状態から導出する (状態は消費しない) */
    g->guild_next_id = 1;
    g->guild_active_count = 0;
    memset(g->guild_active, 0, sizeof g->guild_active);
    g->guild_done = g->guild_expired = 0;
    g->report.guild_gold = g->report.guild_done = g->report.guild_expired = 0;
    guild_refresh_board(g);
}

static int guild_phase_ok(const GameState *g) {
    return g->phase == PH_PREP && (g->status == ST_PLAYING || g->status == ST_SANDBOX);
}

GuildResult guild_accept(GameState *g, int idx) {
    if (!guild_phase_ok(g)) {
        return GUILD_ERR_PHASE;
    }
    if (idx < 0 || idx >= GUILD_OFFER_COUNT) {
        return GUILD_ERR_RANGE;
    }
    if (g->guild_active_count >= GUILD_ACTIVE_MAX) {
        return GUILD_ERR_FULL;
    }
    if (idx >= g->guild_board_count) {
        return GUILD_ERR_NO_CONTRACT;
    }
    GuildContract c = g->guild_board[idx];
    c.deadline_day = g->day + c.days - 1; /* 受注した日も数える */
    g->guild_active[g->guild_active_count++] = c;
    for (int i = idx; i < g->guild_board_count - 1; i++) {
        g->guild_board[i] = g->guild_board[i + 1];
    }
    memset(&g->guild_board[--g->guild_board_count], 0, sizeof g->guild_board[0]);
    return GUILD_OK;
}

int guild_item_fit(const GameState *g, int active_idx, int inv_idx) {
    if (active_idx < 0 || active_idx >= g->guild_active_count) {
        return -1;
    }
    if (inv_idx < 0 || inv_idx >= g->inv_count) {
        return -1;
    }
    const GuildContract *c = &g->guild_active[active_idx];
    const Equipment *e = &g->inv[inv_idx];
    if (e->type != CUSTOMER_DEFS[c->customer_type].wants_eq) {
        return -1;
    }
    return equip_fit(c->customer_type, e);
}

GuildResult guild_deliver(GameState *g, int active_idx, int inv_idx, GuildOutcome *out) {
    GuildOutcome o;
    memset(&o, 0, sizeof o);
    GuildResult r = GUILD_OK;
    if (!guild_phase_ok(g)) {
        r = GUILD_ERR_PHASE;
    } else if (active_idx < 0 || active_idx >= GUILD_ACTIVE_MAX) {
        r = GUILD_ERR_RANGE;
    } else if (active_idx >= g->guild_active_count) {
        r = GUILD_ERR_NO_CONTRACT;
    } else if (inv_idx < 0 || inv_idx >= g->inv_count) {
        r = GUILD_ERR_NO_ITEM;
    }
    if (r != GUILD_OK) {
        o.result = r;
        if (out) {
            *out = o;
        }
        return r;
    }

    const GuildContract c = g->guild_active[active_idx];
    const Equipment e = g->inv[inv_idx];
    o.contract_id = c.id;
    o.item_type = e.type;
    o.fit = equip_fit(c.customer_type, &e); /* 客が使うのと同じ FIT */
    if (e.type != CUSTOMER_DEFS[c.customer_type].wants_eq) {
        r = GUILD_ERR_WRONG_TYPE;
    } else if (o.fit < c.min_fit) {
        r = GUILD_ERR_LOW_FIT;
    } else if (c.kind == GUILD_CONTRACT_DRAGON_HUNT) {
        /* 報酬を受け取れるかは、何かを変える前に判断する (受け取れないなら装備・金・評判・契約は一切そのまま) */
        if (c.reward_qty < 1 || c.reward_mat < 0 || c.reward_mat >= MAT_COUNT || !MATERIALS[c.reward_mat].hunt_only) {
            r = GUILD_ERR_RANGE;
        } else if ((long long)g->mat_stock[c.reward_mat] + c.reward_qty > BAL.stock_cap) {
            r = GUILD_ERR_REWARD_FULL;
        }
    }
    if (r != GUILD_OK) {
        o.result = r;
        if (out) {
            *out = o;
        }
        return r;
    }

    o.gold = c.gold;
    o.rep_gain = g->rep + c.rep > BAL.rep_cap ? BAL.rep_cap - g->rep : c.rep;
    o.profit = c.gold - e.cost;
    money_add(g, c.gold);
    g->rep += o.rep_gain;
    sat_add(&g->report.guild_gold, c.gold, 0, DAY_CAP);
    sat_add(&g->report.guild_done, 1, 0, STAT_CAP);
    sat_add(&g->report.cogs, e.cost, 0, DAY_CAP); /* アイテムは販売時と同様に消費される */
    sat_add(&g->report.rep_delta, o.rep_gain, -DAY_CAP, DAY_CAP);
    sat_add(&g->guild_done, 1, 0, STAT_CAP);
    if (c.kind == GUILD_CONTRACT_DRAGON_HUNT) {
        g->mat_stock[c.reward_mat] += c.reward_qty; /* 上限は上で検査済み。無料の報酬: mat_cost は増やさない (原価 0) */
        o.reward_mat = c.reward_mat;
        o.reward_qty = c.reward_qty;
    }
    inv_remove(g, inv_idx);
    for (int i = active_idx; i < g->guild_active_count - 1; i++) {
        g->guild_active[i] = g->guild_active[i + 1];
    }
    memset(&g->guild_active[--g->guild_active_count], 0, sizeof g->guild_active[0]);
    o.result = GUILD_OK;
    if (out) {
        *out = o;
    }
    return GUILD_OK;
}

void guild_expire(GameState *g) {
    int w = 0;
    for (int i = 0; i < g->guild_active_count; i++) {
        if (g->guild_active[i].deadline_day <= g->day) {
            sat_add(&g->guild_expired, 1, 0, STAT_CAP);
            sat_add(&g->report.guild_expired, 1, 0, STAT_CAP);
        } else {
            g->guild_active[w++] = g->guild_active[i];
        }
    }
    for (int i = w; i < g->guild_active_count; i++) {
        memset(&g->guild_active[i], 0, sizeof g->guild_active[0]);
    }
    g->guild_active_count = w;
}
