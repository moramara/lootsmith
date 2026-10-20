/* LOOT SMITH - core 内部専用ヘッダ。
 * simulation.h (公開 API) には含めず、core ディレクトリ内の各 .c
 * ファイルの間で共有する小さな補助関数と内部関数の宣言だけを置く。 ui / audio / main / tests からは include しない。 */
#ifndef SIM_INTERNAL_H
#define SIM_INTERNAL_H

#include <math.h>
#include "simulation.h"

static inline int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static inline int roundi(double v) {
    return (int)floor(v + 0.5);
}
/* 自然な上限のない値 (サンドボックスのプレイ、編集されたセーブ) を扱うカウンタ用の飽和演算 */
static inline void sat_add(int *v, int delta, int lo, int hi) {
    long long r = (long long)*v + delta;
    *v = r < lo ? lo : r > hi ? hi : (int)r;
}
static inline void money_add(GameState *g, int delta) {
    sat_add(&g->money, delta, MONEY_FLOOR, DAY_CAP);
}
#define CRAFT_SALT 0x43524654u /* 'CRFT': クラフトのストリームをギルドのストリームから分離する */

/* rng.c: シードから独立した乱数ストリームの初期値を作る (0 にはならない) */
uint32_t guild_seed(uint32_t a, uint32_t b);
/* guild.c: 期限日を迎えた未納品の契約を取り除く (精算ごとに 1 回) / guild_state_valid は game_validate() から呼ぶ */
void guild_expire(GameState *g);
int guild_state_valid(const GameState *g);
/* inventory.c: 在庫から 1 件取り除いて詰める (販売・納品・換金が共有) */
void inv_remove(GameState *g, int idx);

#endif
