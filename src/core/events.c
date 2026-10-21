#include "simulation.h"
#include "sim_internal.h"

/* 日替わりイベント: 日付だけから決まる純粋関数。保存も乱数も使わない (rng / guild_rng / craft_rng には一切触れない)。
 * 市場・クラフト・ギルド・UI は、日付の判定を自前で書かず、すべてここ (day_event) を通して参照する。 */
int day_event(int day) {
    if (day < 1) {
        return EVENT_NONE;
    }
    int phase = (day - 1) % EVENT_CYCLE_DAYS;
    for (int e = EVENT_NONE + 1; e < EVENT_COUNT; e++) {
        if (DAY_EVENTS[e].phase == phase) {
            return e;
        }
    }
    return EVENT_NONE;
}

const char *day_event_name(int event) {
    return event >= 0 && event < EVENT_COUNT ? DAY_EVENTS[event].name : "?";
}

int event_market_discount_pct(int day) {
    return DAY_EVENTS[day_event(day)].market_discount_pct;
}

/* ゴールド報酬への上乗せ額: round(gold * pct / 100) を整数で (0.5 は切り上げ)。gold が負や 0 なら 0 */
int event_guild_bonus(int gold, int day) {
    int pct = DAY_EVENTS[day_event(day)].guild_bonus_pct;
    if (pct <= 0 || gold <= 0) {
        return 0;
    }
    return (int)(((long long)gold * pct + 50) / 100);
}
