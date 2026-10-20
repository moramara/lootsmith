#include "simulation.h"
#include "sim_internal.h"
#include <string.h>
#include <math.h>

void customer_generate(uint32_t *rng, int rep, int guild_done, Customer *out) {
    /* 解放済みの客タイプの中から 1 回だけ抽選する (id 順)。出荷時の客しか解放されていない間は、設計図が
     * 導入される前と同じ範囲に対する同じ 1 回の抽選になる */
    int open[CUST_TYPE_COUNT], n = 0;
    for (int c = 0; c < CUST_TYPE_COUNT; c++) {
        if (customer_unlocked(guild_done, c)) {
            open[n++] = c;
        }
    }
    int t = open[rng_range(rng, 0, n - 1)];
    const CustomerDef *d = &CUSTOMER_DEFS[t];
    int budget = rng_range(rng, d->budget_min, d->budget_max);
    budget = budget * (BAL.budget_rep_base_pm + BAL.budget_rep_per_pm * clampi(rep, 0, 100)) / 1000;
    out->type = t;
    out->budget = budget < 1 ? 1 : budget;
    out->min_fit = rng_range(rng, d->fit_min_lo, d->fit_min_hi);
    out->tol_pm = rng_range(rng, d->tol_lo, d->tol_hi);
    out->patience = BAL.customer_patience;
}

static int stat_score(int stat, int ref) {
    return clampi(100 * stat / ref, 0, BAL.fit_stat_cap);
}

int equip_fit(int ct, const Equipment *e) {
    if (ct < 0 || ct >= CUST_TYPE_COUNT) {
        return 0;
    }
    const CustomerDef *d = &CUSTOMER_DEFS[ct];
    int light = clampi(100 * (BAL.fit_ref_weight - e->wt) / BAL.fit_ref_weight, 0, 100);
    long long sum = (long long)d->w_atk * stat_score(e->atk, BAL.fit_ref_atk) +
                    (long long)d->w_def * stat_score(e->def, BAL.fit_ref_def) +
                    (long long)d->w_mag * stat_score(e->mag, BAL.fit_ref_mag) +
                    (long long)d->w_dur * stat_score(e->dur, BAL.fit_ref_dur) + (long long)d->w_light * light;
    int wsum = d->w_atk + d->w_def + d->w_mag + d->w_dur + d->w_light;
    if (wsum <= 0) {
        return 0;
    }
    return clampi((int)((sum + wsum / 2) / wsum), 0, 100);
}

SaleJudgement judge_sale(const Customer *c, const Equipment *e, int price) {
    SaleJudgement j;
    memset(&j, 0, sizeof j);
    j.fit = equip_fit(c->type, e);
    double fair = e->value * (BAL.fair_base_pm + BAL.fair_fit_pm * j.fit / 100.0) / 1000.0;
    int worth = (int)floor(fair * c->tol_pm / 1000.0);
    j.max_price = worth < c->budget ? worth : c->budget;
    if (j.max_price < 0) {
        j.max_price = 0;
    }
    if (e->type != CUSTOMER_DEFS[c->type].wants_eq) {
        j.result = SALE_WRONG_TYPE;
    } else if (j.fit < c->min_fit) {
        j.result = SALE_LOW_FIT;
    } else if (price > c->budget) {
        j.result = SALE_OVER_BUDGET;
    } else if (price > worth) {
        j.result = SALE_TOO_EXPENSIVE;
    } else {
        j.result = SALE_OK;
    }
    return j;
}

int sale_rep_gain(int fit, int price, int max_price) {
    int gain = BAL.rep_gain_base + fit / BAL.rep_gain_fit_div;
    if (max_price > 0 && price * 100 <= max_price * BAL.rep_good_deal_pct) {
        gain += 1;
    }
    return gain;
}
