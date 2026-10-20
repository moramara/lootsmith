/* LOOT SMITH - ゲームロジック (raylib に依存しない)。
 *
 * ルールはすべてここに集約する: 市場、クラフト、価格設定、客、日次精算、アップグレード、キャンペーン状態。
 * バランス用の数値は `BAL` と下記の *_DEFS テーブルにまとめてあるため、調整時にロジックへ触れる必要はない。
 *
 * 規約:
 *   - アクション関数は Result を返す (成功時は R_OK)。失敗したときは GameState を一切変更しない。
 *   - 乱数は GameState 内の xorshift 状態を使うので、同じシードと同じ操作列なら完全に再現できる。
 *     独立した乱数ストリームは 3 本ある: `rng` (市場・客)、`guild_rng` (契約)、`craft_rng` (装備の品質と特性)。
 *     どのストリームも他のストリームの乱数を消費しない。
 */
#ifndef SIMULATION_H
#define SIMULATION_H

#include <stdint.h>
#include <stdio.h>

/* ---------- 容量 ---------- */
#define MAX_UP_LEVEL 3 /* アップグレードの max_level の最大値 */
#define MAX_INVENTORY 24
#define MAX_CUSTOMERS_PER_DAY 5
#define NAME_MAX_LEN 18     /* UI レイアウトが想定している最長のコンテンツ名 */
#define GUILD_OFFER_COUNT 3 /* 毎朝、掲示板に出る契約の数 */
#define GUILD_ACTIVE_MAX 2  /* 同時に受注できる契約の数 */

/* ---------- コンテンツ ID ----------
 * ID は対応するテーブル内のエントリの添字であり、同時にセーブファイルへ書き込まれる数値でもある。
 *   - 新しいコンテンツは、*_COUNT の直前に ID を追加し、テーブルに "[ID] = {...}" の行を足すこと。
 *   - ID の並べ替え・削除・再利用は絶対に行わない (古いセーブの内容が黙って変わってしまう)。
 *   - *_COUNT は最後の列挙子なので、件数が ID とずれることはない。 */
enum {
    MAT_IRON,
    MAT_WOOD,
    MAT_MANA,
    MAT_COPPER,
    MAT_STEEL,
    MAT_SILVER,
    MAT_OBSIDIAN,
    MAT_DRAGON_SCALE, /* 市場では買えない (hunt_only)。Dragon Hunt の報酬でのみ手に入る */
    MAT_COUNT
};
enum {
    EQ_SWORD,
    EQ_SHIELD,
    EQ_STAFF,
    EQ_DAGGER,
    EQ_SPEAR,
    EQ_GRIMOIRE,
    EQ_WARHAMMER,
    EQ_RUNE_BOW,
    EQ_CRYSTAL_ORB,
    EQ_WYRMCLEAVER,
    EQ_COUNT
};
enum {
    CT_WARRIOR,
    CT_GUARDIAN,
    CT_MAGE,
    CT_THIEF,
    CT_SCOUT,
    CT_LANCER,
    CT_SCHOLAR,
    CT_KNIGHT,
    CT_RANGER,
    CT_ARCANIST,
    CT_WYRM_HUNTER,
    CUST_TYPE_COUNT
};
enum { UP_FORGE, UP_STORE, UP_SUPPLY, UP_COUNT };
/* 装備の品質と特性 (アイテムごとに Equipment に保存される)。0 は必ず「Common」/「なし」にする。
 * こうしておけば、ゼロ初期化された装備や v3 より前の装備が構造上そのまま有効になる。他の ID と同じく追記のみ可。 */
