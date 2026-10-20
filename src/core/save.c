/* セーブ / ロード: プレーンテキストで、1 行に 1 つの "key values..." レコード。
 *
 *   LOOTSMITH_SAVE 3
 *   counts <mat> <eq> <cust> <up>   保存時に各コンテンツテーブルに存在した ID の数
 *   rng <u32>  day <n>  phase <n>  status <n>  fail <n>  money <n>  rep <n>
 *   mat <i> <stock> <cost> <price> <prev> <swing>
 *   up <i> <level>
 *   tot <revenue> <sold> <profit>
 *   report <customers> <sold> <revenue> <cogs> <upkeep> <rep_delta> <money_start>
 *   greport <guild_gold> <done> <expired>            (v2) 日次レポートのギルド部分
 *   guild <rng> <next_id> <done> <expired>           (v2) ギルドの乱数状態、次の契約 ID、キャンペーン累計
 *   gboard <count>              続いて <count> 行 (v2、掲示板):
 *   gb <id> <customer_type> <min_fit> <difficulty> <gold> <rep> <days> <deadline> <kind> <reward_mat> <reward_qty>
 *   gactive <count>             続いて <count> 行 (v2、受注済み契約。deadline = 納品できる最終日):
 *   ga <id> <customer_type> <min_fit> <difficulty> <gold> <rep> <days> <deadline> <kind> <reward_mat> <reward_qty>
 *                                                    (末尾の <kind> <reward_mat> <reward_qty> は v4。v2/v3 の契約は 8
 * フィールド) craft <u32>                                      (v3) クラフトの乱数状態 (装備の品質/特性の抽選) inv
 * <count>                 続いて <count> 行: item <type> <main> <sub> <atk> <def> <mag> <dur> <wt> <cost> <value> <rec>
 * <price> <quality> <trait>
 *                                                    (<quality> <trait> は v3。v1/v2 のアイテムは 12 フィールド)
 *   queue <count> <pos>         続いて <count> 行:
 *   cust <type> <budget> <min_fit> <tol_pm> <patience>
 *   end
 *
 * 互換性ポリシー
 *   コンテンツ ID (素材、装備、客、アップグレード) は追記のみ (simulation.h を参照) なので、古いビルドが書いた
 *   セーブが参照する ID は、今も存在し意味も変わっていない。
 *     - セーブが書かれた後に追加されたコンテンツ: その `mat` / `up` レコードは単に存在しない。「破損」とは扱わず、
 *       初期値 (在庫なし、基準価格、アップグレードのレベル 0) で埋める。このために SAVE_VERSION を上げる必要はない。
 *     - `counts` はどのレコードが存在するはずかを正確に教えてくれる。したがって、セーブが *知っていた* ID の
 *       レコードが欠けていれば、やはり破損として報告する。`counts` が導入される前に書かれたセーブは、
 *       LEGACY_COUNTS (最初のリリースの内容) を持つものとして扱う。
 *     - このビルドより多くのコンテンツを知っているセーブ (`counts` が自分より大きい) は、より新しいゲームが
 *       書いたものである: SAVE_ERR_VERSION を返し、何も読み込まない。
 *   SAVE_VERSION を上げるのは、既存のレコードの意味が変わったとき、ID を削除・並べ替えたとき、または
 *   (バージョン 2 のように) 既定値で済ませられない状態を追加したときだけ。
 *   そのときは SAVE_VERSION を上げ、古いレイアウトの読み込みを残し、game_load() の「version fix-ups」ブロックで
 *   変換する (このブロックは解析の後、検証の前に実行され、`version` を参照できる)。
 *   バージョン 2 でギルドを追加した。バージョン 1 のファイルにはギルドのレコードがない (ギルドのキーがあっても
 *   未知のキーと同様に無視する)。fix-up ブロックが guild_migrate_legacy() でギルド状態を与える: ギルドの乱数は
 *   保存された `rng` と日付から (それを消費せずに) 導出し、受注中の契約はなし、累計は 0、掲示板は現在の日付用に
 *   新しく生成する。それ以外はそのまま引き継ぐ。バージョン 2
 * のファイルはギルドのレコードをすべて含んでいなければならない。 バージョン 3 で装備の品質 + 特性 (すべての `item`
 * の末尾の 2 フィールド) と `craft` 乱数キーを追加した。 v3 ではどちらも必須。v1/v2 のアイテムには品質がない: Common /
 * 特性なしとして読み込み、保存された能力値・原価・ 価値・価格はそのまま引き継ぐ
 * (何も再計算しない)。craft_migrate_legacy() は、どの乱数ストリームも消費せずに、
 *   保存された状態からクラフトの乱数を導出する。v3 ではさらに、1 つしかないはずのキーの重複を拒否する。
 *   未知のキーは無視する (前方互換性)。読み込んだ状態は game_validate() で検証し、すべて問題ない場合にだけ
 *   呼び出し側へコピーする。
 *   バージョン 4 で契約の種類と特別報酬 (`gb` / `ga` の末尾 3 フィールド <kind> <reward_mat> <reward_qty>) を追加した。
 *   v4 では 11 フィールドすべてが必須。v1〜v3 の契約は 8 フィールドで、種類のない通常契約 (kind = 0、報酬なし)
 *   として復元する: 旧契約から特別報酬が付くことはない。素材 Dragon Scale の追加そのものは ID の追記なので、
 *   旧セーブの `mat` レコードは足りないまま (在庫 0・基準価格で埋める)。
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* -std=c11 での fileno / fsync */
#endif
#include "simulation.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#undef R_OK /* unistd.h のマクロは、こちらの Result enum を隠してしまう */
#endif

