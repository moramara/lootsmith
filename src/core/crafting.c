#include "simulation.h"
#include "sim_internal.h"
#include <string.h>
#include <math.h>

void equipment_compute_stats(Equipment *e, int forge_level) {
    const EquipDef *q = &EQUIPS[e->type];
    const MaterialDef *a = &MATERIALS[e->main], *b = &MATERIALS[e->sub];
    double mult = upgrade_effect(UP_FORGE, forge_level) / 1000.0;
#define BLEND(base, f) ((base) + (2.0 * a->f + b->f) / 3.0)
    e->atk = roundi(BLEND(q->atk, atk) * mult);
    e->def = roundi(BLEND(q->def, def) * mult);
    e->mag = roundi(BLEND(q->mag, mag) * mult);
    e->dur = roundi(BLEND(q->dur, dur) * mult);
    e->wt = roundi(BLEND(q->wt, wt));
#undef BLEND
    if (e->wt < 1) {
        e->wt = 1;
    }
}

/* ---------- 品質 / 特性の層 ---------- */
/* 確率の組み立て順: ランクの基本確率 -> 日替わりイベント (Arcane Convergence) -> Forge。どの段階も合計を保つ (Common
 * から他の品質へ 移すだけで、Common の残りを超えては移さない) ので、合計は常に 100 で、どの確率も負にならない。 */
void quality_rates_on_day(int rank, int forge_level, int day, int out[QUALITY_COUNT]) {
    rank = clampi(rank, 0, RANK_COUNT - 1);
    for (int q = 0; q < QUALITY_COUNT; q++) {
        out[q] = QUALITY_RATE_PCT[rank][q];
    }
    const DayEventDef *ev = &DAY_EVENTS[day_event(day)];
    if (ev->quality_shift_common > 0) {
        int shift = ev->quality_shift_common;
        int add_m = ev->quality_add_masterwork, add_l = ev->quality_add_legendary;
        if (shift > out[QUALITY_COMMON]) {
            shift = out[QUALITY_COMMON];
        }
        /* 移す量が Masterwork / Legendary への加算の合計に満たないときは、その比率で配分する (通常は 7 = 5 + 2
         * で等しい) */
        int total_add = add_m + add_l;
        int to_m = total_add > 0 ? shift * add_m / total_add : 0;
        out[QUALITY_COMMON] -= shift;
        out[QUALITY_MASTERWORK] += to_m;
        out[QUALITY_LEGENDARY] += shift - to_m;
    }
    int shift = clampi(forge_level, 0, UPGRADES[UP_FORGE].max_level) * BAL.quality_forge_shift_pct;
    if (shift > out[QUALITY_COMMON]) {
        shift = out[QUALITY_COMMON]; /* Common の残りを超えては移さない */
    }
    out[QUALITY_COMMON] -= shift;
    out[QUALITY_FINE] += shift;
}

void quality_rates(int rank, int forge_level, int out[QUALITY_COUNT]) {
    quality_rates_on_day(rank, forge_level, 0, out); /* day 0 はイベントなし: 日付に依存しない従来の確率 */
}

int quality_from_roll_on_day(int rank, int forge_level, int day, int roll) {
    int r[QUALITY_COUNT], acc = 0, last = QUALITY_COMMON;
    quality_rates_on_day(rank, forge_level, day, r);
    roll = clampi(roll, 0, 99);
    for (int q = 0; q < QUALITY_COUNT; q++) {
        if (r[q] <= 0) {
            continue; /* 確率 0% の品質は決して当たらない */
        }
        acc += r[q];
        last = q;
        if (roll < acc) {
            return q;
        }
    }
    return last;
}

int quality_from_roll(int rank, int forge_level, int roll) {
    return quality_from_roll_on_day(rank, forge_level, 0, roll);
}

int trait_from_roll(int roll) {
    return 1 + clampi(roll, 0, EQ_TRAIT_STANDARD_COUNT - 2); /* 通常特性だけ。EQ_TRAIT_COUNT には依存しない */
}

int dragon_scale_uses(int main, int sub) {
    return (main == MAT_DRAGON_SCALE ? 2 : 0) + (sub == MAT_DRAGON_SCALE ? 1 : 0); /* 主 = 副 = Scale なら 3 */
}

int dragon_trait_chance_pct(int uses) {
    return DRAGON_TRAIT_PCT[clampi(uses, 0, DRAGON_SCALE_USE_MAX)];
}

int chance_roll_hits(int pct, int roll) {
    return clampi(roll, 0, 99) < pct;
}

int dragon_trait_hits(int uses, int roll) {
    return chance_roll_hits(dragon_trait_chance_pct(uses), roll);
}

int dragon_trait_from_roll(int roll) {
    return DRAGON_TRAIT_IDS[clampi(roll, 0, DRAGON_TRAIT_KINDS - 1)];
}