enum { QUALITY_COMMON, QUALITY_FINE, QUALITY_MASTERWORK, QUALITY_LEGENDARY, QUALITY_COUNT };
enum {
    EQ_TRAIT_NONE,
    EQ_TRAIT_KEEN,
    EQ_TRAIT_WARDING,
    EQ_TRAIT_ARCANE,
    EQ_TRAIT_STURDY,
    EQ_TRAIT_LIGHTWEIGHT,
    /* ここから下は Dragonforging の特殊特性 (Dragon Scale を使った Masterwork / Legendary だけに付く)。ID は追記のみ */
    EQ_TRAIT_DRACONIC_EDGE,
    EQ_TRAIT_WYRMSCALE,
    EQ_TRAIT_COUNT,
    /* 通常の抽選 (trait_from_roll) の対象は EQ_TRAIT_NONE + 通常特性 5 つ = ここまで。EQ_TRAIT_COUNT が増えても、
     * 通常クラフトの抽選範囲・乱数消費・分布はこの定数で固定される */
    EQ_TRAIT_STANDARD_COUNT = EQ_TRAIT_LIGHTWEIGHT + 1
};
enum { PH_PREP, PH_OPEN, PH_SETTLE };
enum { ST_PLAYING, ST_CLEARED, ST_FAILED, ST_SANDBOX };
enum { FAIL_NONE, FAIL_TIMEOUT, FAIL_BANKRUPT };

typedef enum {
    R_OK = 0,
    R_ERR_PHASE,    /* 現在のフェーズ/状態では許可されない操作 */
    R_ERR_RANGE,    /* 添字・数量・ID が不正 */
    R_ERR_FUNDS,    /* 所持金が足りない */
    R_ERR_MATERIAL, /* 素材が足りない */
    R_ERR_FULL,     /* 在庫または保管の上限に達している */
    R_ERR_MAXLEVEL, /* アップグレードがすでに最大レベル */
    R_ERR_NO_ITEM,  /* 在庫スロットが空 */
    R_ERR_NO_CUSTOMER,
    R_ERR_PRICE,       /* 価格が 1..price_cap の範囲外 */
    R_ERR_LOCKED,      /* 装備タイプがまだ解放されていない (ギルドの設計図) */
    R_ERR_LIMIT,       /* 暦が終わる: MAX_DAY は (サンドボックスの) ゲームが到達できる最終日 */
    R_ERR_NOT_FOR_SALE /* 市場では売っていない素材 (MaterialDef.hunt_only) を買おうとした */
} Result;

typedef enum {
    SALE_OK = 0,
    SALE_WRONG_TYPE,   /* 客が別の種類の装備を求めている */
    SALE_LOW_FIT,      /* 性能が客の要求に届かない */
    SALE_OVER_BUDGET,  /* 価格が客の予算を超えている */
    SALE_TOO_EXPENSIVE /* 価格が客の考える価値を超えている */
} SaleResult;

/* ---------- バランス (調整可能な値すべて。permille = 1/1000) ---------- */
typedef struct {
    int start_money, start_rep, upkeep, max_days;
    int goal_money, goal_rep, bankrupt_line;
    int market_swing_pm;    /* 基準価格からの最大乖離 */
    int market_noise_pm;    /* 1 日あたりのランダム変動幅 */
    int market_persist_pct; /* 前日の変動をどれだけ引き継ぐか */
    int customer_patience;  /* 客が許容する提示回数 */
    int liquidate_pct;      /* 緊急売却額 (原価に対する %) */
    int price_cap, stock_cap, rep_cap;
    int rep_gain_base, rep_gain_fit_div, rep_good_deal_pct;
    int rep_idle_loss, rep_leave_loss;
    int budget_rep_base_pm, budget_rep_per_pm;
    /* 装備の価値: cost*val_cost + stat_score*val_stat、推奨価格 rec = value*margin */
    int val_cost_pm, val_stat_pm;
    int val_atk_pm, val_def_pm, val_mag_pm, val_dur_pm, val_light_pm;
    int val_ref_weight;
    int margin_pm;
    /* 客にとっての適正価格 = value * (fair_base + fair_fit*fit/100) */
    int fair_base_pm, fair_fit_pm;
    /* 適合度の正規化に使う基準値 */
    int fit_ref_atk, fit_ref_def, fit_ref_mag, fit_ref_dur, fit_ref_weight;
    int fit_stat_cap;
    int quality_forge_shift_pct; /* 鍛冶場 (Forge) の 1 レベルごとに Common から Fine へ移す割合 (パーセントポイント) */
} Balance;

/* コンテンツ定義。1 つのコンテンツに固有のもの (テキスト、数値、アイコン、色、ホットキー) はすべて、
 * core/content_tables.c にあるテーブルの 1 行にまとまっている。ロジックと UI はそれを読むだけ。
 * `icon` は icons.c のスプライト ID。NULL / "" / 未知の ID の場合は「?」のフォールバックスプライトを描く。 */