/* ---- 数値の解析 ----
 * 値が収まらない場合の sscanf("%d") は C では未定義動作なので、数値は strtoll() で読む: 10 進のみ、
 * 1 桁以上、ERANGE と範囲を検査し、トークンは空白で終わらなければならない ("12x" は 12 ではない)。
 * 数値でない、または収まらない値は、そのレコードを不正 (SAVE_ERR_FORMAT) とする。 */
static int scan_token(const char **pp, long long lo, long long hi, long long *out) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (!(*p == '-' || *p == '+' || (*p >= '0' && *p <= '9'))) {
        return 0;
    }
    char *end;
    errno = 0;
    long long v = strtoll(p, &end, 10);
    if (end == p || errno == ERANGE || v < lo || v > hi) {
        return 0;
    }
    if (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
        return 0;
    }
    *out = v;
    *pp = end;
    return 1;
}
static int scan_i(const char **pp, int n, ...) { /* 最大 n 個の int を読み、読めた個数を返す (sscanf と同様) */
    va_list ap;
    va_start(ap, n);
    int got = 0;
    for (; got < n; got++) {
        long long v;
        int *dst = va_arg(ap, int *);
        if (!scan_token(pp, INT_MIN, INT_MAX, &v)) {
            break;
        }
        *dst = (int)v;
    }
    va_end(ap);
    return got;
}
static int scan_u32(const char **pp, uint32_t *dst) {
    long long v;
    if (!scan_token(pp, 0, 4294967295LL, &v)) {
        return 0;
    }
    *dst = (uint32_t)v;
    return 1;
}

