/* LOOT SMITH - The Ascendant Era のロジックテスト (raylib 不要)。  make test / ctest
 * 新ランク Ascendant Smith、新設計図・装備・客・素材、特別契約 Relic Expedition、Astral
 * Shard、日替わりイベントを検証する。 既存の挙動 (乱数ストリームの独立・既存ランクの確率・保存形式)
 * が変わっていないことも、ここで直接確かめる。 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../src/core/simulation.h"
#include "../src/core/icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef R_OK /* <unistd.h> 系のマクロと衝突しないように (ここでは Result の R_OK を使う) */

static int g_checks = 0, g_fails = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_fails++;                                                                                                 \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        long long _a = (a), _b = (b);                                                                                  \
        if (_a != _b) {                                                                                                \
            g_fails++;                                                                                                 \
            printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b);                    \
        }                                                                                                              \
    } while (0)

/* ---------- ヘルパ ---------- */
static GameState at_done(int done, int day) { /* done 件を納品済みで、day 日目の準備フェーズ */
    GameState g;
    game_init(&g, 12345);
    g.guild_done = done;
    g.day = day;
    g.money = 20000;
    return g;
}
static int same_state(const GameState *a, const GameState *b) {
    return memcmp(a, b, sizeof *a) == 0;
}
/* 次のクラフトが目的の品質になるよう craft_rng を選ぶ (今日のイベントを含めて、実際の判定と同じ関数で) */
static void pin_craft(GameState *g, int quality) {
    int rank = guild_rank(g->guild_done), forge = g->up_level[UP_FORGE];
    for (uint32_t s = 1; s < 200000; s++) {
        uint32_t t = s;
        if (quality_from_roll_on_day(rank, forge, g->day, rng_range(&t, 0, 99)) == quality) {
            g->craft_rng = s;
            return;
        }
    }
    CHECK(0 && "pin_craft failed");
}
static int craft(GameState *g, int eq, int main, int sub) { /* 素材を足してクラフトし、在庫の添字を返す */
    g->mat_stock[main] += 3;
    g->mat_stock[sub] += 3;
    int idx = -1;
    pin_craft(g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(g, eq, main, sub, &idx), R_OK);
    return idx;
}
static void put_special(GameState *g, int kind) { /* 掲示板の 0 番に特別契約を 1 件だけ置く (表から) */
    const SpecialContractDef *d = guild_special_def(kind);
    memset(g->guild_board, 0, sizeof g->guild_board);
    GuildContract c;
    memset(&c, 0, sizeof c);
    c.id = g->guild_next_id++;
    c.customer_type = d->customer;
    c.min_fit = d->min_fit;
    c.difficulty = d->difficulty;
    c.gold = d->gold;
    c.rep = d->rep;
    c.days = d->days;
    c.kind = kind;
    c.reward_mat = d->reward_mat;
    c.reward_qty = d->reward_qty;
    g->guild_board[0] = c;
    g->guild_board_count = 1;
}
static void put_standard(GameState *g, int ct, int min_fit, int diff) {
    memset(g->guild_board, 0, sizeof g->guild_board);
    GuildContract c;
    memset(&c, 0, sizeof c);
    c.id = g->guild_next_id++;
    c.customer_type = ct;
    c.min_fit = min_fit;
    c.difficulty = diff;
    c.gold = GUILD_DIFFS[diff].gold;
    c.rep = GUILD_DIFFS[diff].rep;
    c.days = GUILD_DIFFS[diff].days;
    g->guild_board[0] = c;
    g->guild_board_count = 1;
}
static char *save_text(const GameState *g) {
    FILE *f = tmpfile();
    game_save(g, f);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}
static SaveResult load_text(GameState *out, const char *text) {
    FILE *f = tmpfile();
    fputs(text, f);
    rewind(f);
    SaveResult r = game_load(out, f);
    fclose(f);
    return r;
}
/* prefix で始まる最初の行を repl に置き換える (repl が NULL なら削除) */
static char *edit_line(const char *text, const char *prefix, const char *repl) {
    size_t cap = strlen(text) + (repl ? strlen(repl) : 0) + 4;
    char *out = malloc(cap), *w = out;
    const char *p = text;
    int done = 0;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (!done && !strncmp(p, prefix, strlen(prefix))) {
            done = 1;
            if (repl) {
                w += sprintf(w, "%s\n", repl);
            }
        } else {
            memcpy(w, p, n);
            w += n;
            *w++ = '\n';
        }
        p += n + (e ? 1 : 0);
    }
    *w = 0;
    CHECK(done);
    return out;
}
static void expect_rejected(const GameState *bad, const char *what) {
    GameState keep = at_done(3, 1), probe = keep;
    char *t = save_text(bad);
    SaveResult r = load_text(&probe, t);
    if (r == SAVE_OK) {
        printf("  (accepted: %s)\n", what);
    }
    CHECK(r != SAVE_OK);
    CHECK(same_state(&probe, &keep)); /* 読み込み失敗で GameState は部分的にも変わらない */
    free(t);
}

/* ---------- 1. ランク・設計図・客 ---------- */
static void test_rank(void) {
    puts("ascendant_rank");
    CHECK(content_validate() == NULL);
    CHECK_EQ(RANK_COUNT, 6);
    CHECK(!strcmp(guild_rank_name(RANK_ASCENDANT), "Ascendant Smith"));
    CHECK_EQ(guild_rank_need(RANK_ASCENDANT), 75);
    static const struct {
        int done, rank, remaining;
    } t[] = {{49, RANK_GRANDMASTER, 1}, {50, RANK_MYTHIC, 25},    {74, RANK_MYTHIC, 1},
             {75, RANK_ASCENDANT, -1},  {76, RANK_ASCENDANT, -1}, {5000, RANK_ASCENDANT, -1}};
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        CHECK_EQ(guild_rank(t[i].done), t[i].rank);
        CHECK_EQ(guild_next_rank_remaining(t[i].done), t[i].remaining);
    }
    CHECK_EQ(guild_next_blueprint(74), BP_ASTRAL_BLADE);
    CHECK_EQ(guild_next_blueprint(75), -1);

    /* 74 件では 3 つの新設計図・装備・客がロックされ、75 件ですべて解放される */
    static const int bp[3] = {BP_ASTRAL_BLADE, BP_AEGIS_OF_DAWN, BP_VOID_CODEX};
    static const int eq[3] = {EQ_ASTRAL_BLADE, EQ_AEGIS_OF_DAWN, EQ_VOID_CODEX};
    static const int ct[3] = {CT_DUELIST, CT_SENTINEL, CT_RIFT_SEEKER};
    static const char *const bpn[3] = {"Astral Blade Plans", "Aegis of Dawn Plans", "Void Codex Plans"};
    for (int i = 0; i < 3; i++) {
        CHECK(!strcmp(BLUEPRINTS[bp[i]].name, bpn[i]));
        CHECK_EQ(BLUEPRINTS[bp[i]].rank, RANK_ASCENDANT);
        CHECK_EQ(BLUEPRINTS[bp[i]].equip, eq[i]);
        CHECK_EQ(BLUEPRINTS[bp[i]].customer, ct[i]);
        CHECK_EQ(CUSTOMER_DEFS[ct[i]].wants_eq, eq[i]);
        for (int done = 0; done <= 74; done += (done == 70 ? 4 : 1)) {
            CHECK(!blueprint_unlocked(done, bp[i]));
            CHECK(!equip_unlocked(done, eq[i]));
            CHECK(!customer_unlocked(done, ct[i]));
        }
        for (int done = 75; done <= 80; done++) {
            CHECK(blueprint_unlocked(done, bp[i]));
            CHECK(equip_unlocked(done, eq[i]));
            CHECK(customer_unlocked(done, ct[i]));
        }
        GameState g = at_done(74, 3);
        CHECK_EQ(can_craft(&g, eq[i], MAT_IRON, MAT_IRON), R_ERR_LOCKED);
        g.guild_done = 75;
        CHECK_EQ(can_craft(&g, eq[i], MAT_IRON, MAT_IRON), R_ERR_MATERIAL); /* 解放済み。素材がないだけ */
        g.mat_stock[MAT_IRON] = 3;
        CHECK_EQ(can_craft(&g, eq[i], MAT_IRON, MAT_IRON), R_OK);
    }
    /* 旧 5 ランクの解放は変わらない (50 件で解放されるのは Wyrmcleaver だけ) */
    CHECK(blueprint_unlocked(50, BP_WYRMCLEAVER));
    CHECK(!blueprint_unlocked(49, BP_WYRMCLEAVER));
    /* 74 件までの客は、未解放の客を決して生成しない */
    uint32_t rng = 99;
    for (int i = 0; i < 20000; i++) {
        Customer c;
        customer_generate(&rng, 60, 74, &c);
        CHECK(c.type != CT_DUELIST && c.type != CT_SENTINEL && c.type != CT_RIFT_SEEKER);
    }
    int seen[CUST_TYPE_COUNT] = {0};
    for (int i = 0; i < 20000; i++) {
        Customer c;
        customer_generate(&rng, 60, 75, &c);
        seen[c.type]++;
    }
    for (int t2 = 0; t2 < CUST_TYPE_COUNT; t2++) {
        CHECK(seen[t2] > 20000 / CUST_TYPE_COUNT / 2);
    }
}