typedef struct {
    unsigned char r, g, b, a;
} IconColor;
#define ICOL(r, g, b) {(r), (g), (b), 255}
#define ICOL_NONE {0, 0, 0, 0} /* 透明: そのパレットスロットは描画しない */

typedef struct {
    const char *name, *desc, *icon;
    IconColor color, color_dark; /* スプライトのパレット。この素材で作る装備の色付けにも使う */
    int base_price, min_price;
    int atk, def, mag, dur, wt;
    int hunt_only; /* 1 = 市場では買えない (ギルドの報酬専用)。価格は変動せず、市場更新で乱数も消費しない。0 が既定 */
    /* 希少度ボーナス: この素材を 1 個使うごとに装備の価値 (value) へ直接加わる点数 (主 2 個 + 副 1 個 = 3 個分)。
     * 能力スコアにも原価にも掛からない。0 が既定 (従来の素材はすべて 0) */
    int rarity_value;
} MaterialDef;
typedef struct {
    const char *name, *desc, *icon;
    int atk, def, mag, dur, wt;
} EquipDef;
typedef struct {
    const char *name, *desc, *priority, *icon;
    IconColor hat, armor, top; /* スプライトのパレット */
    int wants_eq;
    int w_atk, w_def, w_mag, w_dur, w_light; /* 優先度の重み */
    int budget_min, budget_max;
    int fit_min_lo, fit_min_hi; /* 要求される適合度の範囲 */
    int tol_lo, tol_hi;         /* 価格の許容幅 (permille) */
} CustomerDef;
typedef enum {
    UPFX_INT,    /* effect_fmt は %d を 1 つ持ち、効果値を受け取る */
    UPFX_MULT_PM /* 効果値は permille 単位の倍率。effect_fmt は "%d.%02d" を持つ */
} UpgradeFxStyle;
typedef struct {
    const char *name, *desc;
    int hotkey; /* UPGRADE 画面での 'A'..'Z'。0 = なし (クリックのみ) */
    UpgradeFxStyle fx_style;
    const char *effect_fmt; /* effect[] をプレイヤーにどう見せるか */
    int max_level;
    int cost[MAX_UP_LEVEL];       /* cost[l]: レベル l から l+1 へ上げる費用 */
    int effect[MAX_UP_LEVEL + 1]; /* effect[l]: レベル l のときの値 */
} UpgradeDef;

/* 品質: 鍛冶場の倍率の後で ATK/DEF/MAG/DUR に掛ける能力倍率 (permille)。特性は has_trait の品質にだけ存在する。
 * rate_pct[rank][quality] (下の QUALITY_RATE_PCT) が基本の生成確率テーブル。 */
typedef struct {
    const char *name, *tag; /* tag: 一覧表示用の 4 文字のラベル */
    int mult_pm;
    int has_trait;   /* 1 = 特性をちょうど 1 つ抽選する、0 = 特性は付かない */
    IconColor color; /* UI のアクセント色 (Common は通常のテキスト色) */
} QualityDef;
typedef enum { TS_ATK, TS_DEF, TS_MAG, TS_DUR, TS_WT_DOWN } TraitStat; /* 特性が何を変えるか */
typedef struct {
    const char *name, *desc;
    int stat;                  /* TraitStat */
    int amount[QUALITY_COUNT]; /* 品質ごとの変化量 (その品質に特性がない場合は 0) */
    int dragon;                /* 1 = Dragonforging の特性: Dragon Scale を使った装備にだけ存在できる。0 が既定 */
} TraitDef;

/* Dragonforging: Dragon Scale を使った Masterwork / Legendary
 * のクラフトは、通常特性の代わりに特殊特性が付くことがある。 使用数 (主素材 2 個 + 副素材 1 個。両方なら 3 個)
 * ごとの確率 (パーセント) と、選ばれる特性は定義テーブルにある。 */
#define DRAGON_SCALE_USE_MAX 3
#define DRAGON_TRAIT_KINDS 2
extern const int DRAGON_TRAIT_PCT[DRAGON_SCALE_USE_MAX + 1]; /* 添字 = Dragon Scale の使用数。[0] は常に 0 */
extern const int DRAGON_TRAIT_IDS[DRAGON_TRAIT_KINDS];       /* 特殊特性が選ばれたときの候補 (均等) */

