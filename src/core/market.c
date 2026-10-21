#include "simulation.h"
#include "sim_internal.h"

/* 価格の計算順序 (整数のみ。表示・見積もり・購入の 3 つがすべてこの式を通る):
 *   1. 市場の現在価格 g->price[mat] (基準価格や変動履歴は変えない)
 *   2. Supplier の割引: x (100 - supplier%) / 100
 *   3. 日替わりイベント Market Caravan の追加割引: x (100 - event%) / 100  (買える素材だけ。hunt_only は対象外)
 * 2 と 3 は掛け合わせてから 1 回だけ四捨五入する (10000 を分母にした整数計算)。最終価格は 1 個あたり最低 1 ゴールド。
 */
static long long discount_factor(const GameState *g, int mat) {
    int disc = upgrade_effect(UP_SUPPLY, g->up_level[UP_SUPPLY]);
    int ev =
        MATERIALS[mat].hunt_only ? 0 : event_market_discount_pct(g->day); /* hunt_only 素材はイベント割引の対象外 */
    return (long long)(100 - disc) * (100 - ev);
}

int market_unit_price(const GameState *g, int mat) {
    if (mat < 0 || mat >= MAT_COUNT) {
        return -1;
    }
    long long p = ((long long)g->price[mat] * discount_factor(g, mat) + 5000) / 10000;
    return clampi(p > 1000000 ? 1000000 : (int)p, 1, 1000000);
}

int market_quote(const GameState *g, int mat, int qty) {
    if (mat < 0 || mat >= MAT_COUNT || qty < 1 || qty > BAL.stock_cap) {
        return -1;
    }
    if (MATERIALS[mat].hunt_only) {
        return -1; /* 市場では売っていない: 見積もりも存在しない (イベント割引の対象にもならない) */
    }
    long long total =
        ((long long)g->price[mat] * qty * discount_factor(g, mat) + 5000) / 10000; /* まとめ買い合計に対する割引 */
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

/* 市場の価格変動。`rng` を引くのは、導入済みの (MARKET_RNG_MATS 未満の) 買える素材だけで、従来と同じ順序・回数。
 * それ以降に追加された買える素材 (The Ascendant Era の Mithril / Moonstone / Emberglass) の変動幅は、`rng` を
 * *消費せず*、 その時点の状態・日付・素材 ID から作るローカルな乱数で決める
 * (どのストリームも進まない)。これにより、素材を足しても 既存のキャンペーンの客の列・価格の推移・同一シードでの結果は 1
 * つも変わらない。hunt_only の素材は、これまでどおり何もしない。 */
#define MARKET_SALT 0x4D4B5453u /* 'MKTS' */
void market_update(GameState *g) {
    for (int m = 0; m < MAT_COUNT; m++) {
        if (MATERIALS[m].hunt_only) {
            continue; /* 市場に出ない素材: 価格は基準のまま動かず、乱数も消費しない (他の素材の価格推移を変えない) */
        }
        g->prev_price[m] = g->price[m];
        int noise;
        if (m < MARKET_RNG_MATS) {
            noise = rng_range(&g->rng, -BAL.market_noise_pm, BAL.market_noise_pm);
        } else {
            uint32_t local =
                guild_seed(g->rng ^ MARKET_SALT, (uint32_t)g->day * 64u + (uint32_t)m); /* 状態は読むだけ */
            noise = rng_range(&local, -BAL.market_noise_pm, BAL.market_noise_pm);
        }
        int sw = g->swing_pm[m] * BAL.market_persist_pct / 100 + noise;
        sw = clampi(sw, -BAL.market_swing_pm, BAL.market_swing_pm);
        g->swing_pm[m] = sw;
        int p = roundi(MATERIALS[m].base_price * (1000 + sw) / 1000.0);
        if (p < MATERIALS[m].min_price) {
            p = MATERIALS[m].min_price;
        }
        g->price[m] = p;
    }
}