static void test_quality_odds(void) {
    puts("ascendant_quality_odds");
    static const int odds[RANK_COUNT][QUALITY_COUNT] = {{70, 25, 5, 0},   {55, 32, 12, 1},  {40, 35, 20, 5},
                                                        {25, 35, 28, 12}, {20, 33, 30, 17}, {15, 30, 35, 20}};
    for (int r = 0; r < RANK_COUNT; r++) {
        int sum = 0;
        for (int q = 0; q < QUALITY_COUNT; q++) {
            CHECK_EQ(QUALITY_RATE_PCT[r][q], odds[r][q]); /* 既存 5 ランクは変わらず、Ascendant Smith は 15/30/35/20 */
            sum += QUALITY_RATE_PCT[r][q];
        }
        CHECK_EQ(sum, 100);
    }
    /* すべてのランク x Forge x 日付で、確率は 0 以上かつ合計 100。通常の日は日付なしの API と同一 */
    for (int r = 0; r < RANK_COUNT; r++) {
        for (int f = 0; f <= UPGRADES[UP_FORGE].max_level; f++) {
            int plain[QUALITY_COUNT];
            quality_rates(r, f, plain);
            for (int day = 0; day <= 21; day++) {
                int o[QUALITY_COUNT], sum = 0;
                quality_rates_on_day(r, f, day, o);
                for (int q = 0; q < QUALITY_COUNT; q++) {
                    CHECK(o[q] >= 0);
                    sum += o[q];
                }
                CHECK_EQ(sum, 100);
                if (day_event(day) != EVENT_ARCANE_CONVERGENCE) {
                    CHECK(!memcmp(o, plain, sizeof o));
                } else {
                    CHECK(o[QUALITY_LEGENDARY] >= plain[QUALITY_LEGENDARY] + 2);
                    CHECK(o[QUALITY_MASTERWORK] >= plain[QUALITY_MASTERWORK] + 5);
                }
                for (int roll = 0; roll < 100; roll++) { /* 日付なしの quality_from_roll は日付 0 と同じ */
                    CHECK_EQ(quality_from_roll(r, f, roll), quality_from_roll_on_day(r, f, 0, roll));
                }
            }
        }
    }
    int o[QUALITY_COUNT];
    quality_rates(RANK_ASCENDANT, 0, o); /* 通常時の 15/30/35/20 */
    CHECK(o[0] == 15 && o[1] == 30 && o[2] == 35 && o[3] == 20);
    quality_rates(RANK_ASCENDANT, 3, o); /* Forge 3: Common 15 が全部 Fine へ (負にならない) */
    CHECK(o[0] == 0 && o[1] == 45 && o[2] == 35 && o[3] == 20);
}

/* ---------- 2. コンテンツ表 ---------- */
static void test_content(void) {
    puts("ascendant_content");
    static const struct {
        int eq, atk, def, mag, dur, wt;
        const char *name;
    } e[] = {{EQ_ASTRAL_BLADE, 15, 2, 4, 9, 3, "Astral Blade"},
             {EQ_AEGIS_OF_DAWN, 2, 15, 2, 13, 7, "Aegis of Dawn"},
             {EQ_VOID_CODEX, 1, 3, 16, 5, 2, "Void Codex"}};
    for (int i = 0; i < 3; i++) {
        const EquipDef *d = &EQUIPS[e[i].eq];
        CHECK(!strcmp(d->name, e[i].name));
        CHECK(d->atk == e[i].atk && d->def == e[i].def && d->mag == e[i].mag && d->dur == e[i].dur && d->wt == e[i].wt);
        CHECK(icon_exists(d->icon));
    }
    static const struct {
        int ct, eq, w[5], bmin, bmax, fmin, fmax;
    } c[] = {{CT_DUELIST, EQ_ASTRAL_BLADE, {50, 0, 10, 20, 20}, 150, 250, 58, 82},
             {CT_SENTINEL, EQ_AEGIS_OF_DAWN, {5, 55, 0, 35, 5}, 145, 235, 60, 82},
             {CT_RIFT_SEEKER, EQ_VOID_CODEX, {0, 5, 70, 20, 5}, 165, 270, 62, 84}};
    for (int i = 0; i < 3; i++) {
        const CustomerDef *d = &CUSTOMER_DEFS[c[i].ct];
        CHECK_EQ(d->wants_eq, c[i].eq);
        CHECK(d->w_atk == c[i].w[0] && d->w_def == c[i].w[1] && d->w_mag == c[i].w[2] && d->w_dur == c[i].w[3] &&
              d->w_light == c[i].w[4]);
        CHECK_EQ(d->w_atk + d->w_def + d->w_mag + d->w_dur + d->w_light, 100);
        CHECK(d->budget_min == c[i].bmin && d->budget_max == c[i].bmax);
        CHECK(d->fit_min_lo == c[i].fmin && d->fit_min_hi == c[i].fmax);
        CHECK(icon_exists(d->icon));
    }
    CHECK(!strcmp(CUSTOMER_DEFS[CT_DUELIST].name, "Duelist"));
    CHECK(!strcmp(CUSTOMER_DEFS[CT_SENTINEL].name, "Sentinel"));
    CHECK(!strcmp(CUSTOMER_DEFS[CT_RIFT_SEEKER].name, "Rift Seeker"));

    static const struct {
        int m, atk, def, mag, dur, wt, price, min;
        const char *name, *desc;
    } m[] = {
        {MAT_MITHRIL, 11, 8, 2, 9, 4, 28, 20, "Mithril", "Light and strong metal for elite weapons."},
        {MAT_MOONSTONE, 2, 3, 11, 4, 1, 24, 17, "Moonstone", "A pale crystal that channels arcane power."},
        /* Emberglass の基準価格は仕様の 22 から 12 へ調整した (Obsidian 13 より ATK
           が低く高価だと常に劣るため)。能力値は仕様どおり */
        {MAT_EMBERGLASS, 10, 2, 1, 4, 3, 12, 8, "Emberglass", "Volcanic glass with a keen but brittle edge."},
        {MAT_ASTRAL_SHARD, 5, 3, 12, 4, 1, 40, 30, "Astral Shard", "A rare shard recovered from a planar expedition."}};
    for (int i = 0; i < 4; i++) {
        const MaterialDef *d = &MATERIALS[m[i].m];
        CHECK(!strcmp(d->name, m[i].name) && !strcmp(d->desc, m[i].desc));
        CHECK(d->atk == m[i].atk && d->def == m[i].def && d->mag == m[i].mag && d->dur == m[i].dur && d->wt == m[i].wt);
        CHECK(d->base_price == m[i].price && d->min_price == m[i].min);
        CHECK(icon_exists(d->icon));
        CHECK_EQ(d->hunt_only, m[i].m == MAT_ASTRAL_SHARD);
        CHECK_EQ(d->rarity_value, m[i].m == MAT_ASTRAL_SHARD ? 25 : 0);
    }
    /* アイコン ID: 新しい素材・装備・設計図・客のどれも「?」(フォールバック) ではない */
    for (int i = 0; i < MAT_COUNT; i++) {
        CHECK(icon_exists(MATERIALS[i].icon));
    }
    for (int i = 0; i < EQ_COUNT; i++) {
        CHECK(icon_exists(EQUIPS[i].icon));
    }
    for (int i = 0; i < BP_COUNT; i++) {
        CHECK(icon_exists(BLUEPRINTS[i].icon));
    }
    for (int i = 0; i < CUST_TYPE_COUNT; i++) {
        CHECK(icon_exists(CUSTOMER_DEFS[i].icon));
    }
    /* 新しい素材は、既存の計算 (混合・価値) をそのまま通る */
    Equipment q;
    memset(&q, 0, sizeof q);
    q.type = EQ_ASTRAL_BLADE;
    q.main = MAT_MITHRIL;
    q.sub = MAT_STEEL;
    equipment_compute_stats(&q, 0);
    CHECK(q.atk > EQUIPS[EQ_ASTRAL_BLADE].atk && q.wt >= 1);
}

