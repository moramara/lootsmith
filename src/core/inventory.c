#include "simulation.h"
#include "sim_internal.h"
#include <string.h>

static Result inv_check(const GameState *g, int idx) {
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    if (idx < 0 || idx >= g->inv_count) {
        return R_ERR_NO_ITEM;
    }
    return R_OK;
}

Result inv_set_price(GameState *g, int idx, int price) {
    Result r = inv_check(g, idx);
    if (r != R_OK) {
        return r;
    }
    if (price < 1 || price > BAL.price_cap) {
        return R_ERR_PRICE;
    }
    g->inv[idx].price = price;
    return R_OK;
}

Result inv_adjust_price(GameState *g, int idx, int delta) {
    Result r = inv_check(g, idx);
    if (r != R_OK) {
        return r;
    }
    long long want = (long long)g->inv[idx].price + delta; /* 極端な差分でも int オーバーフローしない */
    int p = want < 1 ? 1 : want > BAL.price_cap ? BAL.price_cap : (int)want;
    g->inv[idx].price = p;
    return R_OK;
}

Result inv_reset_price(GameState *g, int idx) {
    Result r = inv_check(g, idx);
    if (r != R_OK) {
        return r;
    }
    g->inv[idx].price = g->inv[idx].rec_price;
    return R_OK;
}

int liquidation_value(const Equipment *e) {
    int v = e->cost * BAL.liquidate_pct / 100;
    return v < 1 ? 1 : v;
}

void inv_remove(GameState *g, int idx) {
    for (int i = idx; i < g->inv_count - 1; i++) {
        g->inv[i] = g->inv[i + 1];
    }
    g->inv_count--;
    memset(&g->inv[g->inv_count], 0, sizeof(Equipment));
}

Result inv_liquidate(GameState *g, int idx, int *out_value) {
    Result r = inv_check(g, idx);
    if (r != R_OK) {
        return r;
    }
    int v = liquidation_value(&g->inv[idx]);
    money_add(g, v);
    inv_remove(g, idx);
    if (out_value) {
        *out_value = v;
    }
    return R_OK;
}