/* 到達可能なあらゆる状態を保存でき、かつ読み込めるようにするための上限 (game_validate() も同じ数値で検証する)。
 * カウンタはオーバーフローせず飽和し、暦は MAX_DAY で止まる。 */
#define MAX_DAY 9999
#define STAT_CAP 1000000000     /* キャンペーン累計: revenue、sold、profit (±)、guild_done / guild_expired */
#define DAY_CAP 100000000       /* 所持金と 1 日分の各レポート額 */
#define GUILD_ID_MAX 1000000000 /* 契約 ID はこの値未満で発行する。guild_refresh_board() はこれを超えずに振り直す */
#define MONEY_FLOOR (-1000000)  /* 所持金はこの値を下回らない (サンドボックスに破産はない) */

extern const Balance BAL;
extern const MaterialDef MATERIALS[MAT_COUNT];
extern const EquipDef EQUIPS[EQ_COUNT];
extern const CustomerDef CUSTOMER_DEFS[CUST_TYPE_COUNT];
extern const UpgradeDef UPGRADES[UP_COUNT];
extern const QualityDef QUALITY_DEFS[QUALITY_COUNT];
extern const TraitDef TRAIT_DEFS[EQ_TRAIT_COUNT]; /* EQ_TRAIT_NONE の行はプレースホルダ: 効果なし */

/* ---------- ギルド契約 ----------
 * 毎朝ギルドが GUILD_OFFER_COUNT 件の契約を掲示する (GameState.guild_board)。PH_PREP の間、プレイヤーはそのうち
 * 最大 GUILD_ACTIVE_MAX 件を受注でき (guild_active へ移る)、求められた装備をクラフトして納品すると
 * ゴールドと評判を得る。受注しなかった契約は翌朝に消える。受注済みの契約は、納品されるか、期限日の精算までは残る。
 * 契約が「掲示板にある」か「受注中」かは、どちらの配列に入っているかだけで決まる。 */
typedef enum { GD_EASY, GD_STANDARD, GD_HARD, GD_COUNT } GuildDifficulty;
typedef struct {
    const char *name;
    int min_fit, gold, rep, days;
} GuildDifficultyDef;
typedef struct {
    int from_day;
    int pct[GD_COUNT];
} GuildSpawnBand; /* `from_day` 以降の難易度の出現確率 (パーセント) */
#define GUILD_SPAWN_BANDS 3
extern const GuildDifficultyDef GUILD_DIFFS[GD_COUNT];
extern const GuildSpawnBand GUILD_SPAWN[GUILD_SPAWN_BANDS];
typedef struct {
    int id;            /* 0 より大きい。1 回のキャンペーン内では再利用しない */
    int customer_type; /* 依頼主。求められる装備は CUSTOMER_DEFS[..].wants_eq */
    int min_fit;       /* 要求される equip_fit(customer_type, item)、0..100 */
    int difficulty;    /* GuildDifficulty */
    int gold, rep;     /* 報酬。納品時にのみ支払われる */
    int days;          /* 日数での期間 (受注した日も数える) */
    int deadline_day;  /* 納品できる最終日: accept_day + days - 1。掲示板にある間は 0 */
    /* 契約の種類と特別報酬 (セーブ v4)。ゼロ初期化された契約は「通常契約・特別報酬なし」になる。
     * GUILD_CONTRACT_STANDARD: reward_mat == 0 かつ reward_qty == 0 (報酬素材なしの正規形)。
     * GUILD_CONTRACT_DRAGON_HUNT: reward_mat は hunt_only の素材、reward_qty は 1..BAL.stock_cap。
     * 報酬素材は納品が成功したときにだけ付与される (guild_deliver)。 */
    int kind;       /* GUILD_CONTRACT_* */
    int reward_mat; /* 報酬素材の ID (reward_qty == 0 のときは 0) */
    int reward_qty; /* 報酬素材の数。0 = なし */
} GuildContract;
enum { GUILD_CONTRACT_STANDARD, GUILD_CONTRACT_DRAGON_HUNT, GUILD_CONTRACT_KIND_COUNT }; /* 追記のみ */