SaveResult game_save(const GameState *g, FILE *f) {
    if (!f) {
        return SAVE_ERR_IO;
    }
    fprintf(f, "LOOTSMITH_SAVE %d\n", SAVE_VERSION);
    fprintf(f, "counts %d %d %d %d\n", MAT_COUNT, EQ_COUNT, CUST_TYPE_COUNT, UP_COUNT);
    fprintf(f, "rng %u\nday %d\nphase %d\nstatus %d\nfail %d\nmoney %d\nrep %d\n", g->rng, g->day, g->phase, g->status,
            g->fail_reason, g->money, g->rep);
    for (int m = 0; m < MAT_COUNT; m++) {
        fprintf(f, "mat %d %d %d %d %d %d\n", m, g->mat_stock[m], g->mat_cost[m], g->price[m], g->prev_price[m],
                g->swing_pm[m]);
    }
    for (int u = 0; u < UP_COUNT; u++) {
        fprintf(f, "up %d %d\n", u, g->up_level[u]);
    }
    fprintf(f, "tot %d %d %d\n", g->total_revenue, g->total_sold, g->total_profit);
    const DayReport *r = &g->report;
    fprintf(f, "report %d %d %d %d %d %d %d\n", r->customers, r->sold, r->revenue, r->cogs, r->upkeep, r->rep_delta,
            r->money_start);
    fprintf(f, "greport %d %d %d\n", r->guild_gold, r->guild_done, r->guild_expired);
    fprintf(f, "guild %u %d %d %d\n", g->guild_rng, g->guild_next_id, g->guild_done, g->guild_expired);
    fprintf(f, "gboard %d\n", g->guild_board_count);
    for (int i = 0; i < g->guild_board_count; i++) {
        const GuildContract *c = &g->guild_board[i];
        fprintf(f, "gb %d %d %d %d %d %d %d %d %d %d %d\n", c->id, c->customer_type, c->min_fit, c->difficulty, c->gold,
                c->rep, c->days, c->deadline_day, c->kind, c->reward_mat, c->reward_qty);
    }
    fprintf(f, "gactive %d\n", g->guild_active_count);
    for (int i = 0; i < g->guild_active_count; i++) {
        const GuildContract *c = &g->guild_active[i];
        fprintf(f, "ga %d %d %d %d %d %d %d %d %d %d %d\n", c->id, c->customer_type, c->min_fit, c->difficulty, c->gold,
                c->rep, c->days, c->deadline_day, c->kind, c->reward_mat, c->reward_qty);
    }
    fprintf(f, "craft %u\n", g->craft_rng);
    fprintf(f, "inv %d\n", g->inv_count);
    for (int i = 0; i < g->inv_count; i++) {
        const Equipment *e = &g->inv[i];
        fprintf(f, "item %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n", e->type, e->main, e->sub, e->atk, e->def, e->mag,
                e->dur, e->wt, e->cost, e->value, e->rec_price, e->price, e->quality, e->trait);
    }
    fprintf(f, "queue %d %d\n", g->queue_count, g->queue_pos);
    for (int i = 0; i < g->queue_count; i++) {
        const Customer *c = &g->queue[i];
        fprintf(f, "cust %d %d %d %d %d\n", c->type, c->budget, c->min_fit, c->tol_pm, c->patience);
    }
    fprintf(f, "end\n");
    return ferror(f) ? SAVE_ERR_IO : SAVE_OK;
}

enum {
    F_RNG = 1,
    F_DAY = 2,
    F_PHASE = 4,
    F_STATUS = 8,
    F_MONEY = 16,
    F_REP = 32,
    F_TOT = 64,
    F_REPORT = 128,
    F_INV = 256,
    F_QUEUE = 512,
    F_END = 1024,
    F_FAIL = 2048,
    F_COUNTS = 4096,
    F_GUILD = 8192,
    F_GREPORT = 16384,
    F_GBOARD = 32768,
    F_GACTIVE = 65536,
    F_CRAFT = 131072,
    F_GUILD_ALL = F_GUILD | F_GREPORT | F_GBOARD | F_GACTIVE,
    F_ALL = F_RNG | F_DAY | F_PHASE | F_STATUS | F_MONEY | F_REP | F_TOT | F_REPORT | F_INV | F_QUEUE | F_END | F_FAIL
};

/* `counts` 行が導入される前 (最初のリリース) に書かれたセーブに存在した ID。 */
typedef struct {
    int mat, eq, cust, up;
} ContentCounts;
static const ContentCounts LEGACY_COUNTS = {3, 3, 4, 3};