/* ---------- 3. 市場・Astral Shard ---------- */
static void test_materials_market(void) {
    puts("ascendant_materials");
    GameState g = at_done(0, 1); /* 1 日目 = イベントなし */
    static const int buyable[3] = {MAT_MITHRIL, MAT_MOONSTONE, MAT_EMBERGLASS};
    for (int i = 0; i < 3; i++) {
        int m = buyable[i], quote = market_quote(&g, m, 4), money = g.money;
        CHECK(quote > 0);
        CHECK_EQ(market_buy(&g, m, 4), R_OK);
        CHECK_EQ(g.mat_stock[m], 4);
        CHECK_EQ(g.money, money - quote);
        CHECK_EQ(g.mat_cost[m], quote);
        CHECK(g.price[m] >= MATERIALS[m].min_price);
    }
    /* 消費 (クラフト) できる: 3 個 */
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_MITHRIL, MAT_EMBERGLASS, NULL), R_OK);
    CHECK_EQ(g.mat_stock[MAT_MITHRIL], 2);
    CHECK_EQ(g.mat_stock[MAT_EMBERGLASS], 3);

    /* Astral Shard: 買えない。拒否は何も変えない */
    GameState before = g;
    CHECK_EQ(market_buy(&g, MAT_ASTRAL_SHARD, 1), R_ERR_NOT_FOR_SALE);
    CHECK(same_state(&g, &before));
    CHECK_EQ(market_quote(&g, MAT_ASTRAL_SHARD, 1), -1);

    /* 市場更新: 買えない素材は価格が動かず、乱数も消費しない。新しい買える素材も既存の 3 本のストリームを進めない
     * (導入済みの買える素材 7 つ分だけ `rng` を引く = 従来のキャンペーンの客・価格は 1 つも変わらない) */
    for (uint32_t s = 1; s <= 40; s++) {
        GameState x = at_done(0, 1);
        x.rng = s;
        uint32_t expect = s, guild_rng = x.guild_rng, craft_rng = x.craft_rng;
        market_update(&x);
        for (int i = 0; i < 7; i++) {
            rng_next(&expect);
        }
        CHECK(x.rng == expect);
        CHECK(x.guild_rng == guild_rng && x.craft_rng == craft_rng);
        CHECK_EQ(x.price[MAT_ASTRAL_SHARD], MATERIALS[MAT_ASTRAL_SHARD].base_price);
        CHECK_EQ(x.swing_pm[MAT_ASTRAL_SHARD], 0);
        CHECK_EQ(x.price[MAT_DRAGON_SCALE], MATERIALS[MAT_DRAGON_SCALE].base_price);
        for (int i = 0; i < 3; i++) {
            CHECK(x.price[buyable[i]] >= MATERIALS[buyable[i]].min_price);
        }
    }
    /* 新素材の価格変動も決定的 (同じ状態からは同じ価格) */
    GameState a = at_done(0, 1), b = at_done(0, 1);
    for (int d = 0; d < 30; d++) {
        market_update(&a);
        market_update(&b);
        a.day++;
        b.day++;
    }
    CHECK(same_state(&a, &b));
}

static void test_astral_use(void) {
    puts("ascendant_astral_use");
    GameState g = at_done(75, 1);
    g.mat_stock[MAT_ASTRAL_SHARD] = 3;
    g.mat_cost[MAT_ASTRAL_SHARD] = 0;
    /* 主素材 (2 個) + 副素材 (1 個) のどちらにも使え、報酬素材の原価は 0 */
    g.mat_stock[MAT_MOONSTONE] = 3;
    g.mat_cost[MAT_MOONSTONE] = 3 * 24;
    int idx = -1;
    CHECK_EQ(craft_equipment(&g, EQ_VOID_CODEX, MAT_ASTRAL_SHARD, MAT_MOONSTONE, &idx), R_OK);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 1);
    CHECK_EQ(g.mat_stock[MAT_MOONSTONE], 2);
    CHECK_EQ(g.inv[idx].cost, 24); /* Moonstone 1 個の原価だけ: Astral Shard 2 個は無料 */
    CHECK_EQ(equipment_rarity_bonus(&g.inv[idx]), 50);
    g.mat_stock[MAT_ASTRAL_SHARD] = 3;
    CHECK_EQ(craft_equipment(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_ASTRAL_SHARD, &idx), R_OK);
    CHECK_EQ(equipment_rarity_bonus(&g.inv[idx]), 25);
    g.mat_stock[MAT_ASTRAL_SHARD] = 3;
    CHECK_EQ(craft_equipment(&g, EQ_VOID_CODEX, MAT_ASTRAL_SHARD, MAT_ASTRAL_SHARD, &idx), R_OK);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 0);
    CHECK_EQ(g.inv[idx].cost, 0);
    Equipment *e = &g.inv[idx];
    CHECK_EQ(equipment_rarity_bonus(e), 75); /* 3 個分: 2 * 25 + 25 */
    /* 価値は、希少度ボーナスを除けば同じ能力・原価の装備と同じ (3 個の使用分 +75 が価値に加わる) */
    Equipment plain = *e;
    plain.main = plain.sub = MAT_MOONSTONE; /* rarity_value 0 */
    equipment_price(&plain);
    Equipment rare = *e;
    equipment_price(&rare);
    CHECK_EQ(rare.value - plain.value, 75);
    CHECK(rare.rec_price > plain.rec_price);
    CHECK(game_validate(&g));
    /* 足りなければ作れない (市場でも買えない) */
    CHECK_EQ(can_craft(&g, EQ_VOID_CODEX, MAT_ASTRAL_SHARD, MAT_MOONSTONE), R_ERR_MATERIAL);
}