/* Dragon Hunt: Mythic Smith 到達後に低確率で掲示される特別契約。数値はすべてこの 1 つのテーブルにある
 * (通常契約の GUILD_DIFFS / GUILD_SPAWN とは独立。通常契約の報酬・難易度・出現率には影響しない)。 */
typedef struct {
    const char *name;
    int customer; /* 依頼主 (その wants_eq が必要装備) */
    int min_fit;  /* 必須の最低 FIT。納品判定は通常の equip_fit() を使う */
    int difficulty;
    int gold, rep, days;
    int reward_mat, reward_qty; /* 納品成功時の特別報酬 */
    int min_done;               /* 抽選の対象になる guild_done の下限 (Mythic Smith) */
    int spawn_pct;              /* 掲示板の更新ごとの出現確率 (%)。抽選は roll(0..99) < spawn_pct */
} DragonHuntDef;
extern const DragonHuntDef GUILD_DRAGON_HUNT;
typedef enum {
    GUILD_OK = 0,
    GUILD_ERR_PHASE,       /* PH_PREP ではない、またはキャンペーンが終了している */
    GUILD_ERR_RANGE,       /* 契約 / アイテムの添字が配列の範囲外 */
    GUILD_ERR_NO_CONTRACT, /* その契約スロットが空 */
    GUILD_ERR_FULL,        /* すでに GUILD_ACTIVE_MAX 件を受注している */
    GUILD_ERR_NO_ITEM,     /* そのような在庫アイテムがない */
    GUILD_ERR_WRONG_TYPE,  /* アイテムが求められた種類の装備ではない */
    GUILD_ERR_LOW_FIT,     /* アイテムの FIT が要求に届かない */
    GUILD_ERR_REWARD_FULL  /* 報酬素材の在庫が BAL.stock_cap に達していて受け取れない (何も変更しない) */
} GuildResult;
typedef struct {
    int result, contract_id;
    int item_type, fit; /* 納品した/却下されたアイテムのもの (アイテムを確認していない場合は 0) */
    int gold, rep_gain; /* 実際に支払われた額 (評判は BAL.rep_cap で上限が掛かる) */
    int profit;         /* ゴールドから、消費したアイテムの原価を引いた額 (報酬素材は原価ゼロの無料報酬: 含めない) */
    int reward_mat, reward_qty; /* 実際に付与された報酬素材 (なければ 0 / 0) */
} GuildOutcome;

/* ---------- ギルドランクと設計図 ----------
 * ランクは保存しない。GameState.guild_done (納品済み契約数) だけから決まる純粋関数なので、guild_done と食い違うことは
 * ありえず、セーブファイルの容量も使わない。設計図は、ランクに達すると 1 つの装備タイプ (とそれを求める客) を解放する。
 * guild_done が減ることはないのでランクも下がらない。どの設計図にも載っていない装備は常に使える。 */
enum { RANK_APPRENTICE, RANK_JOURNEYMAN, RANK_MASTER, RANK_GRANDMASTER, RANK_MYTHIC, RANK_COUNT };
typedef struct {
    const char *name;
    int need_done;
} GuildRankDef; /* need_done: そのランクに必要な納品済み契約数 */
enum { BP_WARHAMMER, BP_RUNE_BOW, BP_CRYSTAL_ORB, BP_WYRMCLEAVER, BP_COUNT }; /* 他の ID と同じく追記のみ可 */
typedef struct {
    const char *name, *desc, *icon;
    int rank;     /* それを解放するランク (1..RANK_COUNT-1) */
    int equip;    /* 解放される装備タイプ */
    int customer; /* その装備を求める客タイプ (解放されると出現する) */
} BlueprintDef;
extern const GuildRankDef GUILD_RANKS[RANK_COUNT];
extern const int QUALITY_RATE_PCT[RANK_COUNT][QUALITY_COUNT]; /* ギルドランクごとの基本品質確率。各行の合計は 100 */
extern const BlueprintDef BLUEPRINTS[BP_COUNT];

/* ---------- 状態 ---------- */
typedef struct {
    int type, main, sub;
    int atk, def, mag, dur, wt;
    int cost;      /* 素材にかかった原価 */
    int value;     /* 本来の価値 */
    int rec_price; /* 推奨販売価格 */
    int price;     /* 実際の (プレイヤーが決める) 価格 */
    int quality;   /* QUALITY_*: すでに atk/def/mag/dur に反映済み。二重には適用しない */
    int trait;     /* EQ_TRAIT_*: 同上。特性を持つのは Masterwork / Legendary のみ */
} Equipment;

