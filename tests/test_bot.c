#include "bot/bot.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(expr)                                                                                                   \
    do {                                                                                                              \
        checks++;                                                                                                     \
        if (!(expr)) {                                                                                                \
            failures++;                                                                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);                                        \
        }                                                                                                             \
    } while (0)

static void run_seed(uint32_t seed, BotRunner *out) {
    BotRunner bot;
    bot_init(&bot, seed);
    for (int step = 0; step < 5000 && !bot.finished; ++step) {
        bot_step(&bot);
        CHECK(game_validate(&bot.game));
    }
    CHECK(bot.finished);
    CHECK(bot.game.status == ST_CLEARED || bot.game.status == ST_FAILED);
    CHECK(bot.log_count > 0);
    *out = bot;
    printf("seed %u: %s on day %d; gold %d, reputation %d, guild %d\n", seed,
           bot.game.status == ST_CLEARED ? "cleared" : "failed", bot.game.day, bot.game.money, bot.game.rep,
           bot.game.guild_done);
}

/* Mythic Smith 到達後のサンドボックス: BOT は公開 API だけで Dragon Hunt を扱う。受け取れない報酬の契約や、
 * 最低 FIT を満たせない契約は受注せず、状態の不変条件はいつも成り立つ。 */
static void sandbox_run(uint32_t seed, BotRunner *out, int *hunts, int *max_scales, int *bad_accept) {
    bot_init(out, seed);
    GameState *g = &out->game;
    g->guild_done = 50;
    g->status = ST_SANDBOX;
    g->money = 3000;
    g->rep = 50;
    guild_refresh_board(g);
    *hunts = 0;
    *max_scales = 0;
    *bad_accept = 0;
    for (int step = 0; step < 4000 && g->day < 60 && !out->finished; ++step) {
        int done_before = g->guild_done, scales_before = g->mat_stock[MAT_DRAGON_SCALE];
        int active_hunts_before = 0; /* 1 ステップ (準備) で、受注から納品まで進みうる: 掲示中のものも数える */
        for (int i = 0; i < g->guild_active_count; ++i) {
            active_hunts_before += g->guild_active[i].kind == GUILD_CONTRACT_DRAGON_HUNT;
        }
        for (int i = 0; i < g->guild_board_count; ++i) {
            active_hunts_before += g->guild_board[i].kind == GUILD_CONTRACT_DRAGON_HUNT;
        }
        bot_step(out);
        CHECK(game_validate(g));
        CHECK(g->mat_stock[MAT_DRAGON_SCALE] <= BAL.stock_cap);
        if (g->mat_stock[MAT_DRAGON_SCALE] > *max_scales) {
            *max_scales = g->mat_stock[MAT_DRAGON_SCALE];
        }
        int gained = g->mat_stock[MAT_DRAGON_SCALE] - scales_before;
        /* 報酬は Dragon Hunt の納品でだけ増える (BOT は消費しない) */
        CHECK(gained >= 0 && gained <= active_hunts_before);
        *hunts += gained;
        if (gained > 0) {
            CHECK(g->guild_done > done_before); /* 納品なしに素材は増えない */
        }
        for (int i = 0; i < g->guild_active_count; ++i) {
            const GuildContract *k = &g->guild_active[i];
            if (k->kind == GUILD_CONTRACT_DRAGON_HUNT && k->customer_type != CT_WYRM_HUNTER) {
                (*bad_accept)++;
            }
        }
    }
}


/* The Ascendant Era: guild_done = start から始めるサンドボックス (ST_SANDBOX)。BOT は公開 API だけで、新ランク・新しい客・
 * Relic Expedition を扱う。BOT だけの特別なルールはない。 */
typedef struct {
    int relics, shards, max_shards, bad_accept, new_gear_crafted, reached, new_customers_seen, bought_hunt_only;
} AscStats;