/* ---------- 4. 特別契約の抽選 ---------- */
static void test_special_roll(void) {
    puts("ascendant_special_roll");
    CHECK(content_validate() == NULL);
    CHECK_EQ(guild_special_def(GUILD_CONTRACT_STANDARD) == NULL, 1);
    CHECK(guild_special_def(GUILD_CONTRACT_DRAGON_HUNT) == &GUILD_DRAGON_HUNT);
    CHECK(guild_special_def(GUILD_CONTRACT_RELIC_EXPEDITION) == &GUILD_RELIC_EXPEDITION);
    CHECK(guild_special_def(99) == NULL && guild_special_def(-1) == NULL);
    const SpecialContractDef *r = &GUILD_RELIC_EXPEDITION;
    CHECK(!strcmp(r->name, "Relic Expedition"));
    CHECK_EQ(r->customer, CT_RIFT_SEEKER);
    CHECK_EQ(CUSTOMER_DEFS[r->customer].wants_eq, EQ_VOID_CODEX);
    CHECK(r->min_fit == 72 && r->difficulty == GD_HARD && r->gold == 260 && r->rep == 5 && r->days == 3);
    CHECK(r->reward_mat == MAT_ASTRAL_SHARD && r->reward_qty == 2 && r->min_done == 75 && r->spawn_pct == 20);
    CHECK(!strcmp(guild_kind_name(GUILD_CONTRACT_RELIC_EXPEDITION), "Relic Expedition"));
    CHECK(GUILD_DRAGON_HUNT.spawn_pct == 25 && GUILD_DRAGON_HUNT.min_done == 50); /* Dragon Hunt は変わらない */

    /* 境界値を直接検査する: 長期シミュレーションに頼らない */
    for (int done = 0; done <= 49; done++) {
        CHECK(!guild_special_eligible(done));
        for (int roll = 0; roll < 100; roll++) {
            CHECK_EQ(guild_special_from_roll(done, roll), GUILD_CONTRACT_STANDARD);
        }
    }
    for (int done = 50; done <= 74; done++) { /* 従来どおり: roll < 25 が Dragon Hunt。Relic Expedition は出ない */
        CHECK(guild_special_eligible(done));
        int dragons = 0;
        for (int roll = 0; roll < 100; roll++) {
            int k = guild_special_from_roll(done, roll);
            CHECK(k == GUILD_CONTRACT_STANDARD || k == GUILD_CONTRACT_DRAGON_HUNT);
            CHECK_EQ(k == GUILD_CONTRACT_DRAGON_HUNT, guild_dragon_hunt_roll_hits(roll));
            CHECK_EQ(k == GUILD_CONTRACT_DRAGON_HUNT, roll < 25);
            dragons += k == GUILD_CONTRACT_DRAGON_HUNT;
        }
        CHECK_EQ(dragons, 25);
    }
    for (int done = 75; done <= 80; done++) { /* 1 つのロール: 0..19 Relic、20..44 Dragon、45..99 なし */
        int relic = 0, dragon = 0, none = 0;
        for (int roll = 0; roll < 100; roll++) {
            int k = guild_special_from_roll(done, roll);
            CHECK_EQ(k, roll <= 19   ? GUILD_CONTRACT_RELIC_EXPEDITION
                        : roll <= 44 ? GUILD_CONTRACT_DRAGON_HUNT
                                     : GUILD_CONTRACT_STANDARD);
            relic += k == GUILD_CONTRACT_RELIC_EXPEDITION;
            dragon += k == GUILD_CONTRACT_DRAGON_HUNT;
            none += k == GUILD_CONTRACT_STANDARD;
        }
        CHECK(relic == 20 && dragon == 25 && none == 55);
    }
    /* 範囲外のロールは 0..99 に丸められる */
    CHECK_EQ(guild_special_from_roll(75, -5), GUILD_CONTRACT_RELIC_EXPEDITION);
    CHECK_EQ(guild_special_from_roll(75, 5000), GUILD_CONTRACT_STANDARD);
}

static int count_kind(const GameState *g, int kind) {
    int n = 0;
    for (int i = 0; i < g->guild_board_count; i++) {
        n += g->guild_board[i].kind == kind;
    }
    return n;
}

static void test_board_refresh(void) {
    puts("ascendant_board");
    /* 掲示板の更新は、対象のときにロールを 1 回だけ (guild_rng から) 引き、その結果で分類する */
    int relic_n = 0, dragon_n = 0, none_n = 0, trials = 20000;
    for (int t = 0; t < trials; t++) {
        GameState g = at_done(75, 3 + t % 20);
        g.guild_rng = (uint32_t)(t * 2654435761u + 17u);
        if (!g.guild_rng) {
            g.guild_rng = 1;
        }
        uint32_t probe = g.guild_rng;
        int roll = rng_range(&probe, 0, 99), expect = guild_special_from_roll(75, roll);
        guild_refresh_board(&g);
        CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT); /* 総数は常に 3 */
        int relic = count_kind(&g, GUILD_CONTRACT_RELIC_EXPEDITION),
            dragon = count_kind(&g, GUILD_CONTRACT_DRAGON_HUNT);
        CHECK(relic + dragon <= 1); /* 特別契約は最大 1 件 */
        CHECK_EQ(relic, expect == GUILD_CONTRACT_RELIC_EXPEDITION);
        CHECK_EQ(dragon, expect == GUILD_CONTRACT_DRAGON_HUNT);
        relic_n += relic;
        dragon_n += dragon;
        none_n += !relic && !dragon;
        /* 依頼主が重ならない (通常契約と特別契約も) */
        for (int i = 0; i < g.guild_board_count; i++) {
            const GuildContract *c = &g.guild_board[i];
            CHECK(customer_unlocked(g.guild_done, c->customer_type));
            for (int j = i + 1; j < g.guild_board_count; j++) {
                CHECK(c->customer_type != g.guild_board[j].customer_type);
            }
            if (c->kind == GUILD_CONTRACT_RELIC_EXPEDITION) {
                CHECK_EQ(c->customer_type, CT_RIFT_SEEKER);
                CHECK_EQ(c->min_fit, 72);
                CHECK_EQ(c->gold, 260);
                CHECK_EQ(c->rep, 5);
                CHECK_EQ(c->days, 3);
                CHECK_EQ(c->difficulty, GD_HARD);
                CHECK_EQ(c->reward_mat, MAT_ASTRAL_SHARD);
                CHECK_EQ(c->reward_qty, 2);
            } else if (c->kind == GUILD_CONTRACT_STANDARD) {
                CHECK(c->reward_qty == 0 && c->reward_mat == 0);
            }
        }
        CHECK(game_validate(&g));
    }
    /* 率 (許容は広めの統計): Relic 20%、Dragon 25% */
    CHECK(relic_n > trials * 17 / 100 && relic_n < trials * 23 / 100);
    CHECK(dragon_n > trials * 22 / 100 && dragon_n < trials * 28 / 100);
    CHECK_EQ(relic_n + dragon_n + none_n, trials);

    /* 75 件未満では Relic Expedition は決して出ず、そのための乱数も引かない (75 未満の guild_rng の消費は従来のまま:
     * 74 件の掲示板は、ロール 1 回 + 通常契約の抽選だけ。Relic
     * の有無で消費が変わらないことを同じ手順で再現して確かめる) */
    for (int t = 0; t < 3000; t++) {
        for (int done = 0; done <= 74; done += 37) {
            GameState g = at_done(done, 5);
            g.guild_rng = (uint32_t)(t * 40503u + 11u);
            GameState h = g;
            guild_refresh_board(&g);
            CHECK_EQ(count_kind(&g, GUILD_CONTRACT_RELIC_EXPEDITION), 0);
            if (done < 50) {
                CHECK_EQ(count_kind(&g, GUILD_CONTRACT_DRAGON_HUNT), 0);
            }
            /* 再現: 50 件以上ならロール 1 回 (Dragon Hunt の判定)、続いて通常契約。0..49 件は特別契約のロールなし */
            uint32_t rng = h.guild_rng;
            int dragon = done >= 50 && guild_dragon_hunt_roll_hits(rng_range(&rng, 0, 99));
            CHECK_EQ(count_kind(&g, GUILD_CONTRACT_DRAGON_HUNT), dragon);
            CHECK(g.guild_rng != 0);
        }
    }
    /* 掲示板の更新は guild_rng だけを使う: 3 本のストリームの独立 */
    GameState g = at_done(75, 4), h = g;
    guild_refresh_board(&g);
    CHECK(g.rng == h.rng && g.craft_rng == h.craft_rng);
    CHECK(g.guild_rng != h.guild_rng);
}

