/* LOOT SMITH - 効果音の ID、優先度、「1 イベントにつき 1 音」の補助関数。純粋な C で raylib は使わない
 * (単体テスト対象)。 再生自体は audio.c が行い、ゲーム/UI のコードは SeId だけを扱う。 */
#ifndef SFX_H
#define SFX_H
#include <stdbool.h>

typedef enum {
    SE_NONE = -1,
    SE_UI_CLICK,
    SE_UI_NAV,
    SE_UI_BACK, /* やわらかい木のカチッという音: 決定 / 画面切り替え / 戻る */
    SE_CRAFT,
    SE_CRAFT_RARE, /* 金床を叩く音。Masterwork / Legendary ではきらめきが加わる */
    SE_COIN,
    SE_UPGRADE, /* 支払い: 素材、アップグレード、換金収入 */
    SE_SELL,    /* 客に売れた */
    SE_ERROR,   /* 拒否された / できない */
    SE_CONTRACT,
    SE_RANK_UP,
    SE_CLEAR,
    SE_GAMEOVER, /* ファンファーレ */
    SE_SHOP_OPEN,
    SE_DAY_START, /* 店のベル、朝のチャイム */
    SE_COUNT
} SeId;

typedef struct {
    const char *name; /* トレース / ログ用の名前 */
    const char *file; /* <assets>/audio/se/ 配下のファイル */
    int priority;     /* 1 フレームで複数の音が要求された場合は、最も優先度の高いものだけを再生する */
    int min_gap_ms;   /* 同じ音をこれより短い間隔で再び開始しない (安全装置) */
} SeDef;

extern const SeDef SE_DEFS[SE_COUNT];
#define BGM_FILE "bgm_hearth.ogg" /* <assets>/audio/ 配下 */

/* ---- 1 イベントにつき 1 音 ---------------------------------------------------------------------------------------
 * 1 回の UI フレームでは多くのことが起こりうる (成功するボタンのクリック、日を終わらせる販売など)。
 * UI は候補をすべて sfx_request() で登録し、フレームの終わりに sfx_take() が唯一の勝者 (最も優先度が高いもの。
 * 同順位なら最初のもの) を返すので、1 回の入力で 2 つの音が鳴ることはない。 */
typedef struct {
    int pending;
} SfxQueue;
void sfx_queue_init(SfxQueue *q);
void sfx_request(SfxQueue *q, SeId id);
SeId sfx_take(SfxQueue *q); /* 何も要求されていなければ SE_NONE。キューを空にする */

/* ---- 再開始の防止 ---------------------------------------------------------------------------------------------- */
typedef struct {
    double last[SE_COUNT];
    bool used[SE_COUNT];
} SfxGate;
void sfx_gate_init(SfxGate *g);
bool sfx_gate_allow(SfxGate *g, SeId id,
                    double now_seconds); /* true = 再生する (そして記録する)、false = 間隔が短すぎる */

#endif
