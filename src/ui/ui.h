/* LOOT SMITH - raylib フロントエンド (描画・入力・画面遷移)。ゲームのルールは core/ (公開 API は core/simulation.h)
 * にある。 */
#ifndef UI_H
#define UI_H

#include "raylib.h"
#include "core/simulation.h"
#include "bot/bot.h"
#include "audio/settings.h"
#include "audio/sfx.h"

#define VW 640 /* 仮想 (ピクセルアート) 画面のサイズ */
#define VH 360
#define MAX_PARTS 192
#define MAX_FLOATS 16

typedef enum {
    SCR_TITLE,
    SCR_MAIN,
    SCR_MARKET,
    SCR_CRAFT,
    SCR_INV,
    SCR_CUST,
    SCR_UPG,
    SCR_SETTLE,
    SCR_CLEAR,
    SCR_OVER,
    SCR_HELP,
    SCR_GUILD,
    SCR_BLUEPRINT,
    SCR_OPTIONS,
    SCR_BOT,
    SCR_COUNT
} Screen;

typedef struct {
    float x, y, vx, vy, life, max;
    Color c;
    int size;
} Particle;
typedef struct {
    float x, y, life;
    char text[24];
    Color c;
} FloatText;

typedef struct {
    GameState gs;
    int screen, next_screen, help_return;
    Vector2 mouse;
    bool click, consumed;
    char msg[112];
    bool msg_good;
    float msg_t;
    float t;
    int sel_inv, inv_scroll, help_scroll;
    int mk_row, mk_scroll, mk_qty[MAT_COUNT]; /* 市場: 選択行と先頭に表示する行 */
    int cr_eq, cr_main, cr_sub;
    int pg_main, pg_strip, pg_settle, pg_up; /* 素材グリッドとアップグレードカードのページ */
    int pg_bp;                               /* 設計図画面のページ */
    char banner_a[48], banner_b[96];
    float banner_t; /* ギルドのランクアップバナー (一時的: 保存せず、それを起こした納品で一度だけ表示する) */
    int gl_sel, gl_item,
        gl_scroll;   /* ギルド: 選択中の受注済み契約、選択中の在庫アイテム (-1 = なし)、リストのスクロール位置 */
    bool seed_fixed; /* --seed が指定された: 最初の新しいキャンペーンは `seed` をそのまま使う */
    Equipment last_craft;
    bool has_last_craft; /* 直近の CRAFT が作ったアイテム (一時的な UI の記憶。保存しない) */
    float anvil_t;
    int last_money;
    int react_pos;
    char react[64];
    bool react_good; /* 客の反応の吹き出し */
    bool has_save, quit;
    uint32_t seed;
    const char *save_path;
    /* オプション / 音声 (一時的な UI 状態。音量そのものは `settings` にある) */
    Settings settings;
    const char *settings_path;
    int options_return;    /* OPTIONS を閉じたときに戻る画面 */
    int opt_sel, opt_drag; /* 選択行 (0 BGM、1 SE、2 BACK)、ドラッグ中のスライダー (-1 = なし) */
    float opt_preview_t;   /* キーボードで調整している間の SE プレビューの間引き */
    bool settings_dirty;
    float settings_idle; /* 未保存の音量変更 / 最後の変更からの秒数 */
    SfxQueue sfx;        /* このフレームで要求された音。勝者が ui_frame の最後に再生される */
    Particle parts[MAX_PARTS];
    FloatText floats[MAX_FLOATS];
    /* BOT 観戦専用の状態。通常キャンペーンの gs とは独立し、セーブ対象にもならない。 */
    BotRunner bot;
    bool bot_paused;
    int bot_speed; /* 0 = slow, 1 = normal, 2 = fast */
    float bot_accum;
} App;

void app_init(App *a, uint32_t seed, const char *save_path);
void ui_frame(App *a, Vector2 mouse, bool click, float dt); /* 仮想画面を 1 フレーム描画する */
void app_demo(App *a, int screen); /* デバッグ: あらかじめ状態を用意したシナリオへジャンプする */
void app_start_bot(App *a); /* 通常キャンペーンと独立した BOT 観戦を開始する */
void app_set_settings(App *a, const Settings *s, const char *path); /* 起動時に読み込んだ音量 + 保存先 */
void app_shutdown(App *a);                                          /* 未保存の設定を書き出す (終了前に 1 回呼ぶ) */

#endif