typedef struct {
    int type, budget, min_fit, tol_pm, patience;
} Customer;

typedef struct {
    int customers, sold, revenue, cogs, upkeep, rep_delta, money_start;
    int guild_gold, guild_done, guild_expired; /* ギルド報酬は `revenue` に含まれない */
} DayReport;                                   /* net = revenue + guild_gold - cogs - upkeep */

typedef struct {
    uint32_t rng;
    int day, phase, status, fail_reason;
    int money, rep;
    int mat_stock[MAT_COUNT];
    int mat_cost[MAT_COUNT]; /* 現在の在庫に支払った合計額 */
    int price[MAT_COUNT], prev_price[MAT_COUNT], swing_pm[MAT_COUNT];
    Equipment inv[MAX_INVENTORY];
    int inv_count;
    int up_level[UP_COUNT];
    int total_revenue, total_sold, total_profit;
    Customer queue[MAX_CUSTOMERS_PER_DAY];
    int queue_count, queue_pos;
    DayReport report;
    /* ギルド契約 (専用の乱数: 契約の生成・受注・納品が `rng` に触れることはない) */
    uint32_t guild_rng;
    int guild_next_id;
    GuildContract guild_board[GUILD_OFFER_COUNT];
    int guild_board_count;
    GuildContract guild_active[GUILD_ACTIVE_MAX];
    int guild_active_count;
    int guild_done, guild_expired; /* キャンペーン累計 */
    /* 品質/特性の抽選 (専用の乱数: クラフトは `rng` や `guild_rng` に触れず、他の処理がこの乱数に触れることもない) */
    uint32_t craft_rng;
} GameState;

typedef struct {
    int result;
    int fit;
    int max_price;
} SaleJudgement;
typedef struct {
    int result, fit, price, profit, rep_delta, sold, customer_left;
} SaleOutcome;

/* ---------- 乱数 ---------- */
uint32_t rng_next(uint32_t *s);
int rng_range(uint32_t *s, int lo, int hi); /* 両端を含む */
uint32_t campaign_seed(uint32_t previous,
                       uint32_t clock); /* 次の新しいキャンペーンのシード (UI は時計を渡す)。最初のキャンペーンでは
                                           --seed N をそのまま使う */

/* ---------- ライフサイクル ---------- */
void game_init(GameState *g, uint32_t seed);
int game_validate(const GameState *g); /* 1 = すべての不変条件が成り立つ */
const char *content_validate(void);    /* NULL = すべてのコンテンツテーブルが整合している、それ以外はメッセージ */
const char *content_validate_dragon_hunt(const DragonHuntDef *h); /* 任意の Dragon Hunt 定義の検査 (NULL = 妥当) */
const char *content_validate_traits(const TraitDef defs[EQ_TRAIT_COUNT]); /* 任意の特性テーブルの検査 (NULL = 妥当) */
const char *content_validate_dragon_forging(const int pct[DRAGON_SCALE_USE_MAX + 1],
                                            const int ids[DRAGON_TRAIT_KINDS]); /* 任意の表の検査 (NULL = 妥当) */
const char *content_validate_blueprints(const GuildRankDef *ranks, int n_rank, const BlueprintDef *bps,
                                        int n_bp); /* EQUIPS / CUSTOMER_DEFS を使う */
/* 任意のテーブルに対する同じ検査 (不正な行が拒否されることをテストで示せるようにする) */
const char *content_validate_tables(const MaterialDef *m, int n_mat, const EquipDef *e, int n_eq, const CustomerDef *c,
                                    int n_cust, const UpgradeDef *u, int n_up);
int net_of(const DayReport *r);

/* ---------- 市場 ---------- */
int market_unit_price(const GameState *g, int mat);     /* 割引後、表示用 */
int market_quote(const GameState *g, int mat, int qty); /* 割引を含む合計費用。不正なら -1 */
Result market_buy(GameState *g, int mat, int qty);
void market_update(GameState *g);

/* ---------- クラフト ---------- */
void equipment_compute_stats(Equipment *e,
                             int forge_level); /* e->type/main/sub と Forge から求めた基本能力。品質も特性も含まない */
