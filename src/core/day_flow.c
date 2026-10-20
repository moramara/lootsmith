#include "simulation.h"
#include "sim_internal.h"
#include <string.h>

/* ---------- 日の進行 ---------- */
static void settle_day(GameState *g) {
    DayReport *r = &g->report;
    guild_expire(g); /* 本日が期限なのに納品されなかった契約。精算ごとに 1 回 */
    r->upkeep = BAL.upkeep;
    money_add(g, -r->upkeep);
    if (r->sold == 0 && r->customers > 0 && g->rep > 0) {
        int loss = g->rep < BAL.rep_idle_loss ? g->rep : BAL.rep_idle_loss;
        g->rep -= loss;
        r->rep_delta -= loss;
    }
    sat_add(&g->total_profit, net_of(r), -STAT_CAP, STAT_CAP);
    g->queue_pos = g->queue_count;
    g->phase = PH_SETTLE;
    market_update(g); /* 翌日の価格 */

    if (g->status == ST_PLAYING) {
        if (g->money >= BAL.goal_money && g->rep >= BAL.goal_rep) {
            g->status = ST_CLEARED;
        } else if (g->money < BAL.bankrupt_line) {
            g->status = ST_FAILED;
            g->fail_reason = FAIL_BANKRUPT;
        } else if (g->day >= BAL.max_days) {
            g->status = ST_FAILED;
            g->fail_reason = FAIL_TIMEOUT;
        }
    } else if (g->status == ST_SANDBOX && g->money < 0) {
        g->money = 0; /* サンドボックスに破産はない */
    }
}

static void queue_advanced(GameState *g) {
    if (g->queue_pos >= g->queue_count) {
        settle_day(g);
    }
}

Result day_open(GameState *g) {
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    int n = customers_per_day(g);
    if (n > MAX_CUSTOMERS_PER_DAY) {
        n = MAX_CUSTOMERS_PER_DAY;
    }
    for (int i = 0; i < n; i++) {
        customer_generate(&g->rng, g->rep, g->guild_done, &g->queue[i]);
    }
    g->queue_count = n;
    g->queue_pos = 0;
    g->report.customers = n;
    g->phase = PH_OPEN;
    queue_advanced(g); /* n == 0 だと即座に精算されてしまう */
    return R_OK;
}

Result customer_offer(GameState *g, int idx, SaleOutcome *out) {
    SaleOutcome o;
    memset(&o, 0, sizeof o);
    if (g->phase != PH_OPEN) {
        return R_ERR_PHASE;
    }
    if (g->queue_pos >= g->queue_count) {
        return R_ERR_NO_CUSTOMER;
    }
    if (idx < 0 || idx >= g->inv_count) {
        return R_ERR_NO_ITEM;
    }
    Customer *c = &g->queue[g->queue_pos];
    const Equipment e = g->inv[idx];
    SaleJudgement j = judge_sale(c, &e, e.price);
    o.result = j.result;
    o.fit = j.fit;
    o.price = e.price;
    if (j.result == SALE_OK) {
        o.rep_delta = sale_rep_gain(j.fit, e.price, j.max_price);
        if (g->rep + o.rep_delta > BAL.rep_cap) {
            o.rep_delta = BAL.rep_cap - g->rep;
        }
        o.profit = e.price - e.cost;
        money_add(g, e.price);
        g->rep += o.rep_delta;
        sat_add(&g->report.sold, 1, 0, MAX_CUSTOMERS_PER_DAY);
        sat_add(&g->report.revenue, e.price, 0, DAY_CAP);
        sat_add(&g->report.cogs, e.cost, 0, DAY_CAP);
        sat_add(&g->report.rep_delta, o.rep_delta, -DAY_CAP, DAY_CAP);
        sat_add(&g->total_sold, 1, 0, STAT_CAP);
        sat_add(&g->total_revenue, e.price, 0, STAT_CAP);
        inv_remove(g, idx);
        o.sold = 1;
        o.customer_left = 1;
        g->queue_pos++;
    } else {
        c->patience--;
        if (c->patience <= 0) {
            int loss = g->rep < BAL.rep_leave_loss ? g->rep : BAL.rep_leave_loss;
            g->rep -= loss;
            g->report.rep_delta -= loss;
            o.rep_delta = -loss;
            o.customer_left = 1;
            g->queue_pos++;
        }
    }
    queue_advanced(g);
    if (out) {
        *out = o;
    }
    return R_OK;
}

Result customer_decline(GameState *g) {
    if (g->phase != PH_OPEN) {
        return R_ERR_PHASE;
    }
    if (g->queue_pos >= g->queue_count) {
        return R_ERR_NO_CUSTOMER;
    }
    g->queue_pos++;
    queue_advanced(g);
    return R_OK;
}

Result day_close_early(GameState *g) {
    if (g->phase != PH_OPEN) {
        return R_ERR_PHASE;
    }
    g->queue_pos = g->queue_count;
    settle_day(g);
    return R_OK;
}

Result day_advance(GameState *g) {
    if (g->phase != PH_SETTLE) {
        return R_ERR_PHASE;
    }
    if (g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    if (g->day >= MAX_DAY) {
        return R_ERR_LIMIT; /* MAX_DAY を超えた状態は保存できても二度とロードできない */
    }
    g->day++;
    g->phase = PH_PREP;
    g->queue_count = g->queue_pos = 0;
    memset(&g->report, 0, sizeof g->report);
    g->report.money_start = g->money;
    guild_refresh_board(g); /* 新しい朝: 受注しなかった契約は入れ替わる */
    return R_OK;
}

Result campaign_continue_sandbox(GameState *g) {
    if (g->status != ST_CLEARED || g->phase != PH_SETTLE) {
        return R_ERR_PHASE;
    }
    if (g->day >= MAX_DAY) {
        return R_ERR_LIMIT; /* 何かに触れる前に判断する: 失敗しても状態は変わらない */
    }
    g->status = ST_SANDBOX;
    return day_advance(g);
}