/* ---------- 5. Relic Expedition の受注・納品 ---------- */
static GameState relic_game(int day) {
    GameState g = at_done(75, day);
    g.rep = 10;
    put_special(&g, GUILD_CONTRACT_RELIC_EXPEDITION);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK_EQ(g.guild_active[0].deadline_day, day + 2); /* 3 日間 (受注日を含む) */
    CHECK(game_validate(&g));
    return g;
}

static void test_relic_deliver(void) {
    puts("ascendant_relic_deliver");
    /* 正常な納品: 金・評判・Astral Shard x2。原価 0。FIT は通常の equip_fit() */
    GameState g = relic_game(1);
    int idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    int fit = guild_item_fit(&g, 0, idx);
    CHECK_EQ(fit, equip_fit(CT_RIFT_SEEKER, &g.inv[idx])); /* 専用の FIT 式はない */
    CHECK(fit >= 72);
    int money = g.money, rep = g.rep, done = g.guild_done;
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(o.gold, 260);
    CHECK_EQ(o.bonus_gold, 0);
    CHECK_EQ(g.money, money + 260);
    CHECK_EQ(g.rep, rep + 5);
    CHECK_EQ(g.guild_done, done + 1);
    CHECK_EQ(o.reward_mat, MAT_ASTRAL_SHARD);
    CHECK_EQ(o.reward_qty, 2);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 2);
    CHECK_EQ(g.mat_cost[MAT_ASTRAL_SHARD], 0); /* 報酬素材の原価は 0 */
    CHECK_EQ(g.guild_active_count, 0);
    CHECK(game_validate(&g));

    /* 間違った装備種別 / FIT 不足は拒否され、何も変わらない */
    g = relic_game(1);
    int sword = craft(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    GameState before = g;
    CHECK_EQ(guild_deliver(&g, 0, sword, &o), GUILD_ERR_WRONG_TYPE);
    CHECK(same_state(&g, &before));
    int weak = craft(&g, EQ_VOID_CODEX, MAT_IRON, MAT_IRON); /* Void Codex に Iron: FIT が 72 に届かない */
    CHECK(guild_item_fit(&g, 0, weak) < 72);
    before = g;
    CHECK_EQ(guild_deliver(&g, 0, weak, &o), GUILD_ERR_LOW_FIT);
    CHECK(same_state(&g, &before));

    /* 在庫が満杯 (98 + 2 > 99): 原子的に拒否される。装備・ゴールド・評判・契約・納品件数・素材のいずれも変わらない */
    g = relic_game(1);
    idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    g.mat_stock[MAT_ASTRAL_SHARD] = BAL.stock_cap - 1;
    before = g;
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_ERR_REWARD_FULL);
    CHECK(same_state(&g, &before));
    g.mat_stock[MAT_ASTRAL_SHARD] = BAL.stock_cap - 2; /* ちょうど入る */
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], BAL.stock_cap);
    CHECK(game_validate(&g));

    /* 準備フェーズ以外では納品できない */
    g = relic_game(1);
    idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    day_open(&g);
    before = g;
    CHECK(guild_deliver(&g, 0, idx, &o) == GUILD_ERR_PHASE);
    CHECK(same_state(&g, &before));
}

static void pass_day(GameState *g) {
    day_open(g);
    day_close_early(g);
    day_advance(g);
}

static void test_relic_expiry(void) {
    puts("ascendant_relic_expiry");
    GameState g = relic_game(1); /* 期限は 3 日目の精算まで */
    int idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    pass_day(&g); /* 1 日目 -> 2 日目 */
    pass_day(&g); /* 2 -> 3 */
    CHECK_EQ(g.guild_active_count, 1);
    CHECK_EQ(g.day, 3);
    day_open(&g);
    day_close_early(&g); /* 3 日目の精算で失効 */
    CHECK_EQ(g.guild_active_count, 0);
    CHECK_EQ(g.guild_expired, 1);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 0); /* 期限切れでは素材は付与されない */
    CHECK(g.inv_count >= 1);
    (void)idx;
    CHECK(game_validate(&g));
    /* 失効した契約は納品できない (契約がない) */
    day_advance(&g);
    GuildOutcome o;
    CHECK(guild_deliver(&g, 0, 0, &o) != GUILD_OK);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 0);
    CHECK_EQ(g.guild_done, 75);

    /* 期限最終日の納品は成功する */
    g = relic_game(1);
    idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    pass_day(&g);
    pass_day(&g);
    CHECK_EQ(g.day, 3);
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 2);
}

static void test_accept_limits(void) {
    puts("ascendant_accept_limits");
    GameState g = at_done(75, 3);
    GameState before;
    put_special(&g, GUILD_CONTRACT_RELIC_EXPEDITION);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    put_special(&g, GUILD_CONTRACT_DRAGON_HUNT);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK_EQ(g.guild_active_count, GUILD_ACTIVE_MAX); /* 同時受注数は 2 のまま */
    CHECK(game_validate(&g));
    put_special(&g, GUILD_CONTRACT_RELIC_EXPEDITION);
    before = g;
    CHECK_EQ(guild_accept(&g, 0), GUILD_ERR_FULL);
    CHECK(same_state(&g, &before));
}