/* 特性の抽選。Dragon Scale を使わない場合は従来と完全に同じ (特性のある品質で rng_range(0, 通常特性数 - 2) を 1 回)。
 * 使う場合 (Masterwork / Legendary のみ) は、先に特殊特性の判定 (0..99) を 1 回引き、当たりなら候補から 1 回選ぶ。
 * 外れたら従来どおり通常特性を 1 回引く。品質に特性がなければ何も引かない。すべて craft_rng だけを使う。 */
int craft_roll_trait(uint32_t *craft_rng, int quality, int main, int sub) {
    if (quality < 0 || quality >= QUALITY_COUNT || !QUALITY_DEFS[quality].has_trait) {
        return EQ_TRAIT_NONE;
    }
    int uses = dragon_scale_uses(main, sub);
    if (uses > 0 && dragon_trait_hits(uses, rng_range(craft_rng, 0, 99))) {
        return dragon_trait_from_roll(rng_range(craft_rng, 0, DRAGON_TRAIT_KINDS - 1));
    }
    return trait_from_roll(rng_range(craft_rng, 0, EQ_TRAIT_STANDARD_COUNT - 2));
}

const char *quality_name(int q) {
    return q >= 0 && q < QUALITY_COUNT ? QUALITY_DEFS[q].name : "?";
}

const char *trait_name(int t) {
    return t >= 0 && t < EQ_TRAIT_COUNT ? TRAIT_DEFS[t].name : "?";
}

void trait_effect_text(int trait, int quality, char *buf, size_t n) {
    static const char *const stat_name[] = {"ATK", "DEF", "MAG", "DUR", "WT"};
    if (!buf || n == 0) {
        return;
    }
    buf[0] = 0;
    if (trait <= EQ_TRAIT_NONE || trait >= EQ_TRAIT_COUNT || quality < 0 || quality >= QUALITY_COUNT) {
        return;
    }
    const TraitDef *d = &TRAIT_DEFS[trait];
    if (d->amount[quality] <= 0) {
        return;
    }
    snprintf(buf, n, "%c%d %s", d->stat == TS_WT_DOWN ? '-' : '+', d->amount[quality], stat_name[d->stat]);
}

/* e には基本能力が入っていなければならない (直前に equipment_compute_stats が実行されている)。倍率と特性は
 * ちょうど 1 回だけ適用する: すでに品質や特性を持つアイテムは拒否するので、繰り返し呼んでも累積しない。 */
int equipment_apply_quality(Equipment *e, int quality, int trait) {
    if (quality < 0 || quality >= QUALITY_COUNT || trait < 0 || trait >= EQ_TRAIT_COUNT) {
        return 0;
    }
    if ((QUALITY_DEFS[quality].has_trait != 0) != (trait != EQ_TRAIT_NONE)) {
        return 0;
    }
    if (TRAIT_DEFS[trait].dragon && dragon_scale_uses(e->main, e->sub) == 0) {
        return 0; /* Dragonforging の特性は、Dragon Scale を使った装備にしか付かない */
    }
    if (e->quality != QUALITY_COMMON || e->trait != EQ_TRAIT_NONE) {
        return 0;
    }
    int m = QUALITY_DEFS[quality].mult_pm;
    e->atk = (e->atk * m + 500) / 1000; /* 四捨五入 (0.5 は切り上げ)。能力値は負にならない */
    e->def = (e->def * m + 500) / 1000;
    e->mag = (e->mag * m + 500) / 1000;
    e->dur = (e->dur * m + 500) / 1000;
    if (trait != EQ_TRAIT_NONE) {
        int amt = TRAIT_DEFS[trait].amount[quality];
        switch (TRAIT_DEFS[trait].stat) {
            case TS_ATK:
                e->atk += amt;
                break;
            case TS_DEF:
                e->def += amt;
                break;
            case TS_MAG:
                e->mag += amt;
                break;
            case TS_DUR:
                e->dur += amt;
                break;
            case TS_WT_DOWN:
                e->wt -= amt;
                if (e->wt < 1) {
                    e->wt = 1;
                }
                break;
        }
    }
    e->quality = quality;
    e->trait = trait;
    return 1;
}

void equipment_build(Equipment *e, int forge_level, int quality, int trait) {
    e->quality = QUALITY_COMMON;
    e->trait = EQ_TRAIT_NONE; /* 最初から作り直す: 累積しない */
    equipment_compute_stats(e, forge_level);
    equipment_apply_quality(e, quality, trait); /* 不正な組み合わせは、ただの Common アイテムになる */
}

int equipment_rarity_bonus(const Equipment *e) {
    if (e->main < 0 || e->main >= MAT_COUNT || e->sub < 0 || e->sub >= MAT_COUNT) {
        return 0;
    }
    return 2 * MATERIALS[e->main].rarity_value + MATERIALS[e->sub].rarity_value;
}

