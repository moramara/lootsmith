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

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