static void ascendant_run(uint32_t seed, int start, int days, BotRunner *out, AscStats *st) {
    memset(st, 0, sizeof *st);
    bot_init(out, seed);
    GameState *g = &out->game;
    g->guild_done = start;
    g->status = ST_SANDBOX;
    g->money = 4000;
    g->rep = 50;
    guild_refresh_board(g);
    for (int step = 0; step < 6000 && g->day < days && !out->finished; ++step) {
        int shards_before = g->mat_stock[MAT_ASTRAL_SHARD], done_before = g->guild_done;
        int dragon_stock = g->mat_stock[MAT_DRAGON_SCALE], money_before = g->money;
        int relics_open = 0; /* 1 ステップで、受注から納品まで進みうる: 掲示中・受注中のものも数える */
        for (int i = 0; i < g->guild_active_count; ++i) {
            relics_open += g->guild_active[i].kind == GUILD_CONTRACT_RELIC_EXPEDITION;
        }
        for (int i = 0; i < g->guild_board_count; ++i) {
            relics_open += g->guild_board[i].kind == GUILD_CONTRACT_RELIC_EXPEDITION;
        }
        bot_step(out);
        CHECK(game_validate(g));
        CHECK(g->mat_stock[MAT_ASTRAL_SHARD] <= BAL.stock_cap);
        int gained = g->mat_stock[MAT_ASTRAL_SHARD] - shards_before;
        /* Astral Shard は Relic Expedition の納品でだけ増える (市場では買えず、BOT は消費しない)。原価は 0 */
        CHECK(gained >= 0 && gained <= relics_open * GUILD_RELIC_EXPEDITION.reward_qty);
        if (gained > 0) {
            CHECK(g->guild_done > done_before);
            CHECK(g->mat_cost[MAT_ASTRAL_SHARD] == 0);
        }
        st->relics += gained / GUILD_RELIC_EXPEDITION.reward_qty;
        st->shards += gained;
        if (g->mat_stock[MAT_ASTRAL_SHARD] > st->max_shards) {
            st->max_shards = g->mat_stock[MAT_ASTRAL_SHARD];
        }
        (void)dragon_stock;
        (void)money_before;
        for (int i = 0; i < g->guild_active_count; ++i) {
            const GuildContract *k = &g->guild_active[i];
            if (k->kind == GUILD_CONTRACT_RELIC_EXPEDITION &&
                (k->customer_type != CT_RIFT_SEEKER || k->min_fit != 72 ||
                 CUSTOMER_DEFS[k->customer_type].wants_eq != EQ_VOID_CODEX)) {
                st->bad_accept++;
            }
        }
        for (int i = 0; i < g->inv_count; ++i) {
            st->new_gear_crafted += g->inv[i].type == EQ_ASTRAL_BLADE || g->inv[i].type == EQ_AEGIS_OF_DAWN ||
                                    g->inv[i].type == EQ_VOID_CODEX;
        }
        for (int i = 0; i < g->queue_count; ++i) {
            st->new_customers_seen += g->queue[i].type == CT_DUELIST || g->queue[i].type == CT_SENTINEL ||
                                      g->queue[i].type == CT_RIFT_SEEKER;
        }
    }
    st->reached = g->guild_done >= 75;
}