/* 品質/特性の層。すべて純粋関数で、GameState なしでテストできる。
 *   quality_rates()       ギルドランクと Forge レベルに応じた確率 (パーセント、合計 100)。Forge は 1 レベルにつき
 *                         5 ポイントを Common から Fine へ移すが、Common の残りを超えては移さない
 *   quality_from_roll()   ロール 0..99 -> 品質 (Common が最も低いロール、Legendary が最も高いロールを取る)
 *   trait_from_roll()     ロール 0..EQ_TRAIT_STANDARD_COUNT-2 -> 通常特性の ID (EQ_TRAIT_NONE にも特殊特性にもならない)
 *   dragon_scale_uses()   レシピ (主, 副) が使う Dragon Scale の数 0..3
 *   chance_roll_hits()    確率 pct (%) のロール 0..99 の判定: roll < pct (範囲外のロールは丸める。pct <= 0
 * は決して当たらず、100 以上は必ず当たる) dragon_trait_chance_pct() / dragon_trait_hits()   使用数 -> 特殊特性の確率 /
 * ロール 0..99 が当たりか dragon_trait_from_roll()  ロール 0..DRAGON_TRAIT_KINDS-1 -> 特殊特性の ID (均等)
 *   craft_roll_trait()    craft_equipment() が使う特性の抽選 (craft_rng だけを引く)。Dragon Scale
 * を使わない、または特性のない 品質では、従来と同じ乱数消費 (特性ありの品質で 1 回、それ以外 0 回)
 *   equipment_apply_quality()  equipment_compute_stats() が出したばかりの能力に品質と特性を反映する。
 *                         アイテムがすでに品質/特性を持つ場合や組み合わせが不正な場合は拒否する
 *                         (0 を返し、e には触れない) ので、二重に適用されることはない
 *   equipment_build()     基本能力 + 品質 + 特性を一度に行う。同じアイテムに再度呼んでも安全 (最初から作り直す) */
void quality_rates(int rank, int forge_level, int out[QUALITY_COUNT]);
int quality_from_roll(int rank, int forge_level, int roll);
int trait_from_roll(int roll);
int chance_roll_hits(int pct, int roll);
int dragon_scale_uses(int main, int sub);
int dragon_trait_chance_pct(int uses);
int dragon_trait_hits(int uses, int roll);
int dragon_trait_from_roll(int roll);
int craft_roll_trait(uint32_t *craft_rng, int quality, int main, int sub);
int equipment_apply_quality(Equipment *e, int quality, int trait);
void equipment_build(Equipment *e, int forge_level, int quality, int trait);
void trait_effect_text(int trait, int quality, char *buf, size_t n); /* 例: "+2 ATK"、"-1 WT"。なければ空文字列 */
const char *quality_name(int quality);                               /* 不正な ID なら "?" */
const char *trait_name(int trait);
void craft_migrate_legacy(GameState *g); /* v3 より前の状態に craft_rng を与える (乱数は消費しない) */
void equipment_price(Equipment *e);      /* e->cost と能力から value と rec_price を求める */
int estimate_craft_cost(const GameState *g, int main, int sub);
Result can_craft(const GameState *g, int eq, int main, int sub);
Result craft_equipment(GameState *g, int eq, int main, int sub, int *out_idx);

/* ---------- 在庫 / 価格設定 ---------- */
Result inv_set_price(GameState *g, int idx, int price);
Result inv_adjust_price(GameState *g, int idx, int delta);
Result inv_reset_price(GameState *g, int idx);
int liquidation_value(const Equipment *e);
Result inv_liquidate(GameState *g, int idx, int *out_value);

/* ---------- 客と日の進行 ---------- */
void customer_generate(uint32_t *rng, int rep, int guild_done, Customer *out); /* 解放済みの客タイプのみ */
int equip_fit(int customer_type, const Equipment *e);                          /* 0..100 */
SaleJudgement judge_sale(const Customer *c, const Equipment *e, int price);    /* 純粋関数 */
int sale_rep_gain(int fit, int price, int max_price);
int customers_per_day(const GameState *g);
Result day_open(GameState *g);
Result customer_offer(GameState *g, int inv_idx, SaleOutcome *out);
Result customer_decline(GameState *g);
Result day_close_early(GameState *g);
Result day_advance(GameState *g);               /* SETTLE -> 翌日の PREP */
Result campaign_continue_sandbox(GameState *g); /* クリア後 */