/* ---------- 6. 日替わりイベント ---------- */
static void test_event_calendar(void) {
    puts("ascendant_events");
    CHECK(content_validate() == NULL);
    CHECK_EQ(EVENT_CYCLE_DAYS, 7);
    for (int day = 1; day <= 70; day++) {
        int phase = (day - 1) % 7;
        int expect = phase == 1   ? EVENT_MARKET_CARAVAN
                     : phase == 3 ? EVENT_ARCANE_CONVERGENCE
                     : phase == 5 ? EVENT_GUILD_ASSEMBLY
                                  : EVENT_NONE;
        CHECK_EQ(day_event(day), expect);
        CHECK_EQ(day_event(day), day_event(day + 7)); /* 7 日周期 */
    }
    static const int caravan[] = {2, 9, 16, 23, 30};
    for (size_t i = 0; i < sizeof caravan / sizeof *caravan; i++) {
        CHECK_EQ(day_event(caravan[i]), EVENT_MARKET_CARAVAN);
    }
    CHECK_EQ(day_event(4), EVENT_ARCANE_CONVERGENCE);
    CHECK_EQ(day_event(6), EVENT_GUILD_ASSEMBLY);
    CHECK_EQ(day_event(1), EVENT_NONE);
    CHECK_EQ(day_event(3), EVENT_NONE);
    CHECK_EQ(day_event(5), EVENT_NONE);
    CHECK_EQ(day_event(7), EVENT_NONE);
    CHECK_EQ(day_event(0), EVENT_NONE);
    CHECK_EQ(day_event(-9), EVENT_NONE);
    CHECK(!strcmp(day_event_name(EVENT_MARKET_CARAVAN), "Market Caravan"));
    CHECK(!strcmp(day_event_name(EVENT_ARCANE_CONVERGENCE), "Arcane Convergence"));
    CHECK(!strcmp(day_event_name(EVENT_GUILD_ASSEMBLY), "Guild Assembly"));
    CHECK(!strcmp(day_event_name(999), "?"));
    CHECK_EQ(event_market_discount_pct(2), 15);
    CHECK_EQ(event_market_discount_pct(4), 0);
    CHECK_EQ(event_guild_bonus(100, 6), 10);
    CHECK_EQ(event_guild_bonus(100, 5), 0);
    CHECK_EQ(event_guild_bonus(35, 6), 4); /* 3.5 -> 4 (四捨五入、0.5 は切り上げ) */
    CHECK_EQ(event_guild_bonus(55, 6), 6); /* 5.5 -> 6 */
    CHECK_EQ(event_guild_bonus(80, 6), 8);
    CHECK_EQ(event_guild_bonus(260, 6), 26);
    CHECK_EQ(event_guild_bonus(0, 6), 0);
    CHECK_EQ(event_guild_bonus(-50, 6), 0);
    /* 表の検証: 不正な表は拒否される */
    DayEventDef t[EVENT_COUNT];
    memcpy(t, DAY_EVENTS, sizeof t);
    CHECK(content_validate_events(t) == NULL);
    t[EVENT_GUILD_ASSEMBLY].phase = t[EVENT_MARKET_CARAVAN].phase;
    CHECK(content_validate_events(t) != NULL);
    memcpy(t, DAY_EVENTS, sizeof t);
    t[EVENT_ARCANE_CONVERGENCE].quality_add_legendary = 5; /* 移す量と加える量が合わない = 合計 100 が崩れる */
    CHECK(content_validate_events(t) != NULL);
    memcpy(t, DAY_EVENTS, sizeof t);
    t[EVENT_ARCANE_CONVERGENCE].quality_shift_common = 40;
    t[EVENT_ARCANE_CONVERGENCE].quality_add_masterwork = 40;
    t[EVENT_ARCANE_CONVERGENCE].quality_add_legendary = 0; /* Common (最低 15) を超えて移す */
    CHECK(content_validate_events(t) != NULL);
    memcpy(t, DAY_EVENTS, sizeof t);
    t[EVENT_MARKET_CARAVAN].market_discount_pct = 95;
    CHECK(content_validate_events(t) != NULL);
}

static void test_market_caravan(void) {
    puts("ascendant_caravan");
    for (int sup_lv = 0; sup_lv <= UPGRADES[UP_SUPPLY].max_level; sup_lv++) {
        int sup = upgrade_effect(UP_SUPPLY, sup_lv);
        for (int day = 1; day <= 14; day++) {
            GameState g = at_done(0, day);
            g.up_level[UP_SUPPLY] = sup_lv;
            int ev = event_market_discount_pct(g.day);
            GameState prices = g;
            for (int m = 0; m < MAT_COUNT; m++) {
                if (MATERIALS[m].hunt_only) {
                    CHECK_EQ(market_quote(&g, m, 1), -1); /* hunt_only は買えず、割引の対象にもならない */
                    continue;
                }
                long long p = g.price[m];
                int unit = market_unit_price(&g, m);
                /* 式: Supplier -> イベントの順に掛け合わせて、1 回だけ四捨五入。最低 1 */
                long long ex = (p * (100 - sup) * (100 - ev) + 5000) / 10000;
                if (ex < 1) {
                    ex = 1;
                }
                CHECK_EQ(unit, ex);
                CHECK_EQ(market_quote(&g, m, 1), unit); /* 表示価格 == 見積もり (1 個) */
                for (int q = 1; q <= 9; q += 4) {
                    long long tot = (p * q * (100 - sup) * (100 - ev) + 5000) / 10000;
                    if (tot < q) {
                        tot = q;
                    }
                    CHECK_EQ(market_quote(&g, m, q), tot);
                    /* 購入処理は見積もりと同額を支払う */
                    GameState b = g;
                    int money = b.money;
                    CHECK_EQ(market_buy(&b, m, q), R_OK);
                    CHECK_EQ(money - b.money, tot);
                    CHECK_EQ(b.mat_cost[m], tot);
                    CHECK(b.price[m] == g.price[m] && b.swing_pm[m] == g.swing_pm[m]); /* 価格・変動の履歴は不変 */
                }
                CHECK(ev == 0 || unit <= (int)((p * (100 - sup) + 50) / 100)); /* イベントの日は同じかより安い */
            }
            CHECK(!memcmp(g.price, prices.price, sizeof g.price));
        }
    }
    /* Caravan と Supplier の併用: 両方の割引を掛け合わせて 1 回だけ四捨五入する */
    GameState g = at_done(0, 2);
    g.price[MAT_IRON] = 20;
    CHECK_EQ(market_quote(&g, MAT_IRON, 1), 17); /* Supplier なし: 20 * .85 = 17 */
    g.up_level[UP_SUPPLY] = UPGRADES[UP_SUPPLY].max_level;
    int sup = upgrade_effect(UP_SUPPLY, g.up_level[UP_SUPPLY]);
    CHECK(sup > 0);
    CHECK_EQ(market_quote(&g, MAT_IRON, 1), (20 * (100 - sup) * 85 + 5000) / 10000);
    /* 最終価格は最低 1 ゴールド */
    g.price[MAT_IRON] = 1;
    CHECK(market_quote(&g, MAT_IRON, 1) >= 1);
    CHECK(market_unit_price(&g, MAT_IRON) >= 1);
    CHECK(market_quote(&g, MAT_IRON, 9) >= 9);
    /* 買えない素材の見かけの価格にも、イベント割引はかからない */
    CHECK_EQ(market_unit_price(&g, MAT_ASTRAL_SHARD),
             (MATERIALS[MAT_ASTRAL_SHARD].base_price * (100 - sup) * 100 + 5000) / 10000);
}