SaveResult game_load(GameState *out, FILE *f) {
    if (!f) {
        return SAVE_ERR_IO;
    }
    char line[256];
    int version = 0;
    if (!fgets(line, sizeof line, f)) {
        return SAVE_ERR_FORMAT;
    }
    { /* "LOOTSMITH_SAVE <n>" */
        const char *hp = line + sizeof "LOOTSMITH_SAVE" - 1;
        if (strncmp(line, "LOOTSMITH_SAVE", sizeof "LOOTSMITH_SAVE" - 1) != 0 || (*hp != ' ' && *hp != '\t')) {
            return SAVE_ERR_FORMAT;
        }
        if (scan_i(&hp, 1, &version) != 1) {
            return SAVE_ERR_FORMAT;
        }
    }
    if (version < SAVE_MIN_VERSION || version > SAVE_VERSION) {
        return SAVE_ERR_VERSION;
    }

    GameState t;
    memset(&t, 0, sizeof t);
    unsigned flags = 0;
    unsigned char mat_seen[MAT_COUNT] = {0}, up_seen[UP_COUNT] = {0};
    int inv_decl = -1, queue_decl = -1, items = 0, custs = 0, id_beyond_build = 0;
    int gb_decl = -1, ga_decl = -1, gb_n = 0, ga_n = 0;
    ContentCounts saved = LEGACY_COUNTS;

    while (fgets(line, sizeof line, f)) {
        char key[24];
        int off = 0;
        if (strchr(line, '\n') == NULL && !feof(f)) {
            return SAVE_ERR_FORMAT; /* 長すぎる行 */
        }
        if (sscanf(line, "%23s %n", key, &off) < 1) {
            continue; /* 空行 */
        }
        const char *p = line + off;
        uint32_t u;
        const char *q_;
        int a, b, c, d, e2, f2, g2, h, i2, j, k, l, qa = 0, qb = 0;
#define SCAN(n, ...) (q_ = p, scan_i(&q_, n, __VA_ARGS__))
#define ONCE(flag)                                                                                                     \
    do {                                                                                                               \
        if (version >= 3 && (flags & (flag)))                                                                          \
            return SAVE_ERR_FORMAT;                                                                                    \
    } while (0) /* v3: 1 つしかないキーの重複は不可 */
        if (!strcmp(key, "counts")) {
            if (flags & F_COUNTS) {
                return SAVE_ERR_FORMAT; /* 重複 */
            }
            if (SCAN(4, &saved.mat, &saved.eq, &saved.cust, &saved.up) != 4) {
                return SAVE_ERR_FORMAT;
            }
            if (saved.mat < 0 || saved.eq < 0 || saved.cust < 0 || saved.up < 0) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_COUNTS;
        } else if (!strcmp(key, "rng")) {
            ONCE(F_RNG);
            q_ = p;
            if (!scan_u32(&q_, &u)) {
                return SAVE_ERR_FORMAT;
            }
            t.rng = u;
            flags |= F_RNG;
        } else if (!strcmp(key, "day")) {
            ONCE(F_DAY);
            if (SCAN(1, &t.day) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_DAY;
        } else if (!strcmp(key, "phase")) {
            ONCE(F_PHASE);
            if (SCAN(1, &t.phase) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_PHASE;
        } else if (!strcmp(key, "status")) {
            ONCE(F_STATUS);
            if (SCAN(1, &t.status) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_STATUS;
        } else if (!strcmp(key, "fail")) {
            ONCE(F_FAIL);
            if (SCAN(1, &t.fail_reason) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_FAIL;
        } else if (!strcmp(key, "money")) {
            ONCE(F_MONEY);
            if (SCAN(1, &t.money) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_MONEY;
        } else if (!strcmp(key, "rep")) {
            ONCE(F_REP);
            if (SCAN(1, &t.rep) != 1) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_REP;
        } else if (version >= 3 && !strcmp(key, "craft")) {
            ONCE(F_CRAFT);
            q_ = p;
            if (!scan_u32(&q_, &u)) {
                return SAVE_ERR_FORMAT;
            }
            t.craft_rng = u;
            flags |= F_CRAFT;
        } else if (!strcmp(key, "mat")) {
            if (SCAN(6, &a, &b, &c, &d, &e2, &f2) != 6) {
                return SAVE_ERR_FORMAT;
            }
            if (a < 0) {
                return SAVE_ERR_VALUE;
            }
            if (a >= MAT_COUNT) {
                id_beyond_build = 1;
                continue;
            } /* 後で判定する (新しいセーブか、壊れたセーブか) */
            if (mat_seen[a]) {
                return SAVE_ERR_FORMAT; /* 重複レコード: 後勝ちの上書きはしない */
            }
            t.mat_stock[a] = b;
            t.mat_cost[a] = c;
            t.price[a] = d;
            t.prev_price[a] = e2;
            t.swing_pm[a] = f2;
            mat_seen[a] = 1;
        } else if (!strcmp(key, "up")) {
            if (SCAN(2, &a, &b) != 2) {
                return SAVE_ERR_FORMAT;
            }
            if (a < 0) {
                return SAVE_ERR_VALUE;
            }
            if (a >= UP_COUNT) {
                id_beyond_build = 1;
                continue;
            }
            if (up_seen[a]) {
                return SAVE_ERR_FORMAT; /* 重複レコード: 後勝ちの上書きはしない */
            }
            t.up_level[a] = b;
            up_seen[a] = 1;
        } else if (!strcmp(key, "tot")) {
            ONCE(F_TOT);
            if (SCAN(3, &t.total_revenue, &t.total_sold, &t.total_profit) != 3) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_TOT;
        } else if (!strcmp(key, "report")) {
            DayReport *r = &t.report;
            ONCE(F_REPORT);
            if (SCAN(7, &r->customers, &r->sold, &r->revenue, &r->cogs, &r->upkeep, &r->rep_delta, &r->money_start) !=
                7) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_REPORT;
        } else if (!strcmp(key, "inv")) {
            ONCE(F_INV);
            if (SCAN(1, &inv_decl) != 1) {
                return SAVE_ERR_FORMAT;
            }
            if (inv_decl < 0 || inv_decl > MAX_INVENTORY) {
                return SAVE_ERR_VALUE;
            }
            flags |= F_INV;
        } else if (!strcmp(key, "item")) {
            if (items >= MAX_INVENTORY) {
                return SAVE_ERR_VALUE;
            }
            int need = version >= 3 ? 14 : 12; /* v3 は <quality> <trait> を追加する */
            if (SCAN(14, &a, &b, &c, &d, &e2, &f2, &g2, &h, &i2, &j, &k, &l, &qa, &qb) < need) {
                return SAVE_ERR_FORMAT;
            }
            Equipment *q = &t.inv[items++];
            q->type = a;
            q->main = b;
            q->sub = c;
            q->atk = d;
            q->def = e2;
            q->mag = f2;
            q->dur = g2;
            q->wt = h;
            q->cost = i2;
            q->value = j;
            q->rec_price = k;
            q->price = l;
            if (version >= 3) {
                q->quality = qa;
                q->trait = qb;
            } /* 古いアイテムは Common / 特性なしのまま: 能力値には触れない */
        } else if (!strcmp(key, "queue")) {
            ONCE(F_QUEUE);
            if (SCAN(2, &queue_decl, &t.queue_pos) != 2) {
                return SAVE_ERR_FORMAT;
            }
            if (queue_decl < 0 || queue_decl > MAX_CUSTOMERS_PER_DAY) {
                return SAVE_ERR_VALUE;
            }
            t.queue_count = queue_decl;
            flags |= F_QUEUE;
        } else if (!strcmp(key, "cust")) {
            if (custs >= MAX_CUSTOMERS_PER_DAY) {
                return SAVE_ERR_VALUE;
            }
            if (SCAN(5, &a, &b, &c, &d, &e2) != 5) {
                return SAVE_ERR_FORMAT;
            }
            Customer *q = &t.queue[custs++];
            q->type = a;
            q->budget = b;
            q->min_fit = c;
            q->tol_pm = d;
            q->patience = e2;
        } else if (version >= 2 && !strcmp(key, "greport")) {
            if (flags & F_GREPORT) {
                return SAVE_ERR_FORMAT;
            }
            if (SCAN(3, &t.report.guild_gold, &t.report.guild_done, &t.report.guild_expired) != 3) {
                return SAVE_ERR_FORMAT;
            }
            flags |= F_GREPORT;
        } else if (version >= 2 && !strcmp(key, "guild")) {
            if (flags & F_GUILD) {
                return SAVE_ERR_FORMAT;
            }
            q_ = p;
            if (!scan_u32(&q_, &u) || scan_i(&q_, 3, &t.guild_next_id, &t.guild_done, &t.guild_expired) != 3) {
                return SAVE_ERR_FORMAT;
            }
            t.guild_rng = u;
            flags |= F_GUILD;
        } else if (version >= 2 && !strcmp(key, "gboard")) {
            if (flags & F_GBOARD) {
                return SAVE_ERR_FORMAT;
            }
            if (SCAN(1, &gb_decl) != 1) {
                return SAVE_ERR_FORMAT;
            }
            if (gb_decl < 0 || gb_decl > GUILD_OFFER_COUNT) {
                return SAVE_ERR_VALUE;
            }
            t.guild_board_count = gb_decl;
            flags |= F_GBOARD;
        } else if (version >= 2 && !strcmp(key, "gactive")) {
            if (flags & F_GACTIVE) {
                return SAVE_ERR_FORMAT;
            }
            if (SCAN(1, &ga_decl) != 1) {
                return SAVE_ERR_FORMAT;
            }
            if (ga_decl < 0 || ga_decl > GUILD_ACTIVE_MAX) {
                return SAVE_ERR_VALUE;
            }
            t.guild_active_count = ga_decl;
            flags |= F_GACTIVE;
        } else if (version >= 2 && (!strcmp(key, "gb") || !strcmp(key, "ga"))) {
            int board = key[1] == 'b';
            if (board ? gb_n >= GUILD_OFFER_COUNT : ga_n >= GUILD_ACTIVE_MAX) {
                return SAVE_ERR_VALUE;
            }
            GuildContract *q = board ? &t.guild_board[gb_n++] : &t.guild_active[ga_n++];
            int need_fields = version >= 4 ? 11 : 8; /* v4 は <kind> <reward_mat> <reward_qty> を追加する */
            if (SCAN(11, &q->id, &q->customer_type, &q->min_fit, &q->difficulty, &q->gold, &q->rep, &q->days,
                     &q->deadline_day, &q->kind, &q->reward_mat, &q->reward_qty) < need_fields) {
                return SAVE_ERR_FORMAT;
            }
            if (version < 4) {
                q->kind = GUILD_CONTRACT_STANDARD; /* 旧契約は通常契約: 特別報酬は付かない */
                q->reward_mat = 0;
                q->reward_qty = 0;
            }
        } else if (!strcmp(key, "end")) {
            flags |= F_END;
            break;
        }
#undef ONCE
#undef SCAN
        /* 未知のキー: 意図的に無視する */
    }
    if ((flags & F_ALL) != F_ALL) {
        return SAVE_ERR_FORMAT;
    }
    if (items != inv_decl || custs != queue_decl) {
        return SAVE_ERR_FORMAT;
    }
    if (version >= 3 && !(flags & F_CRAFT)) {
        return SAVE_ERR_FORMAT; /* v3 はクラフトの乱数を持っていなければならない */
    }
    if (version >= 2) { /* ギルドのレコードはすべて、件数も一致した状態で存在しなければならない */
        if ((flags & F_GUILD_ALL) != F_GUILD_ALL) {
            return SAVE_ERR_FORMAT;
        }
        if (gb_n != gb_decl || ga_n != ga_decl) {
            return SAVE_ERR_FORMAT;
        }
    }

    /* 書き手はどのコンテンツを知っていたか。より新しいビルドのセーブは、ここでは表現できない。 */
    if (saved.mat > MAT_COUNT || saved.eq > EQ_COUNT || saved.cust > CUST_TYPE_COUNT || saved.up > UP_COUNT) {
        return SAVE_ERR_VERSION;
    }
    if (id_beyond_build) {
        return SAVE_ERR_VALUE; /* 書き手自身も宣言していない ID のレコード */
    }

    /* 書き手のコンテンツから導かれるレコードはすべて存在しなければならない。
     * その後に追加されたコンテンツには既定値を入れる。 */
    for (int m = 0; m < MAT_COUNT; m++) {
        if (m < saved.mat) {
            if (!mat_seen[m]) {
                return SAVE_ERR_FORMAT;
            }
            continue;
        }
        t.mat_stock[m] = 0;
        t.mat_cost[m] = 0;
        t.swing_pm[m] = 0;
        t.price[m] = t.prev_price[m] = MATERIALS[m].base_price;
        if (mat_seen[m]) {
            return SAVE_ERR_FORMAT; /* 宣言された件数を超えるレコード */
        }
    }
    for (int u = 0; u < UP_COUNT; u++) {
        if (u < saved.up) {
            if (!up_seen[u]) {
                return SAVE_ERR_FORMAT;
            }
            continue;
        }
        t.up_level[u] = 0;
        if (up_seen[u]) {
            return SAVE_ERR_FORMAT;
        }
    }
    /* アイテム/客は、書き手が知っていた ID だけを使える (それ以外は「新しい」のではなく不整合)。 */
    for (int i = 0; i < items; i++) {
        const Equipment *q = &t.inv[i];
        if (q->type >= saved.eq || q->main >= saved.mat || q->sub >= saved.mat) {
            return SAVE_ERR_VALUE;
        }
    }
    for (int i = 0; i < custs; i++) {
        if (t.queue[i].type >= saved.cust) {
            return SAVE_ERR_VALUE;
        }
    }
    if (version >= 2) { /* 契約は、書き手が知っていた客だけを指名できる */
        for (int i = 0; i < t.guild_board_count; i++) {
            if (t.guild_board[i].customer_type >= saved.cust) {
                return SAVE_ERR_VALUE;
            }
        }
        for (int i = 0; i < t.guild_active_count; i++) {
            if (t.guild_active[i].customer_type >= saved.cust) {
                return SAVE_ERR_VALUE;
            }
        }
        for (int i = 0; i < GUILD_OFFER_COUNT + GUILD_ACTIVE_MAX; i++) { /* 報酬素材も、書き手が知っていた素材だけ */
            const GuildContract *k = i < t.guild_board_count ? &t.guild_board[i]
                                     : i >= GUILD_OFFER_COUNT && i - GUILD_OFFER_COUNT < t.guild_active_count
                                         ? &t.guild_active[i - GUILD_OFFER_COUNT]
                                         : NULL;
            if (k && k->reward_qty > 0 && k->reward_mat >= saved.mat) {
                return SAVE_ERR_VALUE;
            }
        }
    }
    t.inv_count = items;

    /* ---- version fix-ups: 古いレイアウトの変換はここで行う ---- */
    if (version < 3) {
        craft_migrate_legacy(
            &t); /* v1/v2 -> v3: アイテムはすでに Common / 特性なしになっている。新しいのはクラフトの乱数だけ。
                  * 最初に実行するので、v1 のファイルでも guild_rng == 0 (何も消費していない) のまま見える */
    }
    if (version < 2) {
        guild_migrate_legacy(&t); /* v1 -> v2: ギルドは存在しなかった */
    }

    if (!game_validate(&t)) {
        return SAVE_ERR_VALUE;
    }
    *out = t;
    return SAVE_OK;
}

/* `path` を `tmp` で原子的に置き換える。先に `path` を削除することはない: 置き換えに失敗しても古いファイルには
 * 触れず、エラーを返す。POSIX の rename() は同一ファイルシステム上で原子的に置き換える。
 * Windows では MoveFileEx(REPLACE_EXISTING) を使う。 */
static int replace_file(const char *tmp, const char *path) {
#if defined(_WIN32)
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(tmp, path);
#endif
}

static int (*g_replace)(const char *, const char *) = replace_file;
/* テスト用フック: 置き換え処理を差し替える (NULL で既定に戻す)。置き換え失敗の再現に使う。 */
void save_test_set_replace(int (*fn)(const char *, const char *)) {
    g_replace = fn ? fn : replace_file;
}

/* <path>.tmp (同じディレクトリ) に書き込み、フラッシュして同期した後、`path` を置き換える。
 * 失敗した場合は SAVE_ERR_IO を返し、こちらが作った一時ファイルだけを削除し、`path` は元のまま残す。 */
SaveResult game_save_file(const GameState *g, const char *path) {
    char tmp[512];
    if (!path || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        return SAVE_ERR_IO;
    }
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return SAVE_ERR_IO; /* 何も作られていないので、後始末は不要 */
    }
    SaveResult r = game_save(g, f);
    if (r == SAVE_OK && fflush(f) != 0) {
        r = SAVE_ERR_IO;
    }
#if defined(_WIN32)
    if (r == SAVE_OK && _commit(_fileno(f)) != 0) {
        r = SAVE_ERR_IO;
    }
#else
    if (r == SAVE_OK && fsync(fileno(f)) != 0) {
        r = SAVE_ERR_IO;
    }
#endif
    if (fclose(f) != 0) {
        r = SAVE_ERR_IO;
    }
    if (r != SAVE_OK) {
        remove(tmp);
        return r;
    }
    if (g_replace(tmp, path) != 0) {
        remove(tmp);
        return SAVE_ERR_IO;
    }
    return SAVE_OK;
}
SaveResult game_load_file(GameState *out, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return SAVE_ERR_IO;
    }
    SaveResult r = game_load(out, f);
    fclose(f);
    return r;
}
int save_file_exists(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}