/* ---------- ギルドランク / 設計図 (すべて `done` = GameState.guild_done から導出。不正な引数でも安全) ---------- */
int guild_rank(int done);                /* RANK_*。負の値は 0 として扱う */
const char *guild_rank_name(int rank);   /* 不正なランクなら "?" */
int guild_rank_need(int rank);           /* そのランクに必要な契約数。不正なら -1 */
int guild_next_rank_remaining(int done); /* 次のランクまでに不足している契約数。最高ランクなら -1 */
int guild_next_blueprint(int done);      /* 次のランクで解放される BP_*。最高ランクなら -1 */
int blueprint_unlocked(int done, int bp);
int blueprint_of_equip(int eq);          /* この装備を制限している BP_*。常に使える装備なら -1 */
int equip_unlocked(int done, int eq);    /* 設計図が制限していない、またはその設計図が解放済みなら 1 */
int customer_unlocked(int done, int ct); /* 求められる装備が解放されると客が現れる */

/* ---------- ギルド ---------- */
void guild_refresh_board(GameState *g);                /* g->day 用の新しい掲示板 (guild_rng のみ使用) */
void guild_migrate_legacy(GameState *g);               /* ギルド導入前 (セーブ v1) の状態にギルド状態を与える */
GuildResult guild_accept(GameState *g, int offer_idx); /* PH_PREP: 掲示板 -> 受注中。期限を設定する */
GuildResult guild_deliver(GameState *g, int active_idx, int inv_idx, GuildOutcome *out);
int guild_item_fit(const GameState *g, int active_idx,
                   int inv_idx); /* -1 = 納品できない (アイテムの種類違い / 添字が不正) */
/* Dragon Hunt の判定 (純粋関数: 乱数を引かない)。guild_refresh_board() が使うのと同じ規則。
 *   guild_dragon_hunt_eligible()   guild_done が抽選の対象になるか (done >= GUILD_DRAGON_HUNT.min_done)
 *   guild_dragon_hunt_roll_hits()  ロール 0..99 が出現に当たるか (roll < spawn_pct。範囲外のロールは 0..99 に丸める)
 *   guild_kind_name()              契約種別の表示名。不正な値なら "?" */
int guild_dragon_hunt_eligible(int done);
int guild_dragon_hunt_roll_hits(int roll);
const char *guild_kind_name(int kind);

/* ---------- アップグレード ---------- */
int upgrade_cost(const GameState *g, int up); /* 最大レベルなら -1 */
int upgrade_effect(int up, int level);
void upgrade_effect_text(int up, int level, char *buf, size_t n); /* 例: "4 customers / day" */
Result upgrade_buy(GameState *g, int up);

/* ---------- セーブ / ロード (save.c) ---------- */
typedef enum { SAVE_OK = 0, SAVE_ERR_IO, SAVE_ERR_FORMAT, SAVE_ERR_VERSION, SAVE_ERR_VALUE } SaveResult;
/* SAVE_VERSION を上げるのは、既存のレコードの意味が変わったとき、または ID を削除・並べ替えたときだけ。
 * コンテンツの追加 (テーブル末尾への新 ID) では上げなくてよい: save.c を参照。 */
/* 2: ギルド契約 (v1 のセーブはロード時に移行)、3: 装備の品質/特性 + craft_rng (v1/v2 は移行)、
 * 4: 契約の種類と報酬素材 (gb/ga の末尾 3 フィールド。v1〜v3 の契約は通常契約・報酬なしとして移行) */
#define SAVE_VERSION 4
#define SAVE_MIN_VERSION 1 /* game_load() が今も読める最も古い形式 */
SaveResult game_save(const GameState *g, FILE *f);
SaveResult game_load(GameState *out, FILE *f); /* SAVE_OK でない限り *out には触れない */
SaveResult game_save_file(const GameState *g, const char *path);
SaveResult game_load_file(GameState *out, const char *path);
int save_file_exists(const char *path);
void save_test_set_replace(int (*fn)(const char *tmp, const char *path)); /* テスト用フック */

#endif