static void test_arcane(void) {
    puts("ascendant_arcane");
    int o[QUALITY_COUNT];
    quality_rates_on_day(RANK_APPRENTICE, 0, 4, o); /* 70/25/5/0 -> 63/25/10/2 */
    CHECK(o[0] == 63 && o[1] == 25 && o[2] == 10 && o[3] == 2);
    quality_rates_on_day(RANK_MYTHIC, 0, 4, o); /* 20/33/30/17 -> 13/33/35/19 */
    CHECK(o[0] == 13 && o[1] == 33 && o[2] == 35 && o[3] == 19);
    quality_rates_on_day(RANK_ASCENDANT, 0, 4, o); /* 15/30/35/20 -> 8/30/40/22 */
    CHECK(o[0] == 8 && o[1] == 30 && o[2] == 40 && o[3] == 22);
    quality_rates_on_day(RANK_ASCENDANT, 3, 4, o); /* イベント補正 -> Forge: Common 8 しか残っていないので 8 だけ移る */
    CHECK(o[0] == 0 && o[1] == 38 && o[2] == 40 && o[3] == 22);
    quality_rates_on_day(RANK_APPRENTICE, 2, 4, o); /* 63 -> Forge 10 -> 53 / 35 / 10 / 2 */
    CHECK(o[0] == 53 && o[1] == 35 && o[2] == 10 && o[3] == 2);
    /* 同じ日付・ランク・Forge・ロールなら同じ結果 (決定的) */
    for (int r = 0; r < RANK_COUNT; r++) {
        for (int roll = 0; roll < 100; roll++) {
            CHECK_EQ(quality_from_roll_on_day(r, 1, 11, roll), quality_from_roll_on_day(r, 1, 4, roll));
            CHECK_EQ(quality_from_roll_on_day(r, 1, 11, roll), quality_from_roll_on_day(r, 1, 11, roll));
        }
    }
    /* 乱数の消費回数は通常の日と同じ (ロールは常に 1 回): どの日でも craft_rng は同じ回数だけ進む */
    GameState a = at_done(75, 4), b = at_done(75, 3); /* Arcane Convergence の日と通常の日 */
    a.craft_rng = b.craft_rng = 777;
    a.mat_stock[MAT_IRON] = b.mat_stock[MAT_IRON] = 6;
    CHECK_EQ(craft_equipment(&a, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
    CHECK_EQ(craft_equipment(&b, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
    CHECK(a.craft_rng == b.craft_rng);
    CHECK(a.rng == b.rng && a.guild_rng == b.guild_rng);
    /* 同じロールに対して、イベントの日は品質が下がることはなく、Common が減る */
    int better = 0;
    for (int roll = 0; roll < 100; roll++) {
        int qn = quality_from_roll_on_day(RANK_APPRENTICE, 0, 3, roll),
            qe = quality_from_roll_on_day(RANK_APPRENTICE, 0, 4, roll);
        CHECK(qe >= qn);
        better += qe > qn;
    }
    CHECK_EQ(
        better,
        16); /* 上がるロール: Common->Fine 63..69 (7)、Fine->Masterwork 88..94 (7)、Masterwork->Legendary 98..99 (2) */
}

static void test_guild_assembly(void) {
    puts("ascendant_assembly");
    static const int days[] = {5, 6, 7, 13};
    for (size_t i = 0; i < sizeof days / sizeof *days; i++) {
        int day = days[i], bonus = day_event(day) == EVENT_GUILD_ASSEMBLY;
        GameState g = at_done(10, day);
        put_standard(&g, CT_WARRIOR, 0, GD_STANDARD); /* 55 ゴールド */
        CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
        int idx = craft(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
        int money = g.money, rep = g.rep, sold_gold = g.report.guild_gold, net0 = net_of(&g.report);
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
        CHECK_EQ(o.bonus_gold, bonus ? 6 : 0); /* 55 * 10% = 5.5 -> 6 */
        CHECK_EQ(o.gold, 55 + (bonus ? 6 : 0));
        CHECK_EQ(g.money - money, o.gold);
        CHECK_EQ(g.report.guild_gold - sold_gold, o.gold); /* 日報 */
        CHECK_EQ(g.rep - rep, o.rep_gain);
        CHECK_EQ(o.rep_gain, GUILD_DIFFS[GD_STANDARD].rep); /* 評判は増えない */
        CHECK_EQ(o.profit, o.gold - (o.gold - o.profit)); /* 利益 = 報酬ゴールド (ボーナス込み) - 原価 */
        CHECK(o.profit <= o.gold);
        CHECK_EQ(net_of(&g.report) - net0,
                 o.gold); /* 日報の純利益 (そして精算で total_profit に入る額) にもボーナスが入る */
        CHECK(game_validate(&g));
    }
    /* 判定は納品日: 受注日が Assembly の日 (6) でも、翌日 (7)
     * に納品すれば上乗せはない。逆に前日に受注して当日納品なら付く */
    GameState g = at_done(10, 6);
    put_standard(&g, CT_WARRIOR, 0, GD_EASY);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    int idx = craft(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    pass_day(&g);
    CHECK_EQ(g.day, 7);
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(o.bonus_gold, 0);
    CHECK_EQ(o.gold, GUILD_DIFFS[GD_EASY].gold);
    g = at_done(10, 5);
    put_standard(&g, CT_WARRIOR, 0, GD_EASY);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    idx = craft(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    pass_day(&g);
    CHECK_EQ(g.day, 6);
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(o.bonus_gold, event_guild_bonus(GUILD_DIFFS[GD_EASY].gold, 6)); /* 35 -> 4 */
    CHECK_EQ(o.bonus_gold, 4);

    /* Relic Expedition (260 -> 286) と Dragon Hunt にも適用され、素材の報酬は増えない */
    g = relic_game(6);
    idx = craft(&g, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA);
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(o.gold, 286);
    CHECK_EQ(o.bonus_gold, 26);
    CHECK_EQ(o.rep_gain, 5);
    CHECK_EQ(o.reward_qty, 2);
    CHECK_EQ(g.mat_stock[MAT_ASTRAL_SHARD], 2);
    g = at_done(75, 6);
    g.rep = 10;
    put_special(&g, GUILD_CONTRACT_DRAGON_HUNT);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    idx = craft(&g, EQ_WYRMCLEAVER, MAT_STEEL, MAT_IRON);
    CHECK(guild_item_fit(&g, 0, idx) >= GUILD_DRAGON_HUNT.min_fit);
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK_EQ(o.gold, GUILD_DRAGON_HUNT.gold + event_guild_bonus(GUILD_DRAGON_HUNT.gold, 6));
    CHECK_EQ(o.reward_qty, GUILD_DRAGON_HUNT.reward_qty);
    /* 所持金の上限・飽和は既存の処理 */
    /* 所持金の上限・飽和は既存の処理 (money_add): 上限付近でもボーナスで上限を超えない */
    g = at_done(10, 6);
    put_standard(&g, CT_WARRIOR, 0, GD_EASY);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    idx = craft(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    g.money = DAY_CAP - 3;
    CHECK_EQ(guild_deliver(&g, 0, idx, &o), GUILD_OK);
    CHECK(g.money <= DAY_CAP);
    CHECK(game_validate(&g));
}

/* ---------- 7. 乱数ストリームの独立 ---------- */
static void test_streams(void) {
    puts("ascendant_streams");
    /* イベントの日にも、市場・クラフト・ギルドの 3 本は互いに触れない */
    for (int day = 1; day <= 8; day++) {
        GameState g = at_done(75, day);
        g.mat_stock[MAT_MOONSTONE] = 6;
        g.mat_stock[MAT_MANA] = 6;
        GameState b = g;
        CHECK_EQ(market_buy(&b, MAT_MITHRIL, 3), R_OK);
        CHECK(b.rng == g.rng && b.guild_rng == g.guild_rng && b.craft_rng == g.craft_rng);
        b = g;
        CHECK_EQ(craft_equipment(&b, EQ_VOID_CODEX, MAT_MOONSTONE, MAT_MANA, NULL), R_OK);
        CHECK(b.rng == g.rng && b.guild_rng == g.guild_rng);
        CHECK(b.craft_rng != g.craft_rng);
        b = g;
        market_update(&b);
        CHECK(b.guild_rng == g.guild_rng && b.craft_rng == g.craft_rng);
        b = g;
        put_standard(&b, CT_WARRIOR, 0, GD_EASY);
        CHECK_EQ(guild_accept(&b, 0), GUILD_OK);
        CHECK(b.rng == g.rng && b.guild_rng == g.guild_rng && b.craft_rng == g.craft_rng);
        b = g;
        CHECK_EQ(day_open(&b), R_OK);
        CHECK(b.guild_rng == g.guild_rng && b.craft_rng == g.craft_rng);
    }
    /* 同じ入力 -> 同じ結果 */
    GameState a = at_done(75, 4), b = at_done(75, 4);
    for (int i = 0; i < 20; i++) {
        pass_day(&a);
        pass_day(&b);
    }
    CHECK(same_state(&a, &b));
}

/* ---------- 8. 保存 ---------- */
static void test_save(void) {
    puts("ascendant_save");
    CHECK_EQ(SAVE_VERSION, 4); /* 保存形式は変えていない */
    GameState g = relic_game(4);
    g.mat_stock[MAT_ASTRAL_SHARD] = 5;
    g.mat_stock[MAT_MITHRIL] = 4;
    craft(&g, EQ_VOID_CODEX, MAT_ASTRAL_SHARD, MAT_MOONSTONE);
    craft(&g, EQ_ASTRAL_BLADE, MAT_MITHRIL, MAT_STEEL);
    craft(&g, EQ_AEGIS_OF_DAWN, MAT_EMBERGLASS, MAT_IRON);
    put_special(&g, GUILD_CONTRACT_DRAGON_HUNT); /* 掲示中の Dragon Hunt + 受注中の Relic Expedition */
    CHECK(game_validate(&g));
    char *txt = save_text(&g);
    CHECK(!strncmp(txt, "LOOTSMITH_SAVE 4\n", 17));
    CHECK(strstr(txt, "event") == NULL && strstr(txt, "rank") == NULL &&
          strstr(txt, "unlock") == NULL); /* 導出するものは保存しない */
    GameState l;
    CHECK_EQ(load_text(&l, txt), SAVE_OK);
    CHECK(same_state(&g, &l)); /* 特殊素材を含む在庫・装備・掲示中と受注中の特別契約が保存・復元される */
    CHECK_EQ(l.guild_active[0].kind, GUILD_CONTRACT_RELIC_EXPEDITION);
    CHECK_EQ(l.guild_board[0].kind, GUILD_CONTRACT_DRAGON_HUNT);
    CHECK_EQ(guild_rank(l.guild_done), RANK_ASCENDANT);
    CHECK(blueprint_unlocked(l.guild_done, BP_VOID_CODEX));
    CHECK_EQ(day_event(l.day), day_event(g.day)); /* イベントは日付から再計算される */
    CHECK_EQ(l.mat_stock[MAT_ASTRAL_SHARD], g.mat_stock[MAT_ASTRAL_SHARD]);
    free(txt);

    /* 旧ビルド (Ascendant Era より前: 素材 8 つ・装備 10・客 11) のセーブ:
     * 新しい素材は既定値で初期化され、ランクは導出される */
    GameState o = at_done(80, 9);
    craft(&o, EQ_SWORD, MAT_IRON, MAT_IRON);
    char *t = save_text(&o);
    char *a = edit_line(t, "counts ", "counts 8 10 11 3");
    char *b = a;
    for (int m = 8; m < MAT_COUNT; m++) {
        char key[16];
        snprintf(key, sizeof key, "mat %d ", m);
        char *c = edit_line(b, key, NULL);
        free(b);
        b = c;
    }
    GameState old;
    CHECK_EQ(load_text(&old, b), SAVE_OK);
    CHECK_EQ(guild_rank(old.guild_done), RANK_ASCENDANT);
    CHECK_EQ(old.inv[0].type, EQ_SWORD);
    for (int m = 8; m < MAT_COUNT; m++) {
        CHECK_EQ(old.mat_stock[m], 0);
        CHECK_EQ(old.price[m], MATERIALS[m].base_price);
        CHECK_EQ(old.swing_pm[m], 0);
    }
    CHECK(game_validate(&old));
    free(b);
    free(t);
    /* 旧ビルドのセーブは、旧ビルドが知らない客・装備を持てない (状態は不変) */
    {
        GameState bad = at_done(80, 9);
        put_standard(&bad, CT_DUELIST, 40, GD_EASY);
        bad.guild_board_count = 1;
        char *tx = save_text(&bad);
        char *oc = edit_line(tx, "counts ", "counts 8 10 11 3");
        GameState keep = at_done(3, 1), probe = keep;
        CHECK(load_text(&probe, oc) != SAVE_OK);
        CHECK(same_state(&probe, &keep));
        free(oc);
        free(tx);
    }

    /* 不正な契約の種別・報酬素材・報酬数・依頼主・解放条件を持つセーブは拒否され、読み込み先は変わらない */
    GameState base = relic_game(4);
    base.guild_active_count = 0;
    memset(base.guild_active, 0, sizeof base.guild_active);
    put_special(&base, GUILD_CONTRACT_RELIC_EXPEDITION);
    CHECK(game_validate(&base));
    GameState bad = base;
    bad.guild_board[0].kind = GUILD_CONTRACT_KIND_COUNT;
    expect_rejected(&bad, "kind out of range");
    bad = base;
    bad.guild_board[0].kind = -1;
    expect_rejected(&bad, "negative kind");
    bad = base;
    bad.guild_board[0].reward_mat = MAT_IRON; /* 市場で買える素材は報酬にできない */
    expect_rejected(&bad, "non hunt-only reward");
    bad = base;
    bad.guild_board[0].reward_mat = MAT_COUNT;
    expect_rejected(&bad, "reward material out of range");
    bad = base;
    bad.guild_board[0].reward_qty = 0;
    expect_rejected(&bad, "reward qty 0");
    bad = base;
    bad.guild_board[0].reward_qty = BAL.stock_cap + 1;
    expect_rejected(&bad, "reward qty over cap");
    bad = base;
    bad.guild_board[0].customer_type = CT_KNIGHT; /* 依頼主は Rift Seeker 固定 */
    expect_rejected(&bad, "wrong client");
    bad = base;
    bad.guild_done = 74; /* Ascendant Smith 未満では Relic Expedition は存在しえない */
    expect_rejected(&bad, "relic below rank");
    bad = base;
    bad.guild_board[1] = base.guild_board[0]; /* 特別契約が掲示板に 2 件 */
    bad.guild_board[1].id = base.guild_board[0].id + 1;
    bad.guild_board[1].customer_type = CT_WYRM_HUNTER;
    bad.guild_board[1].kind = GUILD_CONTRACT_DRAGON_HUNT;
    bad.guild_board[1].reward_mat = MAT_DRAGON_SCALE;
    bad.guild_board_count = 2;
    expect_rejected(&bad, "two specials");
    bad = base;
    bad.guild_board[0].kind = GUILD_CONTRACT_STANDARD; /* 通常契約が報酬を持つ */
    expect_rejected(&bad, "standard with reward");
}

/* ---------- 9. 通常キャンペーンへの影響 ---------- */
static int play_blind(uint32_t seed, int use_events_flag) {
    (void)use_events_flag;
    GameState g;
    game_init(&g, seed);
    for (int d = 0; d < 6 && g.status == ST_PLAYING; d++) {
        pass_day(&g);
    }
    return g.guild_done == 0 && g.guild_board_count == GUILD_OFFER_COUNT && game_validate(&g);
}

static void test_campaign_safe(void) {
    puts("ascendant_campaign_safe");
    for (uint32_t s = 1; s <= 200; s++) {
        CHECK(play_blind(s, 0));
        GameState g;
        game_init(&g, s);
        /* 通常キャンペーン (guild_done < 50) の掲示板に、未解放の客も特別契約も決して現れない */
        for (int d = 0; d < 30; d++) {
            for (int i = 0; i < g.guild_board_count; i++) {
                CHECK(g.guild_board[i].kind == GUILD_CONTRACT_STANDARD);
                CHECK(customer_unlocked(g.guild_done, g.guild_board[i].customer_type));
            }
            for (int i = 0; i < g.queue_count; i++) {
                CHECK(customer_unlocked(g.guild_done, g.queue[i].type));
            }
            day_open(&g);
            for (int i = 0; i < g.queue_count; i++) {
                CHECK(customer_unlocked(g.guild_done, g.queue[i].type));
            }
            day_close_early(&g);
            if (g.status != ST_PLAYING) {
                break;
            }
            day_advance(&g);
        }
    }
}

int main(void) {
    test_rank();
    test_quality_odds();
    test_content();
    test_materials_market();
    test_astral_use();
    test_special_roll();
    test_board_refresh();
    test_relic_deliver();
    test_relic_expiry();
    test_accept_limits();
    test_event_calendar();
    test_market_caravan();
    test_arcane();
    test_guild_assembly();
    test_streams();
    test_save();
    test_campaign_safe();
    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