void equipment_price(Equipment *e) {
    double stat = e->atk * BAL.val_atk_pm / 1000.0 + e->def * BAL.val_def_pm / 1000.0 +
                  e->mag * BAL.val_mag_pm / 1000.0 + e->dur * BAL.val_dur_pm / 1000.0;
    int lt = BAL.val_ref_weight - e->wt;
    if (lt > 0) {
        stat += lt * BAL.val_light_pm / 1000.0;
    }
    double value = e->cost * BAL.val_cost_pm / 1000.0 + stat * BAL.val_stat_pm / 1000.0;
    /* 希少素材の優遇: 使った素材 3 個 (主 2 + 副 1) の希少度を、品質・特性・Forge を反映した後の価値にそのまま足す。
     * 原価が 0 の報酬素材 (Dragon Scale) でも、希少さが価値と推奨価格に出る。従来の素材は 0 なので何も変わらない */
    value += equipment_rarity_bonus(e);
    e->value = roundi(value);
    e->rec_price = clampi((int)ceil(value * BAL.margin_pm / 1000.0), 1, BAL.price_cap);
}

static void craft_need(int main, int sub, int need[MAT_COUNT]) {
    memset(need, 0, sizeof(int) * MAT_COUNT);
    need[main] += 2;
    need[sub] += 1;
}

int estimate_craft_cost(const GameState *g, int main, int sub) {
    if (main < 0 || main >= MAT_COUNT || sub < 0 || sub >= MAT_COUNT) {
        return 0;
    }
    int need[MAT_COUNT];
    craft_need(main, sub, need);
    double c = 0;
    for (int m = 0; m < MAT_COUNT; m++) {
        if (!need[m]) {
            continue;
        }
        double unit = g->mat_stock[m] > 0 ? (double)g->mat_cost[m] / g->mat_stock[m] : (double)market_unit_price(g, m);
        c += unit * need[m];
    }
    return roundi(c);
}

Result can_craft(const GameState *g, int eq, int main, int sub) {
    if (g->phase != PH_PREP || g->status == ST_CLEARED || g->status == ST_FAILED) {
        return R_ERR_PHASE;
    }
    if (eq < 0 || eq >= EQ_COUNT || main < 0 || main >= MAT_COUNT || sub < 0 || sub >= MAT_COUNT) {
        return R_ERR_RANGE;
    }
    if (!equip_unlocked(g->guild_done, eq)) {
        return R_ERR_LOCKED; /* 設計図がまだ獲得されていない */
    }
    if (g->inv_count >= MAX_INVENTORY) {
        return R_ERR_FULL;
    }
    int need[MAT_COUNT];
    craft_need(main, sub, need);
    for (int m = 0; m < MAT_COUNT; m++) {
        if (g->mat_stock[m] < need[m]) {
            return R_ERR_MATERIAL;
        }
    }
    return R_OK;
}

Result craft_equipment(GameState *g, int eq, int main, int sub, int *out_idx) {
    Result r = can_craft(g, eq, main, sub);
    if (r != R_OK) {
        return r;
    }
    int need[MAT_COUNT];
    craft_need(main, sub, need);
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = eq;
    e.main = main;
    e.sub = sub;
    for (int m = 0; m < MAT_COUNT; m++) {
        if (!need[m]) {
            continue;
        }
        int stock = g->mat_stock[m];
        int removed =
            (need[m] == stock) ? g->mat_cost[m] : (int)(((long long)g->mat_cost[m] * need[m] + stock / 2) / stock);
        g->mat_stock[m] -= need[m];
        g->mat_cost[m] -= removed;
        e.cost += removed;
    }
    /* ここまでのすべてが成功し、もう失敗しえない: craft_rng を消費するのはここから (rng / guild_rng
     * は決して消費しない)。 */
    int forge = g->up_level[UP_FORGE];
    int quality =
        quality_from_roll_on_day(guild_rank(g->guild_done), forge, g->day,
                                 rng_range(&g->craft_rng, 0, 99)); /* ロールは 1 回のまま。イベントは確率だけを変える */
    int trait = craft_roll_trait(&g->craft_rng, quality, main, sub);
    equipment_build(&e, forge, quality, trait); /* 基本能力 -> 品質 -> 特性。それぞれちょうど 1 回 */
    equipment_price(&e);                        /* 価値と価格は最終的な能力から求める */
    e.price = e.rec_price;
    g->inv[g->inv_count] = e;
    if (out_idx) {
        *out_idx = g->inv_count;
    }
    g->inv_count++;
    return R_OK;
}

void craft_migrate_legacy(GameState *g) {
    /* 保存された状態から導出し、どの乱数ストリームも消費しない (v1 のセーブには guild_rng がまだないので、ここでは 0)
     */
    g->craft_rng = guild_seed(g->rng ^ CRAFT_SALT, g->guild_rng + (uint32_t)g->day * 0x9E3779B1u);
}
