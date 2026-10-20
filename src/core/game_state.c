#include "simulation.h"
#include "sim_internal.h"
#include <string.h>
#include <stdlib.h>

int net_of(const DayReport *r) {
    return r->revenue + r->guild_gold - r->cogs - r->upkeep;
}

void game_init(GameState *g, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1u;
    g->day = 1;
    g->phase = PH_PREP;
    g->status = ST_PLAYING;
    g->fail_reason = FAIL_NONE;
    g->money = BAL.start_money;
    g->rep = BAL.start_rep;
    for (int m = 0; m < MAT_COUNT; m++) {
        g->price[m] = MATERIALS[m].base_price;
    }
    market_update(g); /* 1 日目の価格 */
    for (int m = 0; m < MAT_COUNT; m++) {
        g->prev_price[m] = g->price[m];
    }
    g->report.money_start = g->money;
    /* ギルド: シードから導出する独立したストリーム。ここで g->rng を消費してはならない */
    g->guild_rng = guild_seed(seed ? seed : 1u, 0x47554C44u);
    g->guild_next_id = 1;
    guild_refresh_board(g);
    /* 品質/特性の抽選: 同じシードから作る 3 本目の独立ストリーム (rng も guild_rng も消費しない) */
    g->craft_rng = guild_seed(seed ? seed : 1u, CRAFT_SALT);
}

int game_validate(const GameState *g) {
    if (g->day < 1 || g->day > MAX_DAY) {
        return 0;
    }
    if (g->phase < PH_PREP || g->phase > PH_SETTLE) {
        return 0;
    }
    if (g->status < ST_PLAYING || g->status > ST_SANDBOX) {
        return 0;
    }
    if (g->fail_reason < FAIL_NONE || g->fail_reason > FAIL_BANKRUPT) {
        return 0;
    }
    /* ST_FAILED <=> 本物の失敗理由がある (settle_day が両方を同時に設定し、他は一切設定しない) */
    if ((g->status == ST_FAILED) != (g->fail_reason != FAIL_NONE)) {
        return 0;
    }
    if (g->money < MONEY_FLOOR || g->money > DAY_CAP) {
        return 0;
    }
    if (g->rep < 0 || g->rep > BAL.rep_cap) {
        return 0;
    }
    for (int m = 0; m < MAT_COUNT; m++) {
        if (g->mat_stock[m] < 0 || g->mat_stock[m] > BAL.stock_cap) {
            return 0;
        }
        if (g->mat_cost[m] < 0 || g->mat_cost[m] > 10000000) {
            return 0;
        }
        if (g->mat_stock[m] == 0 && g->mat_cost[m] != 0) {
            return 0;
        }
        if (g->price[m] < 1 || g->price[m] > 100000) {
            return 0;
        }
        if (g->prev_price[m] < 1 || g->prev_price[m] > 100000) {
            return 0;
        }
        if (g->swing_pm[m] < -1000 || g->swing_pm[m] > 1000) {
            return 0; /* (abs(INT_MIN) は未定義動作) */
        }
    }
    if (g->inv_count < 0 || g->inv_count > MAX_INVENTORY) {
        return 0;
    }
    for (int i = 0; i < g->inv_count; i++) {
        const Equipment *e = &g->inv[i];
        if (e->type < 0 || e->type >= EQ_COUNT || !equip_unlocked(g->guild_done, e->type)) {
            return 0; /* 未解放の装備はまだ存在しえない */
        }
        if (e->main < 0 || e->main >= MAT_COUNT || e->sub < 0 || e->sub >= MAT_COUNT) {
            return 0;
        }
        if (e->atk < 0 || e->def < 0 || e->mag < 0 || e->dur < 0 || e->wt < 0) {
            return 0;
        }
        if (e->atk > 9999 || e->def > 9999 || e->mag > 9999 || e->dur > 9999 || e->wt > 9999) {
            return 0;
        }
        if (e->cost < 0 || e->cost > 1000000 || e->value < 0 || e->value > 1000000) {
            return 0;
        }
        if (e->rec_price < 1 || e->rec_price > 1000000) {
            return 0;
        }
        if (e->price < 1 || e->price > BAL.price_cap) {
            return 0;
        }
        if (e->quality < 0 || e->quality >= QUALITY_COUNT) {
            return 0;
        }
        if (e->trait < 0 || e->trait >= EQ_TRAIT_COUNT) {
            return 0;
        }
        if ((QUALITY_DEFS[e->quality].has_trait != 0) != (e->trait != EQ_TRAIT_NONE)) {
            return 0; /* Masterwork/Legendary: ちょうど 1 つ。それ以外: なし */
        }
        if (TRAIT_DEFS[e->trait].dragon && dragon_scale_uses(e->main, e->sub) == 0) {
            return 0; /* Dragonforging の特性は Dragon Scale を使った装備だけ */
        }
    }
    if (g->craft_rng == 0) {
        return 0;
    }
    for (int u = 0; u < UP_COUNT; u++) {
        if (g->up_level[u] < 0 || g->up_level[u] > UPGRADES[u].max_level) {
            return 0;
        }
    }
    if (g->total_revenue < 0 || g->total_revenue > STAT_CAP) {
        return 0;
    }
    if (g->total_sold < 0 || g->total_sold > STAT_CAP) {
        return 0;
    }
    if (g->total_profit < -STAT_CAP || g->total_profit > STAT_CAP) {
        return 0;
    }
    { /* 日次レポート: すべての額に上限があるので、net_of() と以下の加算がオーバーフローすることはない */
        const DayReport *r = &g->report;
        if (r->customers < 0 || r->customers > MAX_CUSTOMERS_PER_DAY) {
            return 0;
        }
        if (r->sold < 0 || r->sold > MAX_CUSTOMERS_PER_DAY) {
            return 0;
        }
        if (r->revenue < 0 || r->revenue > DAY_CAP || r->cogs < 0 || r->cogs > DAY_CAP) {
            return 0;
        }
        if (r->upkeep < 0 || r->upkeep > DAY_CAP) {
            return 0;
        }
        if (r->rep_delta < -DAY_CAP || r->rep_delta > DAY_CAP) {
            return 0;
        }
        if (r->money_start < MONEY_FLOOR || r->money_start > DAY_CAP) {
            return 0;
        }
    }
    if (g->queue_count < 0 || g->queue_count > MAX_CUSTOMERS_PER_DAY) {
        return 0;
    }
    if (g->queue_pos < 0 || g->queue_pos > g->queue_count) {
        return 0;
    }
    for (int i = 0; i < g->queue_count; i++) {
        const Customer *c = &g->queue[i];
        if (c->type < 0 || c->type >= CUST_TYPE_COUNT || !customer_unlocked(g->guild_done, c->type)) {
            return 0;
        }
        if (c->budget < 1 || c->budget > 100000) {
            return 0;
        }
        if (c->min_fit < 0 || c->min_fit > 100) {
            return 0;
        }
        if (c->tol_pm < 100 || c->tol_pm > 5000) {
            return 0;
        }
        if (c->patience < 0 || c->patience > 10) {
            return 0;
        }
    }
    /* フェーズの整合性 */
    if (g->phase == PH_OPEN && g->queue_pos >= g->queue_count) {
        return 0;
    }
    if (g->phase == PH_PREP && g->queue_count != 0) {
        return 0;
    }
    return guild_state_valid(g);
}
