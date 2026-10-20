#include "simulation.h"
#include "sim_internal.h"

int upgrade_effect(int up, int level) {
    if (up < 0 || up >= UP_COUNT) {
        return 0;
    }
    return UPGRADES[up].effect[clampi(level, 0, UPGRADES[up].max_level)];
}

int customers_per_day(const GameState *g) {
    return upgrade_effect(UP_STORE, g->up_level[UP_STORE]);
}

void upgrade_effect_text(int up, int level, char *buf, size_t n) {
    if (!buf || n == 0) {
        return;
    }
    buf[0] = 0;
    if (up < 0 || up >= UP_COUNT) {
        return;
    }
    const UpgradeDef *d = &UPGRADES[up];
    int v = upgrade_effect(up, level);
    if (d->fx_style == UPFX_MULT_PM) {
        snprintf(buf, n, d->effect_fmt, v / 1000, (v % 1000) / 10);
    } else {
        snprintf(buf, n, d->effect_fmt, v);
    }
}

int upgrade_cost(const GameState *g, int up) {
    if (up < 0 || up >= UP_COUNT) {
        return -1;
    }
    int lv = g->up_level[up];
    if (lv >= UPGRADES[up].max_level) {
        return -1;
    }
    return UPGRADES[up].cost[lv];
}

Result upgrade_buy(GameState *g, int up) {
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    if (up < 0 || up >= UP_COUNT) {
        return R_ERR_RANGE;
    }
    int cost = upgrade_cost(g, up);
    if (cost < 0) {
        return R_ERR_MAXLEVEL;
    }
    if (cost > g->money) {
        return R_ERR_FUNDS;
    }
    g->money -= cost;
    g->up_level[up]++;
    return R_OK;
}
