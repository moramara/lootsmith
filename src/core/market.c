#include "simulation.h"
#include "sim_internal.h"

int market_unit_price(const GameState *g, int mat) {
    if (mat < 0 || mat >= MAT_COUNT) {
        return -1;
    }
    int disc = upgrade_effect(UP_SUPPLY, g->up_level[UP_SUPPLY]);
    return clampi(roundi(g->price[mat] * (100 - disc) / 100.0), 1, 1000000);
}

int market_quote(const GameState *g, int mat, int qty) {
    if (mat < 0 || mat >= MAT_COUNT || qty < 1 || qty > BAL.stock_cap) {
        return -1;
    }
    if (MATERIALS[mat].hunt_only) {
        return -1; /* 市場では売っていない: 見積もりも存在しない */
    }
    int disc = upgrade_effect(UP_SUPPLY, g->up_level[UP_SUPPLY]);
    long long total = ((long long)g->price[mat] * qty * (100 - disc) + 50) / 100; /* まとめ買い合計に対する割引 */
    if (total < qty) {
        total = qty;
    }
    return (int)total;
}

Result market_buy(GameState *g, int mat, int qty) {
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    if (mat < 0 || mat >= MAT_COUNT || qty < 1 || qty > BAL.stock_cap) {
        return R_ERR_RANGE;
    }
    if (MATERIALS[mat].hunt_only) {
        return R_ERR_NOT_FOR_SALE; /* 在庫・所持金・価格には一切触れない */
    }
    if (g->mat_stock[mat] + qty > BAL.stock_cap) {
        return R_ERR_FULL;
    }
    int total = market_quote(g, mat, qty);
    if (total > g->money) {
        return R_ERR_FUNDS;
    }
    g->money -= total;
    g->mat_stock[mat] += qty;
    g->mat_cost[mat] += total;
    return R_OK;
}

void market_update(GameState *g) {
    for (int m = 0; m < MAT_COUNT; m++) {
        if (MATERIALS[m].hunt_only) {
            continue; /* 市場に出ない素材: 価格は基準のまま動かず、乱数も消費しない (他の素材の価格推移を変えない) */
        }
        g->prev_price[m] = g->price[m];
        int sw = g->swing_pm[m] * BAL.market_persist_pct / 100 +
                 rng_range(&g->rng, -BAL.market_noise_pm, BAL.market_noise_pm);
        sw = clampi(sw, -BAL.market_swing_pm, BAL.market_swing_pm);
        g->swing_pm[m] = sw;
        int p = roundi(MATERIALS[m].base_price * (1000 + sw) / 1000.0);
        if (p < MATERIALS[m].min_price) {
            p = MATERIALS[m].min_price;
        }
        g->price[m] = p;
    }
}