int main(void) {
    puts("bot watch / determinism and state validation");
    BotRunner a, b;
    run_seed(1, &a);
    run_seed(42, &b);

    int batch_clears = 1; /* seed 1 は上で検証済み */
    for (uint32_t seed = 2; seed <= 40; ++seed) {
        BotRunner batch;
        bot_init(&batch, seed);
        for (int step = 0; step < 5000 && !batch.finished; ++step) {
            bot_step(&batch);
            CHECK(game_validate(&batch.game));
        }
        CHECK(batch.finished);
        CHECK(batch.game.status == ST_CLEARED);
        if (batch.game.status == ST_CLEARED) {
            batch_clears++;
        }
    }
    CHECK(batch_clears == 40);
    printf("seed sweep: %d/40 campaigns cleared\n", batch_clears);

    BotRunner same;
    bot_init(&same, 1);
    BotRunner first;
    bot_init(&first, 1);
    for (int step = 0; step < 5000 && !first.finished; ++step) {
        bot_step(&first);
        bot_step(&same);
        CHECK(memcmp(&first.game, &same.game, sizeof first.game) == 0);
        CHECK(first.finished == same.finished);
        CHECK(first.log_count == same.log_count);
        for (int i = 0; i < first.log_count; ++i) {
            CHECK(strcmp(first.log[i], same.log[i]) == 0);
        }
    }
    CHECK(first.finished);
    CHECK(first.game.status == a.game.status);
    CHECK(first.game.day == a.game.day);
    CHECK(first.game.money == a.game.money);
    CHECK(first.game.guild_done == a.game.guild_done);

    { /* Dragon Hunt: 複数のシードで、納品に成功した分だけ素材が増え、在庫上限を超えない */
        int total_hunts = 0, hit_cap = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            BotRunner r1, r2;
            int h1, m1, b1, h2, m2, b2;
            sandbox_run(seed, &r1, &h1, &m1, &b1);
            sandbox_run(seed, &r2, &h2, &m2, &b2); /* 同じシードなら完全に同じ結果 */
            CHECK(memcmp(&r1.game, &r2.game, sizeof r1.game) == 0);
            CHECK(h1 == h2 && m1 == m2);
            CHECK(b1 == 0);
            total_hunts += h1;
            hit_cap += m1 >= BAL.stock_cap;
        }
        CHECK(total_hunts > 0); /* BOT は実際に Dragon Hunt を納品する */
        printf("sandbox sweep: %d Dragon Scale earned over 12 seeds (60 days each)\n", total_hunts);
        /* 在庫枠がない状態では受注も納品もしない: 満杯の状態で何日進めても上限を超えない */
        BotRunner full;
        bot_init(&full, 7);
        full.game.guild_done = 50;
        full.game.status = ST_SANDBOX;
        full.game.money = 3000;
        full.game.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap;
        for (int step = 0; step < 4000 && full.game.day < 40 && !full.finished; ++step) {
            bot_step(&full);
            CHECK(game_validate(&full.game));
            CHECK(full.game.mat_stock[MAT_DRAGON_SCALE] == BAL.stock_cap);
            for (int i = 0; i < full.game.guild_active_count; ++i) {
                /* 報酬を受け取れない契約は受注しない */
                CHECK(full.game.guild_active[i].kind != GUILD_CONTRACT_DRAGON_HUNT);
            }
        }
        (void)hit_cap;
    }

    { /* Dragonforging: BOT が Dragon Hunt を遊んで到達した状態 (Mythic Smith) から、公開 API だけで Dragon Scale
         を使ってクラフトする */
        static const int recipes[3][2] = {{MAT_IRON, MAT_DRAGON_SCALE},
                                          {MAT_DRAGON_SCALE, MAT_IRON},
                                          {MAT_DRAGON_SCALE, MAT_DRAGON_SCALE}}; /* 使用数 1 / 2 / 3 */
        long trials[3] = {0}, dragon[3] = {0}, hq[3] = {0};
        long standard_trait_plain = 0, plain_hq = 0, dragon_cheap = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            BotRunner r;
            int h, m, b;
            sandbox_run(seed, &r, &h, &m, &b);
            for (int u = 0; u < 3; ++u) {
                for (int n = 0; n < 400; ++n) {
                    GameState g = r.game; /* BOT の最終状態のコピー。Forge 等はその状態のまま */
                    g.status = ST_SANDBOX;
                    g.craft_rng = (uint32_t)(seed * 7919u + (uint32_t)(u * 1000003 + n * 104729 + 1));
                    g.mat_stock[MAT_DRAGON_SCALE] = 3;
                    g.mat_cost[MAT_DRAGON_SCALE] = 0;
                    g.mat_stock[MAT_IRON] = 3;
                    g.mat_cost[MAT_IRON] = 30;
                    uint32_t rng0 = g.rng, guild0 = g.guild_rng;
                    int idx = -1;
                    CHECK(craft_equipment(&g, EQ_WYRMCLEAVER, recipes[u][0], recipes[u][1], &idx) == R_OK);
                    const Equipment *e = &g.inv[idx];
                    CHECK(g.rng == rng0 && g.guild_rng == guild0); /* 市場・客・契約の乱数には触れない */
                    CHECK(game_validate(&g));
                    trials[u]++;
                    if (!QUALITY_DEFS[e->quality].has_trait) {
                        CHECK(e->trait == EQ_TRAIT_NONE);
                        continue;
                    }
                    hq[u]++;
                    if (e->trait >= EQ_TRAIT_STANDARD_COUNT) {
                        dragon[u]++;
                        /* FIT と推奨価格は、特性を含んだ最終能力から求まる */
                        Equipment without = *e;
                        if (e->trait == EQ_TRAIT_DRACONIC_EDGE) {
                            without.atk -= TRAIT_DEFS[e->trait].amount[e->quality];
                        } else {
                            without.dur -= TRAIT_DEFS[e->trait].amount[e->quality];
                        }
                        equipment_price(&without);
                        CHECK(equip_fit(CT_WYRM_HUNTER, e) >= equip_fit(CT_WYRM_HUNTER, &without));
                        CHECK(e->rec_price >= without.rec_price);
                        CHECK(e->rec_price == e->price);
                        dragon_cheap += e->rec_price < 60; /* 高品質装備が極端に安価にならない */
                    }
                }
            }
            /* Dragon Scale を使わないクラフトでは、新特性は決して出ない */
            for (int n = 0; n < 300; ++n) {
                GameState g = r.game;
                g.status = ST_SANDBOX;
                g.craft_rng = (uint32_t)(seed * 104729u + (uint32_t)n + 17);
                g.mat_stock[MAT_STEEL] = 3;
                g.mat_cost[MAT_STEEL] = 36;
                int idx = -1;
                CHECK(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL, &idx) == R_OK);
                if (QUALITY_DEFS[g.inv[idx].quality].has_trait) {
                    plain_hq++;
                    standard_trait_plain +=
                        g.inv[idx].trait > EQ_TRAIT_NONE && g.inv[idx].trait < EQ_TRAIT_STANDARD_COUNT;
                }
            }
        }
        static const int pct[3] = {20, 30, 35};
        for (int u = 0; u < 3; ++u) {
            double f = 100.0 * (double)dragon[u] / (double)hq[u];
            printf("dragonforging sweep: %d Dragon Scale -> %.1f%% dragon trait over %ld Masterwork/Legendary (target "
                   "%d%%)\n",
                   u + 1, f, hq[u], pct[u]);
            CHECK(dragon[u] > 0);                        /* 特殊特性が実際に発生する */
            CHECK(f > pct[u] - 4.0 && f < pct[u] + 4.0); /* 設定した確率が反映される (標本誤差の範囲) */
        }
        CHECK(dragon[0] < dragon[2] * 1.0 &&
              (double)dragon[0] / (double)hq[0] < (double)dragon[2] / (double)hq[2]); /* 多いほど高い */
        CHECK(plain_hq > 0 && standard_trait_plain == plain_hq); /* 通常素材だけ: 特性は必ず通常特性 */
        CHECK(dragon_cheap == 0);
    }


    { /* The Ascendant Era: 70 件から (Mythic Smith) 進めて Ascendant Smith に到達するシナリオと、到達後 (75 件から) のシナリオ */
        int reached = 0, relics = 0, bad = 0, gear = 0, seen = 0, runs = 0, cap_hit = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            BotRunner r1, r2;
            AscStats s1, s2;
            ascendant_run(seed, 70, 50, &r1, &s1);
            ascendant_run(seed, 70, 50, &r2, &s2);
            CHECK(memcmp(&r1.game, &r2.game, sizeof r1.game) == 0); /* 同じシードなら完全に同じ (新装備・素材・イベントを含めても) */
            CHECK(memcmp(&s1, &s2, sizeof s1) == 0);
            runs++;
            reached += s1.reached;
            relics += s1.relics;
            bad += s1.bad_accept;
            gear += s1.new_gear_crafted > 0;
            seen += s1.new_customers_seen > 0;
            cap_hit += s1.max_shards >= BAL.stock_cap;
            if (s1.reached) {
                CHECK(guild_rank(r1.game.guild_done) == RANK_ASCENDANT);
            }
        }
        printf("ascendant sweep (from 70): %d/%d reached Ascendant Smith, %d Relic Expeditions, %d runs crafted new gear, "
               "%d runs saw the new customers\n",
               reached, runs, relics, gear, seen);
        CHECK(reached == runs); /* 70 件から 50 日あれば到達できる */
        CHECK(bad == 0);
        CHECK(seen > 0);    /* 解放後に、新しい客が来る */
        CHECK(relics > 0);  /* BOT は実際に Relic Expedition を納品し、Astral Shard を得る */
        (void)cap_hit;

        int relics75 = 0, max_shards = 0;
        for (uint32_t seed = 1; seed <= 12; ++seed) {
            BotRunner r;
            AscStats s;
            ascendant_run(seed, 75, 60, &r, &s);
            CHECK(s.bad_accept == 0);
            relics75 += s.relics;
            if (s.max_shards > max_shards) {
                max_shards = s.max_shards;
            }
            CHECK(guild_rank(r.game.guild_done) == RANK_ASCENDANT);
        }
        printf("ascendant sandbox (from 75, 60 days): %d Relic Expeditions over 12 seeds, max Astral Shard stock %d\n",
               relics75, max_shards);
        CHECK(relics75 > 0);

        /* Astral Shard の在庫が満杯 (報酬を受け取れない) なら、Relic Expedition は受注も納品もしない (失敗を繰り返さない) */
        BotRunner full;
        bot_init(&full, 7);
        full.game.guild_done = 75;
        full.game.status = ST_SANDBOX;
        full.game.money = 4000;
        full.game.rep = 50;
        full.game.mat_stock[MAT_ASTRAL_SHARD] = BAL.stock_cap;
        for (int step = 0; step < 4000 && full.game.day < 40 && !full.finished; ++step) {
            bot_step(&full);
            CHECK(game_validate(&full.game));
            CHECK(full.game.mat_stock[MAT_ASTRAL_SHARD] == BAL.stock_cap);
            for (int i = 0; i < full.game.guild_active_count; ++i) {
                CHECK(full.game.guild_active[i].kind != GUILD_CONTRACT_RELIC_EXPEDITION);
            }
        }
        /* hunt-only 素材を市場で買わない: 買えない素材の在庫は報酬でしか増えない (上の sweep で検証済み)。ここでは購入が拒否されること */
        CHECK(market_buy(&full.game, MAT_ASTRAL_SHARD, 1) == R_ERR_NOT_FOR_SALE);
        CHECK(market_buy(&full.game, MAT_DRAGON_SCALE, 1) == R_ERR_NOT_FOR_SALE);
    }

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
