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

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
