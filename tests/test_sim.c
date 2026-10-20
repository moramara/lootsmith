/* LOOT SMITH のロジックテスト。raylib は不要:  make test */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../src/core/simulation.h"
#include "../src/core/icons.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <unistd.h>
#undef R_OK /* unistd.h は R_OK をマクロ (アクセスモード) として定義している。ここでは同名の Result enum を使う */
#endif

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

static GameState fresh(void) {
    GameState g;
    game_init(&g, 12345);
    return g;
}
static int same_state(const GameState *a, const GameState *b) {
    return memcmp(a, b, sizeof *a) == 0;
}

/* 以下のクラフトのテストは、無印のアイテムを前提に書かれた。現在はクラフトのたびに品質が抽選されるので、基本能力の
 * 計算式を検証するテストは、抽選を固定する: craft_rng を、次の抽選が `quality` になる状態に設定する
 * (ゲームが使うのと同じ純粋関数で 探索する)。期待値そのものは変更していない。 */
static void pin_craft(GameState *g, int quality) {
    int rank = guild_rank(g->guild_done), forge = g->up_level[UP_FORGE];
    for (uint32_t s = 1; s < 200000; s++) {
        uint32_t t = s;
        if (quality_from_roll(rank, forge, rng_range(&t, 0, 99)) == quality) {
            g->craft_rng = s;
            return;
        }
    }
    printf("  FAIL pin_craft: quality %d cannot be rolled at rank %d\n", quality, rank);
    g_fails++;
}

/* ---- 決定的なシナリオを作る補助関数 ---- */
static Customer mk_customer(int type, int budget, int min_fit, int tol) {
    Customer c = {type, budget, min_fit, tol, BAL.customer_patience};
    return c;
}
static Equipment mk_item(GameState *g, int eq, int main, int sub) {
    GameState tmp = *g;
    int idx;
    tmp.mat_stock[main] += 3;
    tmp.mat_stock[sub] += 3;
    tmp.mat_cost[main] += 30;
    tmp.mat_cost[sub] += 30;
    pin_craft(&tmp, QUALITY_COMMON);
    craft_equipment(&tmp, eq, main, sub, &idx);
    return tmp.inv[idx];
}
static void force_open(GameState *g, Customer c) {
    day_open(g);
    for (int i = 0; i < g->queue_count; i++) {
        g->queue[i] = c;
    }
}

/* ---- 1 ---- */
static void test_init(void) {
    puts("init");
    GameState g = fresh();
    CHECK_EQ(g.day, 1);
    CHECK_EQ(g.money, BAL.start_money);
    CHECK_EQ(g.rep, BAL.start_rep);
    CHECK_EQ(g.phase, PH_PREP);
    CHECK_EQ(g.status, ST_PLAYING);
    CHECK_EQ(g.inv_count, 0);
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(g.mat_stock[m], 0);
        CHECK(g.price[m] >= MATERIALS[m].min_price);
        CHECK(g.price[m] <= MATERIALS[m].base_price * 1150 / 1000 + 1);
    }
    CHECK_EQ(customers_per_day(&g), 3);
    CHECK(game_validate(&g));
}
/* ---- 2,3 ---- */
static void test_market_buy(void) {
    puts("market_buy");
    GameState g = fresh();
    int cost = market_quote(&g, MAT_IRON, 4), m0 = g.money;
    CHECK(cost > 0);
    CHECK_EQ(market_buy(&g, MAT_IRON, 4), R_OK);
    CHECK_EQ(g.money, m0 - cost);
    CHECK_EQ(g.mat_stock[MAT_IRON], 4);
    CHECK_EQ(g.mat_cost[MAT_IRON], cost);

    GameState before = g;
    CHECK_EQ(market_buy(&g, MAT_IRON, 0), R_ERR_RANGE);
    CHECK_EQ(market_buy(&g, MAT_IRON, -3), R_ERR_RANGE);
    CHECK_EQ(market_buy(&g, MAT_COUNT, 1), R_ERR_RANGE); /* 最初の範囲外 ID (7 は Dragon Scale になった) */
    CHECK_EQ(market_buy(&g, MAT_IRON, 100000), R_ERR_RANGE);
    CHECK_EQ(market_buy(&g, MAT_IRON, 50), R_ERR_FUNDS); /* 約 500 > 所持金 */
    CHECK(same_state(&g, &before));
    g.money = 100000;
    CHECK_EQ(market_buy(&g, MAT_WOOD, BAL.stock_cap), R_OK);
    GameState b2 = g;
    CHECK_EQ(market_buy(&g, MAT_WOOD, 1), R_ERR_FULL);
    CHECK(same_state(&g, &b2));
    /* フェーズが違う */
    g = fresh();
    g.phase = PH_OPEN;
    g.queue_count = 1;
    CHECK_EQ(market_buy(&g, MAT_IRON, 1), R_ERR_PHASE);
}
/* ---- 4,5 ---- */
static void test_craft(void) {
    puts("craft");
    GameState g = fresh();
    g.money = 1000;
    market_buy(&g, MAT_IRON, 3);
    market_buy(&g, MAT_WOOD, 1);
    int stock_cost = g.mat_cost[MAT_IRON] + g.mat_cost[MAT_WOOD];
    int idx = -1;
    pin_craft(&g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_WOOD, &idx), R_OK);
    CHECK_EQ(idx, 0);
    CHECK_EQ(g.inv_count, 1);
    CHECK_EQ(g.mat_stock[MAT_IRON], 1);
    CHECK_EQ(g.mat_stock[MAT_WOOD], 0);
    Equipment *e = &g.inv[0];
    CHECK_EQ(e->type, EQ_SWORD);
    CHECK_EQ(e->atk, 10 + (2 * 8 + 2) / 3 + 0); /* 10 + 6 = 16 */
    CHECK(e->atk > e->def);
    CHECK(e->wt >= 1);
    CHECK_EQ(e->cost + g.mat_cost[MAT_IRON] + g.mat_cost[MAT_WOOD], stock_cost); /* 原価が正確に計上される */
    CHECK(e->rec_price >= e->cost);
    CHECK_EQ(e->price, e->rec_price);
    CHECK(game_validate(&g));

    /* 素材の選択が結果を左右する */
    Equipment a = mk_item(&g, EQ_STAFF, MAT_MANA, MAT_WOOD), b = mk_item(&g, EQ_STAFF, MAT_IRON, MAT_IRON);
    CHECK(a.mag > b.mag);
    Equipment w = mk_item(&g, EQ_SWORD, MAT_WOOD, MAT_WOOD), i = mk_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK(w.wt < i.wt);
    CHECK(i.atk > w.atk);
    Equipment s = mk_item(&g, EQ_SHIELD, MAT_IRON, MAT_IRON);
    CHECK(s.def > i.def);
    CHECK(s.dur > w.dur);

    /* 同じ素材を 2 回使うには 3 個必要 */
    g = fresh();
    g.money = 1000;
    market_buy(&g, MAT_IRON, 2);
    GameState before = g;
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_MATERIAL);
    CHECK(same_state(&g, &before));
    market_buy(&g, MAT_IRON, 1);
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
    CHECK_EQ(g.mat_stock[MAT_IRON], 0);
    CHECK_EQ(g.mat_cost[MAT_IRON], 0);

    /* 副素材の不足、不正な ID、在庫が満杯、鍛冶場アップグレード */
    g = fresh();
    g.money = 1000;
    market_buy(&g, MAT_IRON, 2);
    before = g;
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_WOOD, NULL), R_ERR_MATERIAL);
    CHECK_EQ(craft_equipment(&g, EQ_COUNT, MAT_IRON, MAT_WOOD, NULL), R_ERR_RANGE);
    CHECK(same_state(&g, &before));
    g = fresh();
    g.money = 100000;
    market_buy(&g, MAT_IRON, 99);
    for (int n = 0; n < MAX_INVENTORY; n++) {
        CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
    }
    before = g;
    CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_FULL);
    CHECK(same_state(&g, &before));
    GameState f0 = fresh();
    GameState f2 = fresh();
    f2.up_level[UP_FORGE] = 2;
    Equipment e0 = mk_item(&f0, EQ_SWORD, MAT_IRON, MAT_IRON), e2 = mk_item(&f2, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK(e2.atk > e0.atk);
    CHECK(e2.rec_price > e0.rec_price);
    CHECK_EQ(e2.wt, e0.wt);
}
/* ---- 6 ---- */
static void test_pricing(void) {
    puts("pricing");
    GameState g = fresh();
    g.money = 1000;
    market_buy(&g, MAT_IRON, 3);
    pin_craft(&g, QUALITY_COMMON);
    craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
    int rec = g.inv[0].rec_price;
    CHECK_EQ(inv_adjust_price(&g, 0, 10), R_OK);
    CHECK_EQ(g.inv[0].price, rec + 10);
    CHECK_EQ(g.inv[0].rec_price, rec);
    CHECK_EQ(inv_adjust_price(&g, 0, -100000), R_OK);
    CHECK_EQ(g.inv[0].price, 1); /* 丸められた */
    CHECK_EQ(inv_set_price(&g, 0, 0), R_ERR_PRICE);
    CHECK_EQ(inv_set_price(&g, 0, BAL.price_cap + 1), R_ERR_PRICE);
    CHECK_EQ(g.inv[0].price, 1);
    CHECK_EQ(inv_set_price(&g, 0, 77), R_OK);
    CHECK_EQ(g.inv[0].price, 77);
    CHECK_EQ(inv_reset_price(&g, 0), R_OK);
    CHECK_EQ(g.inv[0].price, rec);
    CHECK_EQ(inv_set_price(&g, 3, 10), R_ERR_NO_ITEM);
    /* 推奨価格は原価とともに上がる */
    Equipment cheap = g.inv[0], dear = g.inv[0];
    dear.cost += 40;
    equipment_price(&dear);
    CHECK(dear.rec_price > cheap.rec_price);
    /* 換金 */
    int m0 = g.money, v = 0;
    CHECK_EQ(inv_liquidate(&g, 0, &v), R_OK);
    CHECK(v >= 1);
    CHECK(v < cheap.cost);
    CHECK_EQ(g.money, m0 + v);
    CHECK_EQ(g.inv_count, 0);
    CHECK_EQ(inv_liquidate(&g, 0, &v), R_ERR_NO_ITEM);
}
/* ---- 7 ---- */
static void test_judge(void) {
    puts("judge_sale");
    GameState g = fresh();
    Equipment sword = mk_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    Equipment staff = mk_item(&g, EQ_STAFF, MAT_MANA, MAT_MANA);
    Equipment wood = mk_item(&g, EQ_SWORD, MAT_WOOD, MAT_WOOD);
    Customer war = mk_customer(CT_WARRIOR, 200, 40, 1100);

    SaleJudgement j = judge_sale(&war, &sword, sword.rec_price);
    CHECK_EQ(j.result, SALE_OK);
    CHECK(j.fit >= 40);
    CHECK_EQ(judge_sale(&war, &staff, 10).result, SALE_WRONG_TYPE); /* 装備が違う */
    Customer poor = mk_customer(CT_WARRIOR, sword.rec_price - 1, 40, 1300);
    CHECK_EQ(judge_sale(&poor, &sword, sword.rec_price).result, SALE_OVER_BUDGET); /* 予算 */
    CHECK_EQ(judge_sale(&war, &sword, 5000).result, SALE_OVER_BUDGET);
    Customer rich = mk_customer(CT_WARRIOR, 5000, 40, 1100);
    CHECK_EQ(judge_sale(&rich, &sword, 1500).result, SALE_TOO_EXPENSIVE); /* 価値 */
    Customer picky = mk_customer(CT_WARRIOR, 500, 95, 1300);
    CHECK_EQ(judge_sale(&picky, &sword, 1).result, SALE_LOW_FIT); /* 性能 */
    /* 価格の単調性: 価格を下げて、受理が拒否に変わることは決してない */
    int ok_seen = 0, bad_after_ok = 0;
    for (int p = 1; p <= 300; p++) {
        int ok = judge_sale(&war, &sword, p).result == SALE_OK;
        if (ok) {
            ok_seen = 1;
        } else if (ok_seen) {
            bad_after_ok = 1;
        }
    }
    CHECK(ok_seen);
    CHECK(bad_after_ok);
    int pmax = judge_sale(&war, &sword, 1).max_price;
    CHECK_EQ(judge_sale(&war, &sword, pmax).result, SALE_OK);
    CHECK(judge_sale(&war, &sword, pmax + 1).result != SALE_OK);
    /* 優先度: 戦士は鉄の剣を好み、盗賊は軽い木製のものを好む */
    CHECK(equip_fit(CT_WARRIOR, &sword) > equip_fit(CT_WARRIOR, &wood));
    CHECK(equip_fit(CT_THIEF, &wood) > equip_fit(CT_THIEF, &sword));
    Equipment shield = mk_item(&g, EQ_SHIELD, MAT_IRON, MAT_IRON);
    CHECK(equip_fit(CT_GUARDIAN, &shield) > 50);
    CHECK_EQ(judge_sale(&war, &shield, 1).result, SALE_WRONG_TYPE);
    for (int t = 0; t < CUST_TYPE_COUNT; t++) {
        int f = equip_fit(t, &sword);
        CHECK(f >= 0 && f <= 100);
    }
    /* 純粋関数: 同じ入力 -> 同じ答え */
    CHECK_EQ(judge_sale(&war, &sword, 50).fit, judge_sale(&war, &sword, 50).fit);
}
/* ---- 8 ---- */
static void test_sale_flow(void) {
    puts("sale_flow");
    GameState g = fresh();
    g.money = 1000;
    market_buy(&g, MAT_IRON, 3);
    pin_craft(&g, QUALITY_COMMON);
    craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
    market_buy(&g, MAT_IRON, 3);
    pin_craft(&g, QUALITY_COMMON);
    craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
    Equipment item = g.inv[0];
    inv_set_price(&g, 0, item.rec_price);
    CHECK_EQ(customer_offer(&g, 0, NULL), R_ERR_PHASE); /* 店が閉まっている */
    force_open(&g, mk_customer(CT_WARRIOR, 500, 30, 1300));
    CHECK_EQ(g.phase, PH_OPEN);
    CHECK_EQ(g.queue_count, 3);
    CHECK_EQ(day_open(&g), R_ERR_PHASE);                /* 二重には開店できない */
    CHECK_EQ(market_buy(&g, MAT_IRON, 1), R_ERR_PHASE); /* 準備中の操作はロックされる */
    CHECK_EQ(inv_set_price(&g, 0, 1), R_ERR_PHASE);
    int m0 = g.money, r0 = g.rep;
    SaleOutcome o;
    CHECK_EQ(customer_offer(&g, 99, &o), R_ERR_NO_ITEM);
    CHECK_EQ(customer_offer(&g, 0, &o), R_OK);
    CHECK_EQ(o.result, SALE_OK);
    CHECK_EQ(o.sold, 1);
    CHECK_EQ(g.money, m0 + item.rec_price);
    CHECK(g.rep > r0);
    CHECK_EQ(o.rep_delta, g.rep - r0);
    CHECK_EQ(g.inv_count, 1);
    CHECK_EQ(g.total_sold, 1);
    CHECK_EQ(g.total_revenue, item.rec_price);
    CHECK_EQ(g.report.sold, 1);
    CHECK_EQ(g.report.revenue, item.rec_price);
    CHECK_EQ(g.report.cogs, item.cost);
    CHECK_EQ(g.queue_pos, 1);

    /* 拒否してもアイテムは残り、客は苛立ち、やがて去る */
    inv_set_price(&g, 0, 1); /* フェーズが違う: 失敗しなければならない */
    g.inv[0].price = 5000;
    GameState b = g;
    CHECK_EQ(customer_offer(&g, 0, &o), R_OK);
    CHECK(o.result != SALE_OK);
    CHECK_EQ(o.sold, 0);
    CHECK_EQ(g.inv_count, 1);
    CHECK_EQ(g.money, b.money);
    CHECK_EQ(o.customer_left, 0);
    CHECK_EQ(g.queue_pos, 1);
    int rep_b = g.rep;
    CHECK_EQ(customer_offer(&g, 0, &o), R_OK);
    CHECK_EQ(o.customer_left, 1);
    CHECK_EQ(g.queue_pos, 2);
    CHECK_EQ(g.rep, rep_b - 1);
    CHECK_EQ(g.inv_count, 1);
    CHECK_EQ(customer_decline(&g), R_OK); /* 最後の客 */
    CHECK_EQ(g.phase, PH_SETTLE);
    CHECK_EQ(customer_decline(&g), R_ERR_PHASE);
    /* 早期に閉店 */
    g = fresh();
    day_open(&g);
    CHECK_EQ(day_close_early(&g), R_OK);
    CHECK_EQ(g.phase, PH_SETTLE);
    CHECK_EQ(day_close_early(&g), R_ERR_PHASE);
}
/* ---- 9 ---- */
static void test_settle(void) {
    puts("settle");
    GameState g = fresh();
    int p0[MAT_COUNT];
    memcpy(p0, g.price, sizeof p0);
    int m0 = g.money;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.phase, PH_SETTLE);
    CHECK_EQ(g.money, m0 - BAL.upkeep);
    CHECK_EQ(g.report.upkeep, BAL.upkeep);
    CHECK_EQ(net_of(&g.report), -BAL.upkeep);
    CHECK_EQ(g.rep, BAL.start_rep - BAL.rep_idle_loss); /* 売上なし: 評判が下がる */
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(g.prev_price[m], p0[m]);
    }
    CHECK_EQ(day_open(&g), R_ERR_PHASE);
    CHECK_EQ(day_advance(&g), R_OK);
    CHECK_EQ(g.day, 2);
    CHECK_EQ(g.phase, PH_PREP);
    CHECK_EQ(g.report.sold, 0);
    CHECK_EQ(g.report.money_start, g.money);
    CHECK_EQ(day_advance(&g), R_ERR_PHASE);
    /* 市場は長期間にわたって変動帯の内側に収まり、かつ動く */
    int moved = 0;
    for (int d = 0; d < 400; d++) {
        int before[MAT_COUNT];
        memcpy(before, g.price, sizeof before);
        market_update(&g);
        for (int m = 0; m < MAT_COUNT; m++) {
            int base = MATERIALS[m].base_price;
            CHECK(g.price[m] >= MATERIALS[m].min_price);
            CHECK(g.price[m] <= (int)(base * 1.15 + 0.5));
            CHECK(g.price[m] >= (int)(base * 0.85 - 0.5) || g.price[m] == MATERIALS[m].min_price);
            if (g.price[m] != before[m]) {
                moved = 1;
            }
        }
    }
    CHECK(moved);
    /* 決定性 */
    GameState a, b;
    game_init(&a, 777);
    game_init(&b, 777);
    for (int i = 0; i < 20; i++) {
        market_update(&a);
        market_update(&b);
    }
    CHECK(same_state(&a, &b));
    /* 仕入れ先の割引 */
    GameState d0 = fresh(), d3 = fresh();
    d3.up_level[UP_SUPPLY] = 3;
    CHECK(market_quote(&d3, MAT_MANA, 10) < market_quote(&d0, MAT_MANA, 10));
    CHECK_EQ(market_quote(&d3, MAT_MANA, 10), (d3.price[MAT_MANA] * 10 * 85 + 50) / 100);
}
/* ---- 10 ---- */
static void test_upgrades2(void) {
    GameState g = fresh();
    g.money = 100000;
    int cost = upgrade_cost(&g, UP_FORGE), m0 = g.money;
    CHECK_EQ(cost, UPGRADES[UP_FORGE].cost[0]);
    CHECK_EQ(upgrade_buy(&g, UP_FORGE), R_OK);
    CHECK_EQ(g.money, m0 - cost);
    CHECK_EQ(g.up_level[UP_FORGE], 1);
    CHECK_EQ(upgrade_cost(&g, UP_FORGE), UPGRADES[UP_FORGE].cost[1]);
    while (upgrade_buy(&g, UP_FORGE) == R_OK) {
    }
    CHECK_EQ(g.up_level[UP_FORGE], UPGRADES[UP_FORGE].max_level);
    GameState b = g;
    CHECK_EQ(upgrade_buy(&g, UP_FORGE), R_ERR_MAXLEVEL);
    CHECK(same_state(&g, &b));
    CHECK_EQ(upgrade_cost(&g, UP_FORGE), -1);
    CHECK_EQ(upgrade_buy(&g, UP_STORE), R_OK);
    CHECK_EQ(customers_per_day(&g), 4);
    CHECK_EQ(upgrade_buy(&g, UP_STORE), R_OK);
    CHECK_EQ(customers_per_day(&g), 5);
    day_open(&g);
    CHECK_EQ(g.queue_count, 5);
    CHECK_EQ(upgrade_buy(&g, UP_SUPPLY), R_ERR_PHASE); /* 準備中のみ */
    GameState p = fresh();
    p.money = 5;
    GameState pb = p;
    CHECK_EQ(upgrade_buy(&p, UP_SUPPLY), R_ERR_FUNDS);
    CHECK(same_state(&p, &pb));
    CHECK_EQ(upgrade_buy(&p, 7), R_ERR_RANGE);
    CHECK_EQ(upgrade_effect(UP_SUPPLY, 99), UPGRADES[UP_SUPPLY].effect[UPGRADES[UP_SUPPLY].max_level]);
}
/* ---- 11 ---- */
static void test_save_load(void) {
    puts("save_load");
    GameState g = fresh();
    g.money = 900;
    market_buy(&g, MAT_IRON, 6);
    market_buy(&g, MAT_MANA, 3);
    market_buy(&g, MAT_WOOD, 2);
    craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
    craft_equipment(&g, EQ_STAFF, MAT_MANA, MAT_WOOD, NULL);
    inv_adjust_price(&g, 1, 13);
    upgrade_buy(&g, UP_FORGE);
    upgrade_buy(&g, UP_SUPPLY);
    day_open(&g); /* 1 日の途中で保存する */
    SaleOutcome o;
    customer_offer(&g, 0, &o);
    CHECK(game_validate(&g));

    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK_EQ(game_save(&g, f), SAVE_OK);
    rewind(f);
    GameState l;
    memset(&l, 0xAB, sizeof l);
    CHECK_EQ(game_load(&l, f), SAVE_OK);
    CHECK(same_state(&g, &l));
    fclose(f);
    /* ファイル操作の補助関数 */
    const char *path = "test_save.tmp";
    CHECK_EQ(game_save_file(&g, path), SAVE_OK);
    CHECK(save_file_exists(path));
    GameState l2;
    CHECK_EQ(game_load_file(&l2, path), SAVE_OK);
    CHECK(same_state(&g, &l2));
    remove(path);
    CHECK(!save_file_exists(path));
    CHECK_EQ(game_load_file(&l2, path), SAVE_ERR_IO);

    /* 復元したゲームは動き続ける */
    day_close_early(&l);
    CHECK_EQ(l.phase, PH_SETTLE);

    /* 壊れた/悪意のある入力でもクラッシュせず、出力先には触れない */
    const char *bad[] = {
        "",
        "garbage\n",
        "LOOTSMITH_SAVE 99\nend\n",
        "LOOTSMITH_SAVE 1\n",
        "LOOTSMITH_SAVE 1\nrng 1\nday abc\nend\n",
        "LOOTSMITH_SAVE 1\nrng 1\nday 1\nphase 0\nstatus 0\nmoney 5\nrep 500\ntot 0 0 0\nreport 0 0 0 0 0 0 0\ninv "
        "0\nqueue 0 0\nmat 0 0 0 5 5 0\nmat 1 0 0 5 5 0\nmat 2 0 0 5 5 0\nup 0 0\nup 1 0\nup 2 0\nend\n",
        "LOOTSMITH_SAVE 1\nrng 1\nday 1\nphase 0\nstatus 0\nmoney 5\nrep 5\ntot 0 0 0\nreport 0 0 0 0 0 0 0\ninv "
        "99\nqueue 0 0\nend\n",
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        FILE *t = tmpfile();
        fputs(bad[i], t);
        rewind(t);
        GameState keep = fresh(), probe = keep;
        CHECK(game_load(&probe, t) != SAVE_OK);
        CHECK(same_state(&probe, &keep));
        fclose(t);
    }
    /* 切り詰めた本物のセーブ + 1 バイト破損のファジング */
    f = tmpfile();
    game_save(&g, f);
    long len = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)len + 1);
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        CHECK(0);
    }
    fclose(f);
    uint32_t seed = 99;
    int survived = 1;
    for (int n = 0; n < 400; n++) {
        char *copy = malloc((size_t)len + 1);
        memcpy(copy, buf, (size_t)len);
        size_t cut = (size_t)len;
        if (n % 2) {
            cut = (size_t)rng_range(&seed, 0, (int)len);
        } else {
            copy[rng_range(&seed, 0, (int)len - 1)] = (char)rng_range(&seed, 1, 255);
        }
        FILE *t = tmpfile();
        fwrite(copy, 1, cut, t);
        rewind(t);
        GameState probe;
        memset(&probe, 0, sizeof probe);
        SaveResult r = game_load(&probe, t);
        if (r == SAVE_OK && !game_validate(&probe)) {
            survived = 0; /* 受理されたものはすべて有効でなければならない */
        }
        fclose(t);
        free(copy);
    }
    CHECK(survived);
    free(buf);
}

/* ---- セーブ: 必須の "fail" フィールド (回帰) ---- */
static char *save_text(const GameState *g) { /* 本物の game_save() の出力を文字列として得る */
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
static SaveResult load_text(GameState *out, const char *text) { /* 文字列に対する本物の game_load() */
    FILE *f = tmpfile();
    fputs(text, f);
    rewind(f);
    SaveResult r = game_load(out, f);
    fclose(f);
    return r;
}
static char *dup_text(const char *t) {
    char *c = malloc(strlen(t) + 1);
    strcpy(c, t);
    return c;
}
static void drop_line(char *text, const char *prefix) { /* prefix で始まる行を取り除く */
    char *p = text;
    size_t pl = strlen(prefix);
    while (p && *p) {
        if (!strncmp(p, prefix, pl)) {
            char *e = strchr(p, '\n');
            memmove(p, e ? e + 1 : p + strlen(p), strlen(e ? e + 1 : "") + 1);
            return;
        }
        p = strchr(p, '\n');
        if (p) {
            p++;
        }
    }
}
static void test_save_fail_field(void) {
    puts("save_fail_field");
    GameState g = fresh();
    g.money = 1000;
    g.day = BAL.max_days;
    day_open(&g);
    day_close_early(&g); /* -> ST_FAILED / FAIL_TIMEOUT */
    CHECK_EQ(g.fail_reason, FAIL_TIMEOUT);
    char *good = save_text(&g);

    /* A: 完全なデータは読み込め、fail/status が復元される */
    GameState l;
    memset(&l, 0, sizeof l);
    CHECK_EQ(load_text(&l, good), SAVE_OK);
    CHECK(same_state(&g, &l));
    CHECK_EQ(l.fail_reason, FAIL_TIMEOUT);
    CHECK_EQ(l.status, ST_FAILED);

    /* B: fail の行だけを除くと -> 拒否される */
    char *nofail = dup_text(good);
    drop_line(nofail, "fail ");
    CHECK(strstr(nofail, "\nfail ") == NULL);
    CHECK(strlen(nofail) < strlen(good));
    GameState probe = fresh();
    CHECK(load_text(&probe, nofail) != SAVE_OK);

    /* C: 読み込みに失敗しても、既存の状態には触れない */
    GameState keep = fresh();
    market_buy(&keep, MAT_IRON, 3);
    probe = keep;
    CHECK(load_text(&probe, nofail) != SAVE_OK);
    CHECK(same_state(&probe, &keep));

    /* 不正な fail の値は、やはり拒否され、状態は守られる */
    char *badval = dup_text(good);
    char *q = strstr(badval, "fail ");
    CHECK(q != NULL);
    if (q) {
        q[5] = '7';
    }
    probe = keep;
    CHECK_EQ(load_text(&probe, badval), SAVE_ERR_VALUE);
    CHECK(same_state(&probe, &keep));
    free(good);
    free(nofail);
    free(badval);
}

/* ======================================================================
 * コンテンツテーブル / アイコン / セーブ互換性 (コンテンツ拡張の土台)
 * ====================================================================== */
static void test_content_tables(void) {
    puts("content_tables");
    const char *err = content_validate();
    if (err) {
        printf("  content_validate: %s\n", err);
    }
    CHECK(err == NULL);

    /* ID <-> テーブルの添字: 既存の ID は自分のスロットを保つ (並べ替えも再利用もしない)。 */
    CHECK_EQ(MAT_IRON, 0);
    CHECK_EQ(MAT_WOOD, 1);
    CHECK_EQ(MAT_MANA, 2);
    CHECK_EQ(EQ_SWORD, 0);
    CHECK_EQ(EQ_SHIELD, 1);
    CHECK_EQ(EQ_STAFF, 2);
    CHECK_EQ(CT_WARRIOR, 0);
    CHECK_EQ(CT_GUARDIAN, 1);
    CHECK_EQ(CT_MAGE, 2);
    CHECK_EQ(CT_THIEF, 3);
    CHECK_EQ(UP_FORGE, 0);
    CHECK_EQ(UP_STORE, 1);
    CHECK_EQ(UP_SUPPLY, 2);
    CHECK(MAT_COUNT >= 3 && EQ_COUNT >= 3 && CUST_TYPE_COUNT >= 4 && UP_COUNT >= 3);
    CHECK(!strcmp(MATERIALS[MAT_IRON].name, "Iron") && !strcmp(MATERIALS[MAT_WOOD].name, "Wood") &&
          !strcmp(MATERIALS[MAT_MANA].name, "Mana"));
    CHECK(!strcmp(EQUIPS[EQ_SWORD].name, "Sword") && !strcmp(EQUIPS[EQ_SHIELD].name, "Shield") &&
          !strcmp(EQUIPS[EQ_STAFF].name, "Staff"));
    CHECK(!strcmp(CUSTOMER_DEFS[CT_WARRIOR].name, "Warrior") && !strcmp(CUSTOMER_DEFS[CT_GUARDIAN].name, "Guardian"));
    CHECK(!strcmp(CUSTOMER_DEFS[CT_MAGE].name, "Mage") && !strcmp(CUSTOMER_DEFS[CT_THIEF].name, "Thief"));
    CHECK(!strcmp(UPGRADES[UP_FORGE].name, "Forge") && !strcmp(UPGRADES[UP_STORE].name, "Shop") &&
          !strcmp(UPGRADES[UP_SUPPLY].name, "Supplier"));

    /* バランスの契約: 元の数値は変わっていない */
    static const int mat[3][7] = {{10, 7, 8, 8, 0, 10, 6}, {6, 4, 2, 3, 2, 3, 1}, {20, 14, 3, 1, 12, 2, 2}};
    for (int i = 0; i < 3; i++) {
        const MaterialDef *d = &MATERIALS[i];
        int v[7] = {d->base_price, d->min_price, d->atk, d->def, d->mag, d->dur, d->wt};
        for (int k = 0; k < 7; k++) {
            CHECK_EQ(v[k], mat[i][k]);
        }
    }
    static const int eq[3][5] = {{10, 1, 0, 6, 4}, {0, 10, 0, 8, 6}, {1, 1, 10, 4, 3}};
    for (int i = 0; i < 3; i++) {
        const EquipDef *d = &EQUIPS[i];
        int v[5] = {d->atk, d->def, d->mag, d->dur, d->wt};
        for (int k = 0; k < 5; k++) {
            CHECK_EQ(v[k], eq[i][k]);
        }
    }
    static const int cu[4][13] = {{EQ_SWORD, 60, 0, 0, 40, 0, 70, 130, 35, 60, 1000, 1300, 0},
                                  {EQ_SHIELD, 0, 60, 0, 40, 0, 70, 130, 35, 60, 1000, 1300, 0},
                                  {EQ_STAFF, 0, 0, 100, 0, 0, 80, 170, 35, 65, 1000, 1350, 0},
                                  {EQ_SWORD, 40, 0, 0, 0, 60, 40, 90, 30, 50, 900, 1100, 0}};
    for (int i = 0; i < 4; i++) {
        const CustomerDef *d = &CUSTOMER_DEFS[i];
        int v[12] = {d->wants_eq,   d->w_atk,      d->w_def,      d->w_mag,      d->w_dur,  d->w_light,
                     d->budget_min, d->budget_max, d->fit_min_lo, d->fit_min_hi, d->tol_lo, d->tol_hi};
        for (int k = 0; k < 12; k++) {
            CHECK_EQ(v[k], cu[i][k]);
        }
    }
    CHECK_EQ(UPGRADES[UP_FORGE].max_level, 3);
    CHECK_EQ(UPGRADES[UP_FORGE].cost[2], 450);
    CHECK_EQ(UPGRADES[UP_FORGE].effect[3], 1400);
    CHECK_EQ(UPGRADES[UP_STORE].max_level, 2);
    CHECK_EQ(UPGRADES[UP_STORE].cost[1], 400);
    CHECK_EQ(UPGRADES[UP_STORE].effect[2], 5);
    CHECK_EQ(UPGRADES[UP_SUPPLY].max_level, 3);
    CHECK_EQ(UPGRADES[UP_SUPPLY].cost[0], 100);
    CHECK_EQ(UPGRADES[UP_SUPPLY].effect[3], 15);

    /* 効果テキストはテーブルの行から取る */
    char b[64];
    upgrade_effect_text(UP_FORGE, 1, b, sizeof b);
    CHECK(!strcmp(b, "Gear stats x1.12"));
    upgrade_effect_text(UP_STORE, 2, b, sizeof b);
    CHECK(!strcmp(b, "5 customers / day"));
    upgrade_effect_text(UP_SUPPLY, 3, b, sizeof b);
    CHECK(!strcmp(b, "15% off materials"));
    upgrade_effect_text(99, 0, b, sizeof b);
    CHECK(!strcmp(b, ""));
    upgrade_effect_text(UP_FORGE, 1, b, 5);
    CHECK_EQ((int)strlen(b), 4); /* 安全に切り詰める */

    /* 検証関数は不正な行を本当に拒否する (本物のテーブルの変更可能なコピーで確認) */
    MaterialDef m[MAT_COUNT];
    EquipDef e[EQ_COUNT];
    CustomerDef c[CUST_TYPE_COUNT];
    UpgradeDef u[UP_COUNT];
    memcpy(m, MATERIALS, sizeof m);
    memcpy(e, EQUIPS, sizeof e);
    memcpy(c, CUSTOMER_DEFS, sizeof c);
    memcpy(u, UPGRADES, sizeof u);
#define V() content_validate_tables(m, MAT_COUNT, e, EQ_COUNT, c, CUST_TYPE_COUNT, u, UP_COUNT)
    CHECK(V() == NULL);
    {
        const char *o = m[1].name;
        m[1].name = NULL;
        CHECK(V() != NULL);
        m[1].name = o;
    }
    {
        const char *o = m[1].name;
        m[1].name = "Nineteen-char-name-x";
        CHECK(V() != NULL);
        m[1].name = o;
    }
    {
        const char *o = m[1].name;
        m[1].name = m[0].name;
        CHECK(V() != NULL);
        m[1].name = o;
    }
    {
        const char *o = m[1].desc;
        m[1].desc = "";
        CHECK(V() != NULL);
        m[1].desc = o;
    }
    {
        const char *o = m[1].icon;
        m[1].icon = "typo-icon";
        CHECK(V() != NULL);
        m[1].icon = NULL;
        CHECK(V() == NULL);
        m[1].icon = o;
    } /* アイコン未設定は許可される */
    {
        int o = m[1].min_price;
        m[1].min_price = m[1].base_price + 1;
        CHECK(V() != NULL);
        m[1].min_price = o;
    }
    {
        int o = e[1].atk;
        e[1].atk = -1;
        CHECK(V() != NULL);
        e[1].atk = o;
    }
    {
        int o = c[1].wants_eq;
        c[1].wants_eq = EQ_COUNT;
        CHECK(V() != NULL);
        c[1].wants_eq = o;
    }
    {
        int o = c[1].budget_max;
        c[1].budget_max = 1;
        CHECK(V() != NULL);
        c[1].budget_max = o;
    }
    {
        int o = u[1].max_level;
        u[1].max_level = MAX_UP_LEVEL + 1;
        CHECK(V() != NULL);
        u[1].max_level = o;
    }
    {
        int o = u[1].hotkey;
        u[1].hotkey = u[0].hotkey;
        CHECK(V() != NULL);
        u[1].hotkey = '1';
        CHECK(V() != NULL);
        u[1].hotkey = o;
    }
    {
        const char *o = u[1].effect_fmt;
        u[1].effect_fmt = "no number";
        CHECK(V() != NULL);
        u[1].effect_fmt = o;
    }
    {
        int o = u[0].cost[0];
        u[0].cost[0] = 0;
        CHECK(V() != NULL);
        u[0].cost[0] = o;
    }
    CHECK(V() == NULL);
#undef V
}

static void test_icons(void) {
    puts("icons");
    /* すべてのコンテンツは実在するスプライトを参照するか、アイコンを未設定にする (フォールバック) */
    for (int i = 0; i < MAT_COUNT; i++) {
        CHECK(!MATERIALS[i].icon || !MATERIALS[i].icon[0] || icon_exists(MATERIALS[i].icon));
    }
    for (int i = 0; i < EQ_COUNT; i++) {
        CHECK(!EQUIPS[i].icon || !EQUIPS[i].icon[0] || icon_exists(EQUIPS[i].icon));
    }
    for (int i = 0; i < CUST_TYPE_COUNT; i++) {
        CHECK(!CUSTOMER_DEFS[i].icon || !CUSTOMER_DEFS[i].icon[0] || icon_exists(CUSTOMER_DEFS[i].icon));
    }
    /* フォールバック: NULL にならず、常に「?」スプライト */
    const IconSprite *fb = icon_find(ICON_FALLBACK_ID);
    CHECK(fb != NULL);
    CHECK(icon_find(NULL) == fb);
    CHECK(icon_find("") == fb);
    CHECK(icon_find("no-such-sprite") == fb);
    CHECK(!icon_exists(NULL));
    CHECK(!icon_exists(""));
    CHECK(!icon_exists("no-such-sprite"));
    CHECK(!icon_exists(ICON_FALLBACK_ID));
    CHECK(icon_find("sword") != fb);
    CHECK(!strcmp(icon_find("sword")->id, "sword"));
    CHECK(icon_at(-1) == fb);
    CHECK(icon_at(icon_count() + 5) == fb); /* 範囲外 -> こちらもフォールバック */
    /* スプライトデータが整っている: 12 行 x 12 の既知の文字、ID は一意 */
    for (int i = 0; i < icon_count(); i++) {
        const IconSprite *sp = icon_at(i);
        CHECK(sp->id && sp->id[0]);
        for (int j = 0; j < i; j++) {
            CHECK(strcmp(icon_at(j)->id, sp->id) != 0);
        }
        for (int r = 0; r < ICON_SIZE; r++) {
            CHECK(sp->rows[r] != NULL);
            if (!sp->rows[r]) {
                continue;
            }
            CHECK_EQ((int)strlen(sp->rows[r]), ICON_SIZE);
            for (int cc = 0; cc < ICON_SIZE && sp->rows[r][cc]; cc++) {
                CHECK(strchr(ICON_PALETTE_CHARS, sp->rows[r][cc]) != NULL);
            }
        }
    }
}

/* 元の (拡張前の) ビルドが書いた本物のセーブ: "counts" 行なし。 */
static const char LEGACY_SAVE[] =
    "LOOTSMITH_SAVE 1\nrng 1454000782\nday 1\nphase 1\nstatus 0\nfail 0\nmoney 502\nrep 10\n"
    "mat 0 3 27 9 9 -100\nmat 1 3 18 6 6 21\nmat 2 2 38 19 19 -75\nup 0 1\nup 1 0\nup 2 1\n"
    "tot 0 0 0\nreport 3 0 0 0 0 0 250\ninv 3\n"
    "item 0 0 1 16 7 1 14 8 24 54 65 65\nitem 1 0 0 8 18 0 18 12 27 59 71 78\nitem 2 2 1 4 3 19 6 5 44 69 83 83\n"
    "queue 3 0\ncust 0 64 57 1038 1\ncust 0 76 50 1229 2\ncust 1 107 56 1115 2\nend\n";

static char *replace_line(const char *text, const char *prefix, const char *with) { /* with == NULL なら行を取り除く */
    char *out = malloc(strlen(text) + (with ? strlen(with) : 0) + 2), *w = out;
    const char *p = text;
    size_t pl = strlen(prefix);
    int done = 0;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        if (!done && !strncmp(p, prefix, pl)) {
            done = 1;
            if (with) {
                size_t k = strlen(with);
                memcpy(w, with, k);
                w += k;
                *w++ = '\n';
            }
        } else {
            memcpy(w, p, n);
            w += n;
        }
        p += n;
    }
    *w = 0;
    CHECK(done);
    return out;
}

/* Dragon Scale (素材 8 番目) より前のビルドが書いたセーブを模す: counts を差し替え、旧ビルドが書くはずのない
 * `mat 7` 行を取り除く (残すと「宣言された件数を超えるレコード」として拒否される)。 */
static char *as_older_build(const char *text, const char *counts_line) {
    char *a = replace_line(text, "counts ", counts_line);
    char *b = replace_line(a, "mat 7 ", NULL);
    free(a);
    return b;
}

static char *add_line_after_header(const char *text, const char *line) { /* `line` を 2 行目に挿入する */
    const char *nl = strchr(text, '\n') + 1;
    size_t hl = (size_t)(nl - text), n = strlen(text) + strlen(line) + 2;
    char *out = malloc(n);
    memcpy(out, text, hl);
    out[hl] = 0;
    strcat(out, line);
    strcat(out, "\n");
    strcat(out, nl);
    return out;
}

static void test_save_legacy_and_migration(void) {
    puts("save_legacy_and_migration");
    GameState l;
    memset(&l, 0xAB, sizeof l);

    /* 1: 元の形式のセーブは読み込め、何も失われない */
    CHECK_EQ(load_text(&l, LEGACY_SAVE), SAVE_OK);
    CHECK_EQ(l.day, 1);
    CHECK_EQ(l.phase, PH_OPEN);
    CHECK_EQ(l.status, ST_PLAYING);
    CHECK_EQ(l.money, 502);
    CHECK_EQ(l.rep, 10);
    CHECK_EQ((long long)l.rng, 1454000782LL);
    CHECK_EQ(l.mat_stock[MAT_IRON], 3);
    CHECK_EQ(l.mat_cost[MAT_IRON], 27);
    CHECK_EQ(l.price[MAT_IRON], 9);
    CHECK_EQ(l.swing_pm[MAT_IRON], -100);
    CHECK_EQ(l.mat_stock[MAT_WOOD], 3);
    CHECK_EQ(l.swing_pm[MAT_WOOD], 21);
    CHECK_EQ(l.mat_stock[MAT_MANA], 2);
    CHECK_EQ(l.mat_cost[MAT_MANA], 38);
    CHECK_EQ(l.up_level[UP_FORGE], 1);
    CHECK_EQ(l.up_level[UP_STORE], 0);
    CHECK_EQ(l.up_level[UP_SUPPLY], 1);
    CHECK_EQ(l.inv_count, 3);
    CHECK_EQ(l.inv[0].type, EQ_SWORD);
    CHECK_EQ(l.inv[0].main, MAT_IRON);
    CHECK_EQ(l.inv[0].sub, MAT_WOOD);
    CHECK_EQ(l.inv[0].atk, 16);
    CHECK_EQ(l.inv[0].price, 65);
    CHECK_EQ(l.inv[1].type, EQ_SHIELD);
    CHECK_EQ(l.inv[1].price, 78);
    CHECK_EQ(l.inv[1].rec_price, 71);
    CHECK_EQ(l.inv[2].type, EQ_STAFF);
    CHECK_EQ(l.inv[2].main, MAT_MANA);
    CHECK_EQ(l.inv[2].mag, 19);
    CHECK_EQ(l.queue_count, 3);
    CHECK_EQ(l.queue_pos, 0);
    CHECK_EQ(l.queue[2].type, CT_GUARDIAN);
    CHECK_EQ(l.queue[1].patience, 2);
    CHECK_EQ(l.report.money_start, 250);
    CHECK(game_validate(&l));

    /* 2: 遊び続けられ、現在の形式 (counts 付き) で再保存でき、それは同一に再読み込みできる */
    GameState played = l;
    day_close_early(&played);
    CHECK_EQ(played.phase, PH_SETTLE);
    char *again = save_text(&l);
    CHECK(strstr(again, "\ncounts ") != NULL);
    GameState r2;
    CHECK_EQ(load_text(&r2, again), SAVE_OK);
    CHECK(same_state(&l, &r2));
    char *third = save_text(&r2);
    CHECK(!strcmp(again, third));
    free(third);

    /* 3: 古いセーブ + その後に追加されたコンテンツ: 新しい ID は初期値になり、古いデータは保たれる。
     *    (各テーブルで 1 つ少ない ID を宣言し、その ID のレコードを落とすことで再現している。) */
    GameState g = fresh();
    g.money = 700;
    market_buy(&g, MAT_IRON, 5);
    market_buy(&g, MAT_WOOD, 4);
    craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_WOOD, NULL);
    craft_equipment(&g, EQ_SHIELD, MAT_IRON, MAT_IRON, NULL);
    upgrade_buy(&g, UP_FORGE);
    force_open(&g, mk_customer(CT_WARRIOR, 100, 40, 1100));
    char *cur = save_text(&g);
    char hdr[64];
    snprintf(hdr, sizeof hdr, "counts %d %d %d %d", MAT_COUNT - 1, EQ_COUNT - 1, CUST_TYPE_COUNT - 1, UP_COUNT - 1);
    char *old = replace_line(cur, "counts ", hdr);
    char key[24];
    snprintf(key, sizeof key, "mat %d ", MAT_COUNT - 1);
    char *t1 = replace_line(old, key, NULL);
    free(old);
    old = t1;
    snprintf(key, sizeof key, "up %d ", UP_COUNT - 1);
    t1 = replace_line(old, key, NULL);
    free(old);
    old = t1;
    GameState o;
    memset(&o, 0, sizeof o);
    CHECK_EQ(load_text(&o, old), SAVE_OK);
    CHECK_EQ(o.mat_stock[MAT_COUNT - 1], 0);
    CHECK_EQ(o.mat_cost[MAT_COUNT - 1], 0);
    CHECK_EQ(o.swing_pm[MAT_COUNT - 1], 0);
    CHECK_EQ(o.price[MAT_COUNT - 1], MATERIALS[MAT_COUNT - 1].base_price);
    CHECK_EQ(o.prev_price[MAT_COUNT - 1], MATERIALS[MAT_COUNT - 1].base_price);
    CHECK_EQ(o.up_level[UP_COUNT - 1], 0);
    CHECK_EQ(o.money, g.money);
    CHECK_EQ(o.rep, g.rep);
    CHECK_EQ(o.day, g.day);
    CHECK_EQ(o.inv_count, g.inv_count);
    CHECK(!memcmp(o.inv, g.inv, sizeof o.inv));
    CHECK_EQ(o.mat_stock[MAT_IRON], g.mat_stock[MAT_IRON]);
    CHECK_EQ(o.up_level[UP_FORGE], 1);
    CHECK(game_validate(&o));

    /* 4: 破損は破損のまま: セーブが宣言した *はず* のレコードは存在しなければならない */
    snprintf(key, sizeof key, "mat %d ", MAT_IRON);
    char *bad = replace_line(cur, key, NULL);
    GameState keep = fresh(), probe = keep;
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
    snprintf(key, sizeof key, "up %d ", UP_STORE);
    bad = replace_line(cur, key, NULL);
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
    bad = replace_line(LEGACY_SAVE, "mat 1 ", NULL); /* 穴のある旧形式のファイル */
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    free(bad);
    bad = replace_line(LEGACY_SAVE, "up 2 ", NULL);
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    free(bad);

    /* 5: より新しいビルド (より多くのコンテンツを知っている) のセーブは、バージョン問題として拒否され、状態には触れない
     */
    snprintf(hdr, sizeof hdr, "counts %d %d %d %d", MAT_COUNT + 1, EQ_COUNT, CUST_TYPE_COUNT, UP_COUNT);
    bad = replace_line(cur, "counts ", hdr);
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VERSION);
    CHECK(same_state(&probe, &keep));
    free(bad);
    snprintf(hdr, sizeof hdr, "counts %d %d %d %d", MAT_COUNT, EQ_COUNT + 2, CUST_TYPE_COUNT, UP_COUNT + 1);
    bad = replace_line(cur, "counts ", hdr);
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VERSION);
    free(bad);
    { /* ... 未知の ID のレコードを含む */
        char *ext = malloc(strlen(cur) + 64);
        snprintf(hdr, sizeof hdr, "counts %d %d %d %d", MAT_COUNT + 1, EQ_COUNT, CUST_TYPE_COUNT, UP_COUNT);
        char *b2 = replace_line(cur, "counts ", hdr);
        snprintf(ext, strlen(cur) + 64, "%s", b2);
        char *endp = strstr(ext, "end\n");
        snprintf(endp, 64, "mat %d 1 5 5 5 0\nend\n", MAT_COUNT);
        CHECK_EQ(load_text(&probe, ext), SAVE_ERR_VERSION);
        CHECK(same_state(&probe, &keep));
        free(b2);
        free(ext);
    }

    /* 6: その他の不正なヘッダ / バージョン / ID */
    CHECK_EQ(load_text(&probe, "LOOTSMITH_SAVE 0\nend\n"), SAVE_ERR_VERSION);
    {
        char fut[40];
        snprintf(fut, sizeof fut, "LOOTSMITH_SAVE %d\nend\n", SAVE_VERSION + 1); /* より新しいゲームのセーブ */
        CHECK_EQ(load_text(&probe, fut), SAVE_ERR_VERSION);
    }
    CHECK_EQ(load_text(&probe, "LOOTSMITH_SAVE -1\nend\n"), SAVE_ERR_VERSION);
    bad = replace_line(cur, "counts ", "counts 3 3 4");
    CHECK(load_text(&probe, bad) != SAVE_OK);
    free(bad);
    bad = replace_line(cur, "counts ", "counts -1 3 4 3");
    CHECK(load_text(&probe, bad) != SAVE_OK);
    free(bad);
    {
        char *dup = add_line_after_header(cur, "counts 3 3 4 3"); /* 2 つ目の counts 行 */
        CHECK(load_text(&probe, dup) != SAVE_OK);
        free(dup);
    }
    snprintf(hdr, sizeof hdr, "mat %d 2 38 19 19 -75", MAT_COUNT); /* どのテーブルにもない ID、counts なし */
    bad = replace_line(LEGACY_SAVE, "mat 2 ", hdr);
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    free(bad);
    bad = replace_line(LEGACY_SAVE, "mat 2 ", "mat -1 2 38 19 19 -75");
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    free(bad);
    bad = replace_line(LEGACY_SAVE, "item 2 ", "item 9 2 1 4 3 19 6 5 44 69 83 83");
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    free(bad);
    /* アイテムは、セーブが「まだ存在しなかった」と言う ID を使えない (この本文は Mana / Staff = id 2 を使う) */
    snprintf(hdr, sizeof hdr, "counts 2 2 3 2");
    {
        char *withc = add_line_after_header(LEGACY_SAVE, hdr); /* 旧形式の本文 + より小さい counts */
        char *b3 = replace_line(withc, "mat 2 ", NULL), *b4 = replace_line(b3, "up 2 ", NULL);
        CHECK_EQ(load_text(&probe, b4),
                 SAVE_ERR_VALUE); /* staff / mana 製のアイテムが、宣言された件数を超える ID を使っている */
        CHECK(same_state(&probe, &keep));
        free(withc);
        free(b3);
        free(b4);
    }
    free(again);
    free(cur);
    free(old);
}

/* ======================================================================
 * コンテンツパック 1: Copper / Steel / Silver / Obsidian、Dagger / Spear / Grimoire、Scout / Lancer / Scholar
 * ====================================================================== */
static void test_content_pack1(void) {
    puts("content_pack1");
    CHECK(content_validate() == NULL);
    CHECK_EQ(NAME_MAX_LEN, 18);

    /* ID は各 enum の末尾に追加された (既存の ID は番号を保つ) */
    CHECK_EQ(MAT_COPPER, 3);
    CHECK_EQ(MAT_STEEL, 4);
    CHECK_EQ(MAT_SILVER, 5);
    CHECK_EQ(MAT_OBSIDIAN, 6);
    CHECK_EQ(MAT_DRAGON_SCALE, 7); /* Dragon Scale は末尾に追記された */
    CHECK_EQ(MAT_COUNT, 8);
    CHECK_EQ(EQ_DAGGER, 3);
    CHECK_EQ(EQ_SPEAR, 4);
    CHECK_EQ(EQ_GRIMOIRE, 5);
    CHECK_EQ(EQ_COUNT, 10); /* +4 の設計図装備 */
    CHECK_EQ(CT_SCOUT, 4);
    CHECK_EQ(CT_LANCER, 5);
    CHECK_EQ(CT_SCHOLAR, 6);
    CHECK_EQ(CUST_TYPE_COUNT, 11);
    CHECK_EQ(UP_COUNT, 3); /* アップグレードは追加されていない */

    /* 素材: 名前、アイコン ID、価格、最低価格、ATK DEF MAG DUR WT。能力値は仕様どおり。Steel / Silver / Obsidian の
     * 価格は最初の案から下方調整された (ボットのテストを参照)。 */
    static const struct {
        int id;
        const char *name, *icon;
        int v[7];
    } mats[] = {{MAT_COPPER, "Copper", "copper", {8, 5, 5, 4, 0, 6, 3}},
                {MAT_STEEL, "Steel", "steel", {12, 8, 10, 8, 0, 11, 6}},
                {MAT_SILVER, "Silver", "silver", {13, 9, 3, 4, 8, 5, 2}},
                {MAT_OBSIDIAN, "Obsidian", "obsidian", {13, 9, 13, 1, 5, 3, 4}}};
    for (size_t i = 0; i < sizeof mats / sizeof *mats; i++) {
        const MaterialDef *d = &MATERIALS[mats[i].id];
        CHECK(!strcmp(d->name, mats[i].name));
        CHECK(!strcmp(d->icon, mats[i].icon));
        CHECK(icon_exists(d->icon));
        int v[7] = {d->base_price, d->min_price, d->atk, d->def, d->mag, d->dur, d->wt};
        for (int k = 0; k < 7; k++) {
            CHECK_EQ(v[k], mats[i].v[k]);
        }
        CHECK(d->desc && d->desc[0]);
    }
    /* 装備: ATK DEF MAG DUR WT は仕様どおり */
    static const struct {
        int id;
        const char *name, *icon;
        int v[5];
    } eqs[] = {{EQ_DAGGER, "Dagger", "dagger", {5, 1, 0, 4, 1}},
               {EQ_SPEAR, "Spear", "spear", {8, 3, 0, 6, 4}},
               {EQ_GRIMOIRE, "Grimoire", "grimoire", {0, 1, 13, 3, 2}}};
    for (size_t i = 0; i < sizeof eqs / sizeof *eqs; i++) {
        const EquipDef *d = &EQUIPS[eqs[i].id];
        CHECK(!strcmp(d->name, eqs[i].name));
        CHECK(!strcmp(d->icon, eqs[i].icon));
        CHECK(icon_exists(d->icon));
        int v[5] = {d->atk, d->def, d->mag, d->dur, d->wt};
        for (int k = 0; k < 5; k++) {
            CHECK_EQ(v[k], eqs[i].v[k]);
        }
    }
    /* 客: 求める装備、優先度、仕様どおりの予算の範囲 */
    static const struct {
        int id;
        const char *name;
        int wants, bmin, bmax;
    } cus[] = {{CT_SCOUT, "Scout", EQ_DAGGER, 45, 110},
               {CT_LANCER, "Lancer", EQ_SPEAR, 70, 145},
               {CT_SCHOLAR, "Scholar", EQ_GRIMOIRE, 90, 190}};
    for (size_t i = 0; i < sizeof cus / sizeof *cus; i++) {
        const CustomerDef *d = &CUSTOMER_DEFS[cus[i].id];
        CHECK(!strcmp(d->name, cus[i].name));
        CHECK_EQ(d->wants_eq, cus[i].wants);
        CHECK_EQ(d->budget_min, cus[i].bmin);
        CHECK_EQ(d->budget_max, cus[i].bmax);
        CHECK(icon_exists(d->icon));
        CHECK(d->hat.a && d->armor.a);
    }
    CHECK(CUSTOMER_DEFS[CT_SCOUT].w_atk > 0 && CUSTOMER_DEFS[CT_SCOUT].w_light > 0 &&
          CUSTOMER_DEFS[CT_SCOUT].w_def == 0);
    CHECK(CUSTOMER_DEFS[CT_LANCER].w_atk > 0 && CUSTOMER_DEFS[CT_LANCER].w_def > 0 &&
          CUSTOMER_DEFS[CT_LANCER].w_dur > 0);
    CHECK(CUSTOMER_DEFS[CT_SCHOLAR].w_mag > 0 && CUSTOMER_DEFS[CT_SCHOLAR].w_dur > 0 &&
          CUSTOMER_DEFS[CT_SCHOLAR].w_atk == 0);
    /* 3 人の新しい客は、互いにも出荷時の客とも見た目が異なる */
    for (int a = 0; a < CUST_TYPE_COUNT; a++) {
        for (int b = 0; b < a; b++) {
            CHECK(memcmp(&CUSTOMER_DEFS[a].hat, &CUSTOMER_DEFS[b].hat, sizeof(IconColor)) ||
                  memcmp(&CUSTOMER_DEFS[a].armor, &CUSTOMER_DEFS[b].armor, sizeof(IconColor)));
        }
    }
    /* すべての装備は誰かに求められ、すべての客は実在する装備を求める */
    for (int e = 0; e < EQ_COUNT; e++) {
        int wanted = 0;
        for (int c = 0; c < CUST_TYPE_COUNT; c++) {
            wanted |= CUSTOMER_DEFS[c].wants_eq == e;
        }
        CHECK(wanted);
    }

    /* 能力値は既存の混合式で合成される: 基本値 + (2*main + sub)/3 を鍛冶場で拡大 (手計算) */
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = EQ_DAGGER;
    e.main = MAT_COPPER;
    e.sub = MAT_WOOD;
    equipment_compute_stats(&e, 0);
    CHECK_EQ(e.atk, 9);
    CHECK_EQ(e.def, 5);
    CHECK_EQ(e.mag, 1);
    CHECK_EQ(e.dur, 9);
    CHECK_EQ(e.wt, 3);
    e.type = EQ_SPEAR;
    e.main = MAT_STEEL;
    e.sub = MAT_IRON;
    equipment_compute_stats(&e, 0);
    CHECK_EQ(e.atk, 17);
    CHECK_EQ(e.def, 11);
    CHECK_EQ(e.mag, 0);
    CHECK_EQ(e.dur, 17);
    CHECK_EQ(e.wt, 10);
    equipment_compute_stats(&e, 1); /* 鍛冶場 Lv1 = x1.12 (wt は除く) */
    CHECK_EQ(e.atk, 19);
    CHECK_EQ(e.def, 12);
    CHECK_EQ(e.dur, 19);
    CHECK_EQ(e.wt, 10);
    e.type = EQ_GRIMOIRE;
    e.main = MAT_SILVER;
    e.sub = MAT_OBSIDIAN;
    equipment_compute_stats(&e, 0);
    CHECK_EQ(e.atk, 6);
    CHECK_EQ(e.def, 4);
    CHECK_EQ(e.mag, 20);
    CHECK_EQ(e.dur, 7);
    CHECK_EQ(e.wt, 5);

    /* 新しい客はそれぞれ、何らかのレシピで推奨価格のまま採算が取れる */
    for (int ct = CT_SCOUT; ct <= CT_SCHOLAR; ct++) {
        const CustomerDef *d = &CUSTOMER_DEFS[ct];
        Customer mid = mk_customer(ct, (d->budget_min + d->budget_max) / 2, (d->fit_min_lo + d->fit_min_hi) / 2,
                                   (d->tol_lo + d->tol_hi) / 2);
        int found = 0;
        for (int m = 0; m < MAT_COUNT; m++) {
            for (int sub = 0; sub < MAT_COUNT; sub++) {
                Equipment q;
                memset(&q, 0, sizeof q);
                q.type = d->wants_eq;
                q.main = m;
                q.sub = sub;
                equipment_compute_stats(&q, 0);
                q.cost = 2 * MATERIALS[m].base_price + MATERIALS[sub].base_price;
                equipment_price(&q);
                if (judge_sale(&mid, &q, q.rec_price).result == SALE_OK && q.rec_price > q.cost) {
                    found = 1;
                }
            }
        }
        CHECK(found);
        /* ... ただし、誤った種類の装備では取れない */
        Equipment w = mk_item(&(GameState){0}, d->wants_eq == EQ_SWORD ? EQ_SHIELD : EQ_SWORD, MAT_IRON, MAT_IRON);
        CHECK_EQ(judge_sale(&mid, &w, 1).result, SALE_WRONG_TYPE);
    }
    /* 出荷時の客は、これまでとまったく同じものを求める */
    CHECK_EQ(CUSTOMER_DEFS[CT_THIEF].wants_eq, EQ_SWORD);
    CHECK_EQ(CUSTOMER_DEFS[CT_MAGE].wants_eq, EQ_STAFF);

    /* 客の生成はすべてのタイプに届き、各タイプの範囲を守る */
    uint32_t rng = 777;
    int seen[CUST_TYPE_COUNT] = {0};
    for (int i = 0; i < 3000; i++) {
        Customer c;
        customer_generate(&rng, 50, 50, &c); /* 50 件: すべての客が解放済み */
        CHECK(c.type >= 0 && c.type < CUST_TYPE_COUNT);
        seen[c.type]++;
        const CustomerDef *d = &CUSTOMER_DEFS[c.type];
        CHECK(c.budget >= d->budget_min * 900 / 1000 && c.budget <= d->budget_max * 1100 / 1000);
        CHECK(c.min_fit >= d->fit_min_lo && c.min_fit <= d->fit_min_hi);
        CHECK(c.tol_pm >= d->tol_lo && c.tol_pm <= d->tol_hi);
    }
    for (int t = 0; t < CUST_TYPE_COUNT; t++) {
        CHECK(seen[t] > 3000 / CUST_TYPE_COUNT / 2);
    }

    /* 名前の上限: 18 文字は可、19 文字は不可 */
    MaterialDef m[MAT_COUNT];
    EquipDef eq[EQ_COUNT];
    CustomerDef c[CUST_TYPE_COUNT];
    UpgradeDef u[UP_COUNT];
    memcpy(m, MATERIALS, sizeof m);
    memcpy(eq, EQUIPS, sizeof eq);
    memcpy(c, CUSTOMER_DEFS, sizeof c);
    memcpy(u, UPGRADES, sizeof u);
    m[1].name = "123456789012345678";
    CHECK(content_validate_tables(m, MAT_COUNT, eq, EQ_COUNT, c, CUST_TYPE_COUNT, u, UP_COUNT) == NULL);
    m[1].name = "1234567890123456789";
    CHECK(content_validate_tables(m, MAT_COUNT, eq, EQ_COUNT, c, CUST_TYPE_COUNT, u, UP_COUNT) != NULL);
}

/* 新しいコンテンツを本物のゲームループで通す: クラフト、販売、保存、再読み込み、続行。 */
static void test_content_pack1_flow(void) {
    puts("content_pack1_flow");
    GameState g = fresh();
    g.money = 2000;
    market_buy(&g, MAT_COPPER, 3);
    market_buy(&g, MAT_WOOD, 3);
    market_buy(&g, MAT_STEEL, 3);
    market_buy(&g, MAT_IRON, 3);
    market_buy(&g, MAT_SILVER, 3);
    market_buy(&g, MAT_OBSIDIAN, 3);
    int di, si, gi;
    pin_craft(&g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&g, EQ_DAGGER, MAT_COPPER, MAT_WOOD, &di), R_OK);
    pin_craft(&g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&g, EQ_SPEAR, MAT_STEEL, MAT_IRON, &si), R_OK);
    pin_craft(&g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&g, EQ_GRIMOIRE, MAT_SILVER, MAT_OBSIDIAN, &gi), R_OK);
    CHECK_EQ(g.inv_count, 3);
    CHECK_EQ(g.inv[di].type, EQ_DAGGER);
    CHECK_EQ(g.inv[si].main, MAT_STEEL);
    CHECK_EQ(g.inv[gi].sub, MAT_OBSIDIAN);
    CHECK_EQ(g.mat_stock[MAT_COPPER], 1);
    CHECK_EQ(g.mat_stock[MAT_WOOD], 2); /* 従来どおり main 2 + sub 1 */
    CHECK_EQ(g.mat_stock[MAT_OBSIDIAN], 2);
    CHECK_EQ(g.mat_stock[MAT_SILVER], 1);
    CHECK(game_validate(&g));

    /* それぞれを対応する客に売る (予算/許容幅を広くして、テストの対象を配線に絞る) */
    static const int who[3] = {CT_SCOUT, CT_LANCER, CT_SCHOLAR};
    const int idx[3] = {di, si, gi};
    GameState s = g;
    for (int k = 0; k < 3; k++) {
        Customer c = mk_customer(who[k], 400, 20, 1500);
        force_open(&s, c);
        int before = s.money, pos = s.inv_count - 1;
        (void)pos;
        int at = -1;
        for (int i = 0; i < s.inv_count; i++) {
            if (s.inv[i].type == CUSTOMER_DEFS[who[k]].wants_eq) {
                at = i;
            }
        }
        CHECK(at >= 0);
        (void)idx;
        SaleOutcome o;
        CHECK_EQ(customer_offer(&s, at, &o), R_OK);
        CHECK_EQ(o.result, SALE_OK);
        CHECK_EQ(o.sold, 1);
        CHECK(s.money > before);
        day_close_early(&s);
        day_advance(&s);
        CHECK(game_validate(&s));
    }
    CHECK_EQ(s.inv_count, 0);
    CHECK_EQ(s.total_sold, 3);

    /* 新しいコンテンツを在庫/待ち行列に持った状態で保存し、同一に再読み込みする。テキストは安定している */
    GameState q = g;
    force_open(&q, mk_customer(CT_SCHOLAR, 150, 40, 1100));
    char *t1 = save_text(&q);
    CHECK(strstr(t1, "\ncounts 8 10 11 3\n") != NULL);
    CHECK(strstr(t1, "\nmat 6 ") != NULL); /* Obsidian のレコード */
    GameState r;
    CHECK_EQ(load_text(&r, t1), SAVE_OK);
    CHECK(same_state(&q, &r));
    char *t2 = save_text(&r);
    CHECK(!strcmp(t1, t2));
    CHECK_EQ(r.inv[2].type, EQ_GRIMOIRE);
    CHECK_EQ(r.queue[0].type, CT_SCHOLAR);
    SaleOutcome o;
    CHECK_EQ(customer_offer(&r, 2, &o), R_OK); /* 再読み込みの後も遊び続けられる */
    free(t1);
    free(t2);
}

/* 以前のビルド (素材 3 / 装備 3 / 客 4 / アップグレード 3) が書いたセーブも、読み込めなければならない。 */
static void test_save_before_pack1(void) {
    puts("save_before_pack1");
    GameState l, l2, probe;
    memset(&probe, 0, sizeof probe);
    CHECK_EQ(load_text(&l, LEGACY_SAVE), SAVE_OK);
    /* 古いゲームのものは何も変わらない ... */
    CHECK_EQ(l.money, 502);
    CHECK_EQ(l.rep, 10);
    CHECK_EQ(l.day, 1);
    CHECK_EQ(l.phase, PH_OPEN);
    CHECK_EQ(l.inv_count, 3);
    CHECK_EQ(l.inv[2].type, EQ_STAFF);
    CHECK_EQ(l.queue_count, 3);
    CHECK_EQ(l.up_level[UP_FORGE], 1);
    CHECK_EQ(l.up_level[UP_SUPPLY], 1);
    CHECK_EQ(l.mat_stock[MAT_IRON], 3);
    CHECK_EQ(l.mat_stock[MAT_WOOD], 3);
    CHECK_EQ(l.mat_stock[MAT_MANA], 2);
    /* ... 追加された素材は、空の在庫と基準価格で始まる */
    for (int m = MAT_COPPER; m < MAT_COUNT; m++) {
        CHECK_EQ(l.mat_stock[m], 0);
        CHECK_EQ(l.mat_cost[m], 0);
        CHECK_EQ(l.swing_pm[m], 0);
        CHECK_EQ(l.price[m], MATERIALS[m].base_price);
        CHECK_EQ(l.prev_price[m], MATERIALS[m].base_price);
    }
    CHECK(game_validate(&l));
    /* 直前のビルドは "counts 3 3 4 3" も書いていた (このパックより前のビルドの形式): 結果は同じ */
    char *with = add_line_after_header(LEGACY_SAVE, "counts 3 3 4 3");
    CHECK_EQ(load_text(&l2, with), SAVE_OK);
    CHECK(same_state(&l, &l2));
    free(with);
    /* 古いゲームは続く: 1 日を終え、新しい市場価格が効き、新しいコンテンツがクラフトできる */
    GameState p = l;
    day_close_early(&p);
    day_advance(&p);
    CHECK(game_validate(&p));
    CHECK_EQ(p.day, 2);
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK(p.price[m] >= MATERIALS[m].min_price);
    }
    p.money = 1000;
    CHECK_EQ(market_buy(&p, MAT_STEEL, 3), R_OK);
    pin_craft(&p, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&p, EQ_SPEAR, MAT_STEEL, MAT_STEEL, NULL), R_OK);
    /* 現在の形式で再保存すると、同一に再読み込みできる */
    char *t = save_text(&p);
    CHECK(strstr(t, "counts 8 10 11 3") != NULL);
    GameState r;
    CHECK_EQ(load_text(&r, t), SAVE_OK);
    char *t2 = save_text(&r);
    CHECK(!strcmp(t, t2)); /* (テキストで比較: 使われていない待ち行列スロットは保存されない) */
    CHECK_EQ(r.money, p.money);
    CHECK_EQ(r.day, p.day);
    CHECK_EQ(r.inv_count, p.inv_count);
    CHECK(!memcmp(r.inv, p.inv, sizeof r.inv));
    free(t);
    free(t2);
    /* 古いセーブには新しい ID が含まれてはならない: それは新コンテンツではなく破損 */
    char *bad =
        replace_line(LEGACY_SAVE, "item 2 ", "item 3 0 1 5 1 0 4 1 20 30 30 30"); /* それより前のセーブにある Dagger */
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    free(bad);
    bad = replace_line(LEGACY_SAVE, "cust 0 76 ", "cust 4 76 50 1229 2"); /* それより前のセーブにある Scout */
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    free(bad);
}

/* ======================================================================
 * ギルド契約
 * ====================================================================== */
static void pass_day(GameState *g) { /* 開店し、すべての客を断り、翌朝へ進む */
    day_open(g);
    while (g->phase == PH_OPEN) {
        customer_decline(g);
    }
    if (g->status == ST_PLAYING || g->status == ST_SANDBOX) {
        day_advance(g);
    }
}
static void put_contract(GameState *g, int ct, int min_fit, int diff) { /* 掲示板のスロット 0 にある決定的な契約 */
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
    if (g->guild_board_count < 1) {
        g->guild_board_count = 1;
    }
}
static int give_item(GameState *g, int eq, int main, int sub) { /* 実際にクラフトして、在庫の添字を返す */
    g->mat_stock[main] += 3;
    g->mat_stock[sub] += 3;
    g->mat_cost[main] += 3 * MATERIALS[main].base_price;
    g->mat_cost[sub] += 3 * MATERIALS[sub].base_price;
    int idx = -1;
    pin_craft(g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(g, eq, main, sub, &idx), R_OK);
    return idx;
}

static void test_guild_generation(void) {
    puts("guild_generation");
    CHECK(content_validate() == NULL);
    CHECK_EQ(GUILD_OFFER_COUNT, 3);
    CHECK_EQ(GUILD_ACTIVE_MAX, 2);
    /* 難易度テーブルは仕様どおり */
    static const int dv[3][4] = {{35, 35, 1, 3}, {50, 55, 2, 3}, {65, 80, 3, 4}};
    for (int d = 0; d < GD_COUNT; d++) {
        CHECK_EQ(GUILD_DIFFS[d].min_fit, dv[d][0]);
        CHECK_EQ(GUILD_DIFFS[d].gold, dv[d][1]);
        CHECK_EQ(GUILD_DIFFS[d].rep, dv[d][2]);
        CHECK_EQ(GUILD_DIFFS[d].days, dv[d][3]);
    }
    CHECK(!strcmp(GUILD_DIFFS[GD_EASY].name, "Easy") && !strcmp(GUILD_DIFFS[GD_HARD].name, "Hard"));

    /* 同じシード -> 同じ最初の掲示板であり、それは正当なもの */
    GameState a, b;
    game_init(&a, 777);
    game_init(&b, 777);
    CHECK(!memcmp(a.guild_board, b.guild_board, sizeof a.guild_board));
    CHECK_EQ(a.guild_rng, b.guild_rng);
    CHECK_EQ(a.guild_board_count, 3);
    CHECK_EQ(a.guild_active_count, 0);
    CHECK_EQ(a.guild_done, 0);
    CHECK_EQ(a.guild_expired, 0);
    int differs = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        GameState g;
        game_init(&g, seed);
        CHECK(game_validate(&g));
        CHECK(g.guild_rng != 0);
        CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);
        CHECK_EQ(g.guild_next_id, GUILD_OFFER_COUNT + 1);
        int seen_types = 0;
        for (int i = 0; i < GUILD_OFFER_COUNT; i++) {
            const GuildContract *c = &g.guild_board[i];
            CHECK_EQ(c->id, i + 1);
            CHECK_EQ(c->deadline_day, 0);
            CHECK(c->difficulty == GD_EASY || c->difficulty == GD_STANDARD); /* 1〜5 日目: Hard なし */
            const GuildDifficultyDef *d = &GUILD_DIFFS[c->difficulty];
            CHECK_EQ(c->min_fit, d->min_fit);
            CHECK_EQ(c->gold, d->gold);
            CHECK_EQ(c->rep, d->rep);
            CHECK_EQ(c->days, d->days);
            CHECK(!(seen_types & (1 << c->customer_type)));
            seen_types |= 1 << c->customer_type; /* 客はそれぞれ異なる */
        }
        GameState h;
        game_init(&h, seed + 1000);
        if (memcmp(h.guild_board, g.guild_board, sizeof h.guild_board)) {
            differs++;
        }
    }
    CHECK(differs > 250);

    /* 難易度の確率は日数の区分に従う */
    static const struct {
        int day;
        int pct[3];
    } bands[] = {{1, {70, 30, 0}},   {5, {70, 30, 0}},   {6, {30, 50, 20}},
                 {15, {30, 50, 20}}, {16, {15, 45, 40}}, {30, {15, 45, 40}}};
    for (size_t k = 0; k < sizeof bands / sizeof *bands; k++) {
        GameState g;
        game_init(&g, 4242);
        g.day = bands[k].day;
        int cnt[3] = {0}, total = 0;
        for (int rounds = 0; rounds < 2000; rounds++) {
            guild_refresh_board(&g);
            for (int i = 0; i < GUILD_OFFER_COUNT; i++) {
                cnt[g.guild_board[i].difficulty]++;
                total++;
            }
        }
        for (int d = 0; d < 3; d++) {
            if (bands[k].pct[d] == 0) {
                CHECK_EQ(cnt[d], 0);
            } else {
                CHECK(abs(cnt[d] * 100 / total - bands[k].pct[d]) <= 3);
            }
        }
    }

    /* ギルドはゲームの乱数を消費しない */
    GameState g;
    game_init(&g, 99);
    uint32_t r0 = g.rng;
    for (int i = 0; i < 20; i++) {
        guild_refresh_board(&g);
    }
    CHECK_EQ(g.rng, r0);
    guild_accept(&g, 0);
    guild_accept(&g, 0);
    CHECK_EQ(g.rng, r0);
    /* ... だから、ギルドを使うキャンペーンは、無視するキャンペーンと同じ市場/客の展開になる */
    GameState idle, busy;
    game_init(&idle, 31337);
    game_init(&busy, 31337);
    for (int day = 0; day < 12; day++) {
        while (busy.guild_active_count < GUILD_ACTIVE_MAX && busy.guild_board_count > 0) {
            guild_accept(&busy, 0);
        }
        pass_day(&idle);
        pass_day(&busy);
        CHECK_EQ(idle.rng, busy.rng);
        CHECK(!memcmp(idle.price, busy.price, sizeof idle.price));
        CHECK(game_validate(&busy));
    }
    CHECK(busy.guild_expired > 0);

    /* 掲示板は暦に従う。受注した契約は残る。ID は再利用されない */
    GameState c;
    game_init(&c, 5);
    guild_accept(&c, 1);
    GuildContract kept = c.guild_active[0];
    int first_next = c.guild_next_id;
    GuildContract old_board = c.guild_board[0];
    pass_day(&c);
    CHECK_EQ(c.day, 2);
    CHECK_EQ(c.guild_board_count, GUILD_OFFER_COUNT);
    CHECK_EQ(c.guild_next_id, first_next + GUILD_OFFER_COUNT);
    CHECK_EQ(c.guild_active_count, 1);
    CHECK(!memcmp(&c.guild_active[0], &kept, sizeof kept)); /* 1 日をまたいでも手つかずのまま */
    CHECK(c.guild_board[0].id > old_board.id);
    int ids[64], n = 0;
    for (int day = 0; day < 15; day++) {
        for (int i = 0; i < c.guild_board_count; i++) {
            for (int j = 0; j < n; j++) {
                CHECK(ids[j] != c.guild_board[i].id);
            }
            if (n < 64) {
                ids[n++] = c.guild_board[i].id;
            }
        }
        pass_day(&c);
    }
}

static void test_guild_accept(void) {
    puts("guild_accept");
    GameState g = fresh();
    g.day = 5;
    put_contract(&g, CT_WARRIOR, 35, GD_STANDARD); /* 3 日間、5 日目に受注 */
    int id = g.guild_board[0].id, others = g.guild_board_count - 1;
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK_EQ(g.guild_active_count, 1);
    CHECK_EQ(g.guild_board_count, 2);
    CHECK_EQ(others, 2);
    CHECK_EQ(g.guild_active[0].id, id);
    CHECK_EQ(g.guild_active[0].deadline_day, 7); /* 5 + 3 - 1 */
    for (int i = 0; i < g.guild_board_count; i++) {
        CHECK(g.guild_board[i].id != id && g.guild_board[i].deadline_day == 0);
    }
    CHECK(game_validate(&g));
    GameState before = g;
    CHECK_EQ(guild_accept(&g, 2), GUILD_ERR_NO_CONTRACT); /* 空のスロット */
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK); /* 2 つ目のスロット */
    CHECK_EQ(g.guild_active_count, GUILD_ACTIVE_MAX);
    CHECK_EQ(g.guild_board_count, 1);
    before = g;
    CHECK_EQ(guild_accept(&g, 0), GUILD_ERR_FULL);
    CHECK(same_state(&g, &before)); /* 上限に達した */
    CHECK_EQ(guild_accept(&g, 7), GUILD_ERR_RANGE);
    CHECK(same_state(&g, &before));
    /* 最初の期限日の計算を、すべての難易度で確認する */
    for (int d = 0; d < GD_COUNT; d++) {
        GameState h = fresh();
        h.day = 9;
        put_contract(&h, CT_MAGE, 0, d);
        CHECK_EQ(guild_accept(&h, 0), GUILD_OK);
        CHECK_EQ(h.guild_active[0].deadline_day, 9 + GUILD_DIFFS[d].days - 1);
    }
    /* 不正な添字、誤ったフェーズ、終了したキャンペーン: 何にも触れずに拒否される */
    GameState p = fresh();
    before = p;
    CHECK_EQ(guild_accept(&p, -1), GUILD_ERR_RANGE);
    CHECK_EQ(guild_accept(&p, GUILD_OFFER_COUNT), GUILD_ERR_RANGE);
    CHECK_EQ(guild_accept(&p, 1000), GUILD_ERR_RANGE);
    CHECK(same_state(&p, &before));
    day_open(&p);
    before = p;
    CHECK_EQ(guild_accept(&p, 0), GUILD_ERR_PHASE);
    CHECK(same_state(&p, &before)); /* PH_OPEN */
    day_close_early(&p);
    before = p;
    CHECK_EQ(guild_accept(&p, 0), GUILD_ERR_PHASE);
    CHECK(same_state(&p, &before)); /* PH_SETTLE */
    int ended[] = {ST_CLEARED, ST_FAILED};
    for (int i = 0; i < 2; i++) {
        GameState e = fresh();
        e.status = ended[i];
        if (ended[i] == ST_FAILED) {
            e.fail_reason = FAIL_TIMEOUT;
        }
        before = e;
        CHECK_EQ(guild_accept(&e, 0), GUILD_ERR_PHASE);
        CHECK(same_state(&e, &before));
    }
    GameState sb = fresh();
    sb.status = ST_SANDBOX;
    CHECK_EQ(guild_accept(&sb, 0), GUILD_OK); /* サンドボックスは可 */
}

static void test_guild_deliver(void) {
    puts("guild_deliver");
    GameState g = fresh();
    g.money = 1000;
    put_contract(&g, CT_WARRIOR, 35, GD_EASY);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    int dagger = give_item(&g, EQ_DAGGER, MAT_IRON, MAT_IRON);
    int sword = give_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    int shield = give_item(&g, EQ_SHIELD, MAT_IRON, MAT_IRON);
    CHECK_EQ(g.inv_count, 3);
    int fit = equip_fit(CT_WARRIOR, &g.inv[sword]);
    CHECK(fit >= 35);
    CHECK_EQ(guild_item_fit(&g, 0, sword), fit); /* 客が使うのと同じ数値 */
    CHECK_EQ(guild_item_fit(&g, 0, shield), -1);
    CHECK_EQ(guild_item_fit(&g, 0, 9), -1);
    CHECK_EQ(guild_item_fit(&g, 1, sword), -1);
    uint32_t rng0 = g.rng;

    /* 失敗しても何も変わらない */
    GameState before = g;
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&g, 0, shield, &o), GUILD_ERR_WRONG_TYPE);
    CHECK_EQ(o.result, GUILD_ERR_WRONG_TYPE);
    CHECK_EQ(o.item_type, EQ_SHIELD);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, 0, dagger, &o), GUILD_ERR_WRONG_TYPE);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, 0, 3, &o), GUILD_ERR_NO_ITEM);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, 0, -1, &o), GUILD_ERR_NO_ITEM);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, 1, sword, &o), GUILD_ERR_NO_CONTRACT);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, -1, sword, &o), GUILD_ERR_RANGE);
    CHECK_EQ(guild_deliver(&g, GUILD_ACTIVE_MAX, sword, &o), GUILD_ERR_RANGE);
    CHECK(same_state(&g, &before));
    CHECK_EQ(guild_deliver(&g, 0, shield, NULL), GUILD_ERR_WRONG_TYPE); /* out は NULL でもよい */

    /* 成功: 選んだアイテムだけが消費され、報酬が支払われ、販売統計は汚れない */
    Equipment keep0 = g.inv[dagger], keep1 = g.inv[shield];
    int money = g.money, rep = g.rep, cost = g.inv[sword].cost;
    CHECK_EQ(guild_deliver(&g, 0, sword, &o), GUILD_OK);
    CHECK_EQ(o.result, GUILD_OK);
    CHECK_EQ(o.gold, 35);
    CHECK_EQ(o.rep_gain, 1);
    CHECK_EQ(o.fit, fit);
    CHECK_EQ(o.profit, 35 - cost);
    CHECK_EQ(o.item_type, EQ_SWORD);
    CHECK_EQ(g.inv_count, 2);
    CHECK(!memcmp(&g.inv[0], &keep0, sizeof keep0));
    CHECK(!memcmp(&g.inv[1], &keep1, sizeof keep1)); /* 剣はなくなり、他は何も変わらない */
    CHECK_EQ(g.money, money + 35);
    CHECK_EQ(g.rep, rep + 1);
    CHECK_EQ(g.guild_active_count, 0);
    CHECK_EQ(g.guild_done, 1);
    CHECK_EQ(g.guild_expired, 0);
    CHECK_EQ(g.report.guild_gold, 35);
    CHECK_EQ(g.report.guild_done, 1);
    CHECK_EQ(g.report.cogs, cost);
    CHECK_EQ(g.report.rep_delta, 1);
    CHECK_EQ(g.report.revenue, 0);
    CHECK_EQ(g.report.sold, 0);
    CHECK_EQ(g.total_revenue, 0);
    CHECK_EQ(g.total_sold, 0); /* 販売ではない */
    CHECK_EQ(net_of(&g.report), 35 - cost - g.report.upkeep);
    CHECK_EQ(g.rng, rng0); /* ゲームの乱数は使っていない */
    CHECK(game_validate(&g));
    CHECK_EQ(guild_deliver(&g, 0, 0, &o), GUILD_ERR_NO_CONTRACT); /* 納品した契約はなくなる */

    /* FIT が不足 */
    GameState l = fresh();
    l.money = 1000;
    put_contract(&l, CT_WARRIOR, 65, GD_HARD);
    guild_accept(&l, 0);
    int weak = give_item(&l, EQ_SWORD, MAT_WOOD, MAT_WOOD);
    CHECK(equip_fit(CT_WARRIOR, &l.inv[weak]) < 65);
    before = l;
    CHECK_EQ(guild_deliver(&l, 0, weak, &o), GUILD_ERR_LOW_FIT);
    CHECK_EQ(o.fit, equip_fit(CT_WARRIOR, &l.inv[weak]));
    CHECK(same_state(&l, &before));
    int strong = give_item(&l, EQ_SWORD, MAT_STEEL, MAT_STEEL);
    CHECK(equip_fit(CT_WARRIOR, &l.inv[strong]) >= 65);
    CHECK_EQ(guild_deliver(&l, 0, strong, &o), GUILD_OK);
    CHECK_EQ(o.gold, 80);
    CHECK_EQ(o.rep_gain, 3);
    CHECK_EQ(l.inv_count, 1);
    CHECK_EQ(l.inv[0].main, MAT_WOOD); /* 弱い方はまだ残っている */

    /* 契約は客タイプを指名する: 同じ装備を求める別のタイプは、そのタイプの重みで判定する */
    GameState t = fresh();
    t.money = 1000;
    put_contract(&t, CT_THIEF, 0, GD_EASY);
    guild_accept(&t, 0);
    int sw = give_item(&t, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK_EQ(guild_item_fit(&t, 0, sw), equip_fit(CT_THIEF, &t.inv[sw]));
    CHECK(equip_fit(CT_THIEF, &t.inv[sw]) != equip_fit(CT_WARRIOR, &t.inv[sw]));

    /* 評判には上限がある */
    GameState c = fresh();
    c.money = 1000;
    c.rep = BAL.rep_cap - 1;
    put_contract(&c, CT_WARRIOR, 0, GD_HARD);
    guild_accept(&c, 0);
    int it = give_item(&c, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK_EQ(guild_deliver(&c, 0, it, &o), GUILD_OK);
    CHECK_EQ(o.rep_gain, 1);
    CHECK_EQ(c.rep, BAL.rep_cap);
    CHECK_EQ(c.report.rep_delta, 1);
    GameState f = fresh();
    f.money = 1000;
    f.rep = BAL.rep_cap;
    put_contract(&f, CT_WARRIOR, 0, GD_HARD);
    guild_accept(&f, 0);
    it = give_item(&f, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK_EQ(guild_deliver(&f, 0, it, &o), GUILD_OK);
    CHECK_EQ(o.rep_gain, 0);
    CHECK_EQ(f.rep, BAL.rep_cap);

    /* 契約 2 件: 2 つ目を先に納品しても、1 つ目はスロット 0 に残る */
    GameState two = fresh();
    two.money = 1000;
    put_contract(&two, CT_WARRIOR, 0, GD_EASY);
    guild_accept(&two, 0);
    int idA = two.guild_active[0].id;
    put_contract(&two, CT_MAGE, 0, GD_EASY);
    guild_accept(&two, 0);
    int idB = two.guild_active[1].id;
    int staff = give_item(&two, EQ_STAFF, MAT_MANA, MAT_MANA);
    CHECK_EQ(guild_deliver(&two, 1, staff, &o), GUILD_OK);
    CHECK_EQ(o.contract_id, idB);
    CHECK_EQ(two.guild_active_count, 1);
    CHECK_EQ(two.guild_active[0].id, idA);

    /* フェーズ / キャンペーン状態による制限 */
    GameState p = fresh();
    p.money = 1000;
    put_contract(&p, CT_WARRIOR, 0, GD_EASY);
    guild_accept(&p, 0);
    int pi = give_item(&p, EQ_SWORD, MAT_IRON, MAT_IRON);
    GameState open = p;
    day_open(&open);
    before = open;
    CHECK_EQ(guild_deliver(&open, 0, pi, &o), GUILD_ERR_PHASE);
    CHECK(same_state(&open, &before));
    GameState cl = p;
    cl.status = ST_CLEARED;
    before = cl;
    CHECK_EQ(guild_deliver(&cl, 0, pi, &o), GUILD_ERR_PHASE);
    CHECK(same_state(&cl, &before));
    GameState fl = p;
    fl.status = ST_FAILED;
    fl.fail_reason = FAIL_TIMEOUT;
    before = fl;
    CHECK_EQ(guild_deliver(&fl, 0, pi, &o), GUILD_ERR_PHASE);
    CHECK(same_state(&fl, &before));
    GameState sb = p;
    sb.status = ST_SANDBOX;
    CHECK_EQ(guild_deliver(&sb, 0, pi, &o), GUILD_OK);
}

static void test_guild_deadline(void) {
    puts("guild_deadline");
    /* 1 日目に 3 日間で受注 -> 期限は 3 日目: 3 日目の準備フェーズなら納品でき、3 日目の精算後はなくなる */
    GameState g = fresh();
    g.money = 5000;
    put_contract(&g, CT_WARRIOR, 0, GD_STANDARD);
    guild_accept(&g, 0);
    CHECK_EQ(g.guild_active[0].deadline_day, 3);
    GameState twin = g; /* 契約を一度も使わない同じゲーム */
    twin.guild_active_count = 0;
    memset(twin.guild_active, 0, sizeof twin.guild_active);
    for (int day = 1; day <= 2; day++) {
        pass_day(&g);
        pass_day(&twin);
        CHECK_EQ(g.guild_active_count, 1);
        CHECK_EQ(g.guild_expired, 0); /* 期限より前ではない */
        CHECK(game_validate(&g));
    }
    CHECK_EQ(g.day, 3);
    CHECK_EQ(g.phase, PH_PREP);
    int it = give_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    GameState later = g; /* 分岐 A: 期限日に納品する */
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&later, 0, it, &o), GUILD_OK);
    day_open(&later);
    while (later.phase == PH_OPEN) {
        customer_decline(&later);
    }
    CHECK_EQ(later.guild_expired, 0);
    CHECK_EQ(later.guild_done, 1);
    CHECK(game_validate(&later));
    /* 分岐 B: 失効させる */
    GameState miss = g;
    GameState miss_twin = twin;
    give_item(&miss_twin, EQ_SWORD, MAT_IRON, MAT_IRON);
    day_open(&miss);
    day_open(&miss_twin);
    while (miss.phase == PH_OPEN) {
        customer_decline(&miss);
    }
    while (miss_twin.phase == PH_OPEN) {
        customer_decline(&miss_twin);
    }
    CHECK_EQ(miss.phase, PH_SETTLE);
    CHECK_EQ(miss.guild_active_count, 0);
    CHECK_EQ(miss.guild_expired, 1);
    CHECK_EQ(miss.report.guild_expired, 1);
    CHECK_EQ(miss.guild_done, 0);
    CHECK_EQ(miss.money, miss_twin.money);
    CHECK_EQ(miss.rep, miss_twin.rep); /* ゴールド/評判への余分な影響はない */
    CHECK_EQ(miss.inv_count, miss_twin.inv_count);
    CHECK_EQ(miss.total_profit, miss_twin.total_profit); /* 何も消費されない */
    CHECK(game_validate(&miss));
    /* 二重処理されない */
    CHECK_EQ(day_close_early(&miss), R_ERR_PHASE);
    CHECK_EQ(miss.guild_expired, 1);
    CHECK_EQ(day_advance(&miss), R_OK);
    CHECK_EQ(miss.guild_expired, 1);
    CHECK_EQ(miss.report.guild_expired, 0);
    pass_day(&miss);
    CHECK_EQ(miss.guild_expired, 1);

    /* 早期の閉店と、通常の「最後の客に対応した」経路は、どちらもちょうど 1 回だけ精算する */
    for (int mode = 0; mode < 2; mode++) {
        GameState h = fresh();
        put_contract(&h, CT_MAGE, 0, GD_EASY);
        guild_accept(&h, 0);
        h.guild_active[0].deadline_day = 1; /* 本日が期限 (本日受注、1 日の期間) */
        h.guild_active[0].days = 1;
        CHECK(game_validate(&h));
        day_open(&h);
        if (mode == 0) {
            day_close_early(&h);
        } else {
            while (h.phase == PH_OPEN) {
                customer_decline(&h);
            }
        }
        CHECK_EQ(h.guild_expired, 1);
        CHECK_EQ(h.guild_active_count, 0);
        CHECK(game_validate(&h));
    }
    /* 契約 2 件、本日が期限のものと後のもの: 期限のものだけが失効する */
    GameState two = fresh();
    put_contract(&two, CT_MAGE, 0, GD_EASY);
    guild_accept(&two, 0);
    put_contract(&two, CT_THIEF, 0, GD_HARD);
    guild_accept(&two, 0); /* 期限は 4 日目 */
    two.guild_active[0].deadline_day = 1;
    two.guild_active[0].days = 1;
    int keep_id = two.guild_active[1].id;
    day_open(&two);
    day_close_early(&two);
    CHECK_EQ(two.guild_expired, 1);
    CHECK_EQ(two.guild_active_count, 1);
    CHECK_EQ(two.guild_active[0].id, keep_id);

    /* 最終日で終わるキャンペーン: 失効は集計され、状態は整合したままで、day_advance は拒否する */
    GameState end = fresh();
    end.day = BAL.max_days;
    end.money = 400;
    put_contract(&end, CT_MAGE, 0, GD_HARD);
    guild_accept(&end, 0);
    end.guild_active[0].deadline_day = end.day + 2;
    day_open(&end);
    day_close_early(&end);
    CHECK_EQ(end.status, ST_FAILED);
    CHECK_EQ(end.fail_reason, FAIL_TIMEOUT);
    CHECK_EQ(end.guild_active_count, 1);
    CHECK(game_validate(&end)); /* まだ期限ではない: 保持されたまま (支払いも請求もされない) */
    CHECK_EQ(day_advance(&end), R_ERR_PHASE);
    GameState end2 = fresh();
    end2.day = BAL.max_days;
    put_contract(&end2, CT_MAGE, 0, GD_EASY);
    guild_accept(&end2, 0);
    end2.guild_active[0].deadline_day = end2.day;
    end2.guild_active[0].days = 1; /* 最終日がちょうど期限 */
    day_open(&end2);
    day_close_early(&end2);
    CHECK_EQ(end2.status, ST_FAILED);
    CHECK_EQ(end2.guild_expired, 1);
    CHECK(game_validate(&end2));

    /* サンドボックス: 契約は引き続き機能し、失効する */
    GameState sb = fresh();
    sb.status = ST_SANDBOX;
    put_contract(&sb, CT_MAGE, 0, GD_EASY);
    guild_accept(&sb, 0);
    for (int day = 0; day < 4; day++) {
        pass_day(&sb);
        CHECK(game_validate(&sb));
    }
    CHECK_EQ(sb.guild_expired, 1);
    CHECK_EQ(sb.status, ST_SANDBOX);

    /* ギルド報酬は同じ日のクリア判定に算入される。これがなければその日はクリアにならない */
    for (int with = 0; with < 2; with++) {
        GameState w = fresh();
        w.rep = BAL.rep_cap;
        put_contract(&w, CT_WARRIOR, 0, GD_STANDARD);
        guild_accept(&w, 0);
        w.money = BAL.goal_money - GUILD_DIFFS[GD_STANDARD].gold + BAL.upkeep + 1;
        int sw = give_item(&w, EQ_SWORD, MAT_IRON, MAT_IRON);
        if (with) {
            CHECK_EQ(guild_deliver(&w, 0, sw, &o), GUILD_OK);
        }
        day_open(&w);
        day_close_early(&w);
        CHECK_EQ(w.status, with ? ST_CLEARED : ST_PLAYING);
        CHECK(game_validate(&w));
        if (with) {
            CHECK_EQ(w.total_profit, o.profit - BAL.upkeep); /* 報酬 - アイテムの原価 - 維持費 */
        }
    }
    /* 販売のみの日: 数値はギルドが存在する前とまったく同じ */
    GameState s = fresh();
    s.money = 1000;
    int sd = give_item(&s, EQ_SWORD, MAT_IRON, MAT_IRON);
    force_open(&s, mk_customer(CT_WARRIOR, 300, 20, 1500));
    SaleOutcome so;
    CHECK_EQ(customer_offer(&s, sd, &so), R_OK);
    CHECK_EQ(so.sold, 1);
    day_close_early(&s);
    CHECK_EQ(s.report.guild_gold, 0);
    CHECK_EQ(net_of(&s.report), s.report.revenue - s.report.cogs - s.report.upkeep);
}

static void test_guild_save(void) {
    puts("guild_save");
    GameState g = fresh();
    g.money = 3000;
    pass_day(&g);
    pass_day(&g);
    guild_accept(&g, 0);
    guild_accept(&g, 1);
    int it = give_item(&g, CUSTOMER_DEFS[g.guild_active[0].customer_type].wants_eq, MAT_STEEL, MAT_STEEL);
    GuildOutcome o;
    guild_deliver(&g, 0, it, &o); /* FIT で失敗することもある。どちらでも有効な状態 */
    CHECK(game_validate(&g));
    char *t1 = save_text(&g);
    CHECK(!strncmp(t1, "LOOTSMITH_SAVE 4\n", 17));
    CHECK_EQ(SAVE_VERSION, 4); /* v4 = 契約の種類と報酬素材 (gb / ga の末尾 3 フィールド) */
    CHECK(strstr(t1, "\nguild ") && strstr(t1, "\ngreport ") && strstr(t1, "\ngboard ") && strstr(t1, "\ngactive "));
    GameState r;
    CHECK_EQ(load_text(&r, t1), SAVE_OK);
    {
        char *tr = save_text(&r);
        CHECK(!strcmp(t1, tr));
        free(tr);
    } /* (テキストで比較: 使われていない待ち行列スロットは保存されない) */
    CHECK_EQ(r.money, g.money);
    CHECK_EQ(r.day, g.day);
    CHECK_EQ(r.inv_count, g.inv_count);
    CHECK(!memcmp(r.inv, g.inv, sizeof r.inv));
    CHECK_EQ(r.guild_rng, g.guild_rng);
    CHECK_EQ(r.guild_next_id, g.guild_next_id);
    CHECK_EQ(r.guild_board_count, g.guild_board_count);
    CHECK_EQ(r.guild_active_count, g.guild_active_count);
    CHECK(!memcmp(r.guild_board, g.guild_board, sizeof r.guild_board));
    CHECK(!memcmp(r.guild_active, g.guild_active, sizeof r.guild_active));
    CHECK_EQ(r.guild_done, g.guild_done);
    CHECK_EQ(r.guild_expired, g.guild_expired);
    char *prev = t1;
    for (int round = 0; round < 3; round++) { /* 保存/読み込みを繰り返してもずれない */
        GameState x;
        char *tx = save_text(&r);
        CHECK(!strcmp(tx, prev));
        CHECK_EQ(load_text(&x, tx), SAVE_OK);
        CHECK(same_state(&x, &r));
        r = x;
        if (prev != t1) {
            free(prev);
        }
        prev = tx;
    }
    if (prev != t1) {
        free(prev);
    }
    /* 再読み込みしたゲームは、元のゲームと同じ契約を生成する */
    GameState a = g, b = r;
    pass_day(&a);
    pass_day(&b);
    CHECK(!memcmp(a.guild_board, b.guild_board, sizeof a.guild_board));
    CHECK_EQ(a.guild_rng, b.guild_rng);
    CHECK_EQ(a.guild_next_id, b.guild_next_id);

    /* バージョン 1 -> 2 の移行 */
    GameState m, m2;
    CHECK_EQ(load_text(&m, LEGACY_SAVE), SAVE_OK);
    CHECK_EQ(load_text(&m2, LEGACY_SAVE), SAVE_OK);
    CHECK(same_state(&m, &m2)); /* 決定的 */
    CHECK_EQ(m.money, 502);
    CHECK_EQ(m.rep, 10);
    CHECK_EQ(m.day, 1);
    CHECK_EQ(m.inv_count, 3);
    CHECK_EQ(m.queue_count, 3);
    CHECK_EQ(m.up_level[UP_FORGE], 1);
    CHECK_EQ(m.mat_stock[MAT_IRON], 3);
    CHECK_EQ(m.report.money_start, 250);
    CHECK_EQ((long long)m.rng, 1454000782LL); /* ゲームの乱数は消費されていない */
    CHECK(m.guild_rng != 0 && m.guild_rng != m.rng);
    CHECK_EQ(m.guild_active_count, 0);
    CHECK_EQ(m.guild_done, 0);
    CHECK_EQ(m.guild_expired, 0);
    CHECK_EQ(m.report.guild_gold, 0);
    CHECK_EQ(m.guild_board_count, GUILD_OFFER_COUNT);
    CHECK_EQ(m.guild_next_id, GUILD_OFFER_COUNT + 1);
    for (int i = 0; i < GUILD_OFFER_COUNT; i++) {
        CHECK_EQ(m.guild_board[i].id, i + 1);
    }
    CHECK(game_validate(&m));
    char *with = add_line_after_header(LEGACY_SAVE, "counts 3 3 4 3"); /* ギルド導入前のビルドの形式 */
    CHECK_EQ(load_text(&m2, with), SAVE_OK);
    CHECK(same_state(&m, &m2));
    free(with);
    char *mt = save_text(&m);
    CHECK(!strncmp(mt, "LOOTSMITH_SAVE 4\n", 17));
    GameState mr;
    CHECK_EQ(load_text(&mr, mt), SAVE_OK);
    CHECK(same_state(&m, &mr));
    free(mt);
    /* 移行したゲームは遊び続けられ、その中でギルドが機能する */
    GameState pm = m;
    day_close_early(&pm);
    CHECK_EQ(day_advance(&pm), R_OK); /* セーブは 1 日の途中で取られた */
    CHECK_EQ(pm.guild_board_count, GUILD_OFFER_COUNT);
    CHECK_EQ(pm.day, 2);
    CHECK_EQ(guild_accept(&pm, 0), GUILD_OK);
    CHECK(game_validate(&pm));
    day_open(&pm);
    day_close_early(&pm);
    CHECK(game_validate(&pm));

    /* 壊れた/不整合なギルドデータは拒否され、出力先には触れない */
    GameState keep = fresh(), probe = keep;
    GameState s = fresh();
    s.day = 3;
    s.guild_board_count = 1;
    s.guild_active_count = 0;
    memset(&s.guild_board[1], 0, sizeof s.guild_board[1]);
    memset(&s.guild_board[2], 0, sizeof s.guild_board[2]);
    s.guild_board[0] = (GuildContract){11, CT_MAGE, 50, GD_STANDARD, 55, 2, 3, 0, GUILD_CONTRACT_STANDARD, 0, 0};
    s.guild_active_count = 1;
    s.guild_active[0] = (GuildContract){10, CT_WARRIOR, 35, GD_EASY, 35, 1, 3, 4, GUILD_CONTRACT_STANDARD, 0, 0};
    s.guild_next_id = 12;
    CHECK(game_validate(&s));
    char *base = save_text(&s);
    char *v;
#define REJECT(prefix, with, expect)                                                                                   \
    do {                                                                                                               \
        v = replace_line(base, prefix, with);                                                                          \
        CHECK_EQ(load_text(&probe, v), expect);                                                                        \
        CHECK(same_state(&probe, &keep));                                                                              \
        free(v);                                                                                                       \
    } while (0)
    CHECK_EQ(load_text(&probe, base), SAVE_OK);
    CHECK(same_state(&probe, &s) || probe.guild_board_count == 1);
    keep = probe;
    REJECT("gb ", "gb 10 2 50 1 55 2 3 0 0 0 0", SAVE_ERR_VALUE);  /* ID の重複 (受注中のものとも共用) */
    REJECT("gb ", "gb 11 2 50 1 -55 2 3 0 0 0 0", SAVE_ERR_VALUE); /* 負の報酬 */
    REJECT("gb ", "gb 11 2 50 1 55 -2 3 0 0 0 0", SAVE_ERR_VALUE);
    REJECT("gb ", "gb 11 2 50 1 55 2 3 5 0 0 0", SAVE_ERR_VALUE);  /* 掲示板の契約には期限がない */
    REJECT("gb ", "gb 11 99 50 1 55 2 3 0 0 0 0", SAVE_ERR_VALUE); /* 未知の客タイプ */
    REJECT("gb ", "gb 11 2 101 1 55 2 3 0 0 0 0", SAVE_ERR_VALUE); /* FIT が範囲外 */
    REJECT("gb ", "gb 11 2 50 7 55 2 3 0 0 0 0", SAVE_ERR_VALUE);  /* 未知の難易度 */
    REJECT("gb ", "gb 11 2 50 1 55 2 0 0 0 0 0", SAVE_ERR_VALUE);  /* 期間が 0 */
    REJECT("gb ", "gb 0 2 50 1 55 2 3 0 0 0 0", SAVE_ERR_VALUE);   /* ID は正の値 */
    REJECT("ga ", "ga 10 0 35 0 35 1 3 2 0 0 0", SAVE_ERR_VALUE);  /* 期限が今日より前 */
    REJECT("ga ", "ga 10 0 35 0 35 1 3 9 0 0 0", SAVE_ERR_VALUE);  /* 未来に受注されている (9-3+1 > 3 日目) */
    REJECT("ga ", "ga 10 0 35 0 35 1 3 0 0 0 0", SAVE_ERR_VALUE);  /* 受注中の契約に期限がない */
    REJECT("guild ", "guild 12345 11 0 0", SAVE_ERR_VALUE);        /* 次の ID が最大の ID より大きくない */
    REJECT("guild ", "guild 0 12 0 0", SAVE_ERR_VALUE);            /* 乱数状態が死んでいる */
    REJECT("guild ", "guild 12345 12 -1 0", SAVE_ERR_VALUE);       /* 累計が負 */
    REJECT("guild ", "guild 12345 12 0 -1", SAVE_ERR_VALUE);
    REJECT("greport ", "greport -5 0 0", SAVE_ERR_VALUE);
    REJECT("gboard ", "gboard 4", SAVE_ERR_VALUE);   /* 掲示板の上限超過 */
    REJECT("gactive ", "gactive 3", SAVE_ERR_VALUE); /* 受注の上限超過 */
    REJECT("gboard ", "gboard 2", SAVE_ERR_FORMAT);  /* 宣言は 2、実際は 1 */
    REJECT("gboard ", "gboard 0", SAVE_ERR_FORMAT);  /* 宣言は 0、実際は 1 */
    REJECT("gactive ", "gactive 2", SAVE_ERR_FORMAT);
    REJECT("gactive ", "gactive 0", SAVE_ERR_FORMAT);
    REJECT("gb ", NULL, SAVE_ERR_FORMAT); /* レコードが欠けている */
    REJECT("ga ", NULL, SAVE_ERR_FORMAT);
    REJECT("gboard ", NULL, SAVE_ERR_FORMAT); /* セクションが欠けている */
    REJECT("gactive ", NULL, SAVE_ERR_FORMAT);
    REJECT("guild ", NULL, SAVE_ERR_FORMAT);
    REJECT("greport ", NULL, SAVE_ERR_FORMAT);
    REJECT("gb ", "gb 11 2 50", SAVE_ERR_FORMAT); /* レコードが途中で切れている */
    REJECT("guild ", "guild 12345", SAVE_ERR_FORMAT);
    {
        char *dup = add_line_after_header(base, "guild 777 99 0 0");
        CHECK_EQ(load_text(&probe, dup), SAVE_ERR_FORMAT);
        CHECK(same_state(&probe, &keep));
        free(dup);
        dup = add_line_after_header(base, "gboard 1");
        CHECK_EQ(load_text(&probe, dup), SAVE_ERR_FORMAT);
        free(dup);
        dup = add_line_after_header(base, "gb 12 2 50 1 55 2 3 0 0 0 0");
        CHECK(load_text(&probe, dup) != SAVE_OK);
        CHECK(same_state(&probe, &keep));
        free(dup);
    }
    {
        char *extra = malloc(strlen(base) + 64);
        const char *e = strstr(base, "inv "); /* 宣言より gb 行が 1 つ多い */
        size_t k = (size_t)(e - base);
        memcpy(extra, base, k);
        extra[k] = 0;
        strcat(extra, "gb 13 3 50 1 55 2 3 0 0 0 0\n");
        strcat(extra, e);
        CHECK_EQ(load_text(&probe, extra), SAVE_ERR_FORMAT);
        CHECK(same_state(&probe, &keep));
        free(extra);
    }
    /* 契約は、書き手が知らなかった客を指名できない */
    {
        char *lowc = replace_line(base, "counts ", "counts 8 6 5 3");
        char *x = replace_line(lowc, "gb ", "gb 11 5 50 1 55 2 3 0 0 0 0");
        CHECK_EQ(load_text(&probe, x), SAVE_ERR_VALUE);
        free(lowc);
        free(x);
    }
    /* バージョン 1 のファイルの中のギルドのキーは、単なる未知のキー。ファイルは他の v1 セーブと同様に移行される */
    {
        char *junk = add_line_after_header(LEGACY_SAVE, "gb 1 2 3");
        GameState j;
        CHECK_EQ(load_text(&j, junk), SAVE_OK);
        CHECK(game_validate(&j));
        free(junk);
    }
#undef REJECT
    free(base);
    free(t1);
}

static void test_guild_validate(void) {
    puts("guild_validate");
    GameState g = fresh();
    g.day = 3;
    put_contract(&g, CT_MAGE, 50, GD_STANDARD);
    guild_accept(&g, 0);
    CHECK(game_validate(&g));
    CHECK_EQ(g.guild_board_count, 2);
    CHECK_EQ(g.guild_active_count, 1);
    GameState b;
#define BAD(stmt)                                                                                                      \
    do {                                                                                                               \
        b = g;                                                                                                         \
        stmt;                                                                                                          \
        CHECK(!game_validate(&b));                                                                                     \
    } while (0)
    BAD(b.guild_rng = 0);
    BAD(b.guild_board_count = GUILD_OFFER_COUNT + 1);
    BAD(b.guild_board_count = -1);
    BAD(b.guild_active_count = GUILD_ACTIVE_MAX + 1);
    BAD(b.guild_active_count = -1);
    BAD(b.guild_board[0].id = 0);
    BAD(b.guild_board[0].id = -4);
    BAD(b.guild_active[0].id = 0);
    BAD(b.guild_board[1].id = b.guild_board[0].id);  /* 掲示板内の重複 */
    BAD(b.guild_active[0].id = b.guild_board[0].id); /* 配列をまたぐ重複 */
    BAD(b.guild_next_id = b.guild_board[1].id);
    BAD(b.guild_next_id = 0);
    BAD(b.guild_board[0].customer_type = CUST_TYPE_COUNT);
    BAD(b.guild_board[0].customer_type = -1);
    BAD(b.guild_active[0].customer_type = 99);
    BAD(b.guild_board[0].min_fit = 101);
    BAD(b.guild_board[0].min_fit = -1);
    BAD(b.guild_board[0].difficulty = GD_COUNT);
    BAD(b.guild_board[0].difficulty = -1);
    BAD(b.guild_board[0].gold = -1);
    BAD(b.guild_board[0].gold = 100001);
    BAD(b.guild_board[0].rep = -1);
    BAD(b.guild_board[0].rep = BAL.rep_cap + 1);
    BAD(b.guild_board[0].days = 0);
    BAD(b.guild_board[0].days = 100);
    BAD(b.guild_board[0].deadline_day = 4);           /* 掲示板の契約の期限は 0 でなければならない */
    BAD(b.guild_active[0].deadline_day = 0);          /* 受注中の契約には実際の期限が必要 */
    BAD(b.guild_active[0].deadline_day = g.day - 1);  /* PREP ですでに過ぎている */
    BAD(b.guild_active[0].deadline_day = g.day + 50); /* 未来に受注されたことになる */
    BAD(b.guild_done = -1);
    BAD(b.guild_expired = -1);
    BAD(b.report.guild_gold = -1);
    BAD(b.report.guild_done = -1);
    BAD(b.report.guild_expired = -1);
#undef BAD
    b = g;
    b.guild_active[0].deadline_day = g.day; /* 準備中に本日が期限: 問題なし */
    b.guild_active[0].days = 1;
    CHECK(game_validate(&b));
    day_open(&b);
    day_close_early(&b);
    CHECK(game_validate(&b));
    CHECK(content_validate() == NULL);
}

/* ======================================================================
 * ギルドランク、設計図、未解放の装備 / 客
 * ====================================================================== */
static int roundx(double v) {
    return (int)floor(v + 0.5);
}
static GameState at_done(int done) { /* すでに `done` 件の契約を納品した新しいゲーム */
    GameState g = fresh();
    g.guild_done = done;
    g.money = 5000;
    return g;
}

static void test_rank_tables(void) {
    puts("rank_tables");
    CHECK(content_validate() == NULL);
    CHECK_EQ(RANK_COUNT, 5);
    CHECK_EQ(BP_COUNT, 4);
    static const struct {
        const char *name;
        int need;
    } ranks[] = {
        {"Apprentice", 0}, {"Journeyman", 5}, {"Master Craftsman", 15}, {"Grandmaster", 30}, {"Mythic Smith", 50}};
    for (int r = 0; r < RANK_COUNT; r++) {
        CHECK(!strcmp(GUILD_RANKS[r].name, ranks[r].name));
        CHECK_EQ(GUILD_RANKS[r].need_done, ranks[r].need);
        CHECK(!strcmp(guild_rank_name(r), ranks[r].name));
        CHECK_EQ(guild_rank_need(r), ranks[r].need);
    }
    CHECK(!strcmp(guild_rank_name(-1), "?"));
    CHECK(!strcmp(guild_rank_name(RANK_COUNT), "?"));
    CHECK_EQ(guild_rank_need(-1), -1);
    CHECK_EQ(guild_rank_need(99), -1);
    /* 設計図: ID、ランク、装備、客が安定している */
    static const struct {
        int bp, rank, eq, ct;
    } bps[] = {{BP_WARHAMMER, RANK_JOURNEYMAN, EQ_WARHAMMER, CT_KNIGHT},
               {BP_RUNE_BOW, RANK_MASTER, EQ_RUNE_BOW, CT_RANGER},
               {BP_CRYSTAL_ORB, RANK_GRANDMASTER, EQ_CRYSTAL_ORB, CT_ARCANIST},
               {BP_WYRMCLEAVER, RANK_MYTHIC, EQ_WYRMCLEAVER, CT_WYRM_HUNTER}};
    for (int i = 0; i < BP_COUNT; i++) {
        CHECK_EQ(bps[i].bp, i);
        CHECK_EQ(BLUEPRINTS[i].rank, bps[i].rank);
        CHECK_EQ(BLUEPRINTS[i].equip, bps[i].eq);
        CHECK_EQ(BLUEPRINTS[i].customer, bps[i].ct);
        CHECK(BLUEPRINTS[i].name[0] && BLUEPRINTS[i].desc[0] && icon_exists(BLUEPRINTS[i].icon));
        CHECK_EQ(CUSTOMER_DEFS[BLUEPRINTS[i].customer].wants_eq, BLUEPRINTS[i].equip);
    }
    CHECK_EQ(EQ_WARHAMMER, 6);
    CHECK_EQ(EQ_RUNE_BOW, 7);
    CHECK_EQ(EQ_CRYSTAL_ORB, 8);
    CHECK_EQ(CT_KNIGHT, 7);
    CHECK_EQ(CT_RANGER, 8);
    CHECK_EQ(CT_ARCANIST, 9);
    CHECK_EQ(EQ_WYRMCLEAVER, 9); /* 追記のみ: 既存の ID は動かない */
    CHECK_EQ(CT_WYRM_HUNTER, 10);
    CHECK_EQ(RANK_MYTHIC, 4);
    CHECK_EQ(BP_WYRMCLEAVER, 3);
    /* 新しい装備: 基本能力は仕様どおり */
    static const struct {
        int eq;
        const char *name, *icon;
        int v[5];
    } eqs[] = {{EQ_WARHAMMER, "Warhammer", "warhammer", {13, 3, 0, 8, 7}},
               {EQ_RUNE_BOW, "Rune Bow", "runebow", {10, 1, 3, 5, 3}},
               {EQ_CRYSTAL_ORB, "Crystal Orb", "orb", {0, 2, 14, 4, 2}},
               {EQ_WYRMCLEAVER, "Wyrmcleaver", "wyrmcleaver", {14, 2, 1, 9, 5}}};
    for (int i = 0; i < 4; i++) {
        const EquipDef *d = &EQUIPS[eqs[i].eq];
        CHECK(!strcmp(d->name, eqs[i].name));
        CHECK(!strcmp(d->icon, eqs[i].icon));
        CHECK(d->desc[0]);
        int v[5] = {d->atk, d->def, d->mag, d->dur, d->wt};
        for (int k = 0; k < 5; k++) {
            CHECK_EQ(v[k], eqs[i].v[k]);
        }
    }
    /* 新しい客: 求める装備、予算、FIT の範囲は仕様どおりで、重みの合計は 100 */
    static const struct {
        int ct;
        const char *name;
        int eq, bmin, bmax, flo, fhi;
    } cus[] = {{CT_KNIGHT, "Knight", EQ_WARHAMMER, 100, 180, 40, 68},
               {CT_RANGER, "Ranger", EQ_RUNE_BOW, 80, 155, 40, 65},
               {CT_ARCANIST, "Arcanist", EQ_CRYSTAL_ORB, 100, 200, 40, 70},
               {CT_WYRM_HUNTER, "Wyrm Hunter", EQ_WYRMCLEAVER, 120, 210, 50, 75}};
    for (int i = 0; i < 4; i++) {
        const CustomerDef *d = &CUSTOMER_DEFS[cus[i].ct];
        CHECK(!strcmp(d->name, cus[i].name));
        CHECK_EQ(d->wants_eq, cus[i].eq);
        CHECK_EQ(d->budget_min, cus[i].bmin);
        CHECK_EQ(d->budget_max, cus[i].bmax);
        CHECK_EQ(d->fit_min_lo, cus[i].flo);
        CHECK_EQ(d->fit_min_hi, cus[i].fhi);
        CHECK(icon_exists(d->icon));
        CHECK(d->desc[0] && d->priority[0]);
    }
    const CustomerDef *k = &CUSTOMER_DEFS[CT_KNIGHT], *r = &CUSTOMER_DEFS[CT_RANGER], *a = &CUSTOMER_DEFS[CT_ARCANIST];
    CHECK(k->w_atk > 0 && k->w_def > 0 && k->w_dur > 0 && k->w_mag == 0 && k->w_light == 0);
    CHECK(r->w_atk > 0 && r->w_light > 0 && r->w_def == 0 && r->w_mag == 0 && r->w_dur == 0);
    CHECK(a->w_mag > 0 && a->w_dur > 0 && a->w_atk == 0 && a->w_def == 0 && a->w_light == 0);
    const CustomerDef *w = &CUSTOMER_DEFS[CT_WYRM_HUNTER]; /* 仕様どおりの重み: ATK 45 / DUR 35 / 軽さ 20 */
    CHECK_EQ(w->w_atk, 45);
    CHECK_EQ(w->w_def, 0);
    CHECK_EQ(w->w_mag, 0);
    CHECK_EQ(w->w_dur, 35);
    CHECK_EQ(w->w_light, 20);
    for (int c = 0; c < CUST_TYPE_COUNT; c++) {
        const CustomerDef *d = &CUSTOMER_DEFS[c];
        CHECK_EQ(d->w_atk + d->w_def + d->w_mag + d->w_dur + d->w_light, 100);
    }
    /* 出荷時の客は設計図の装備を誰も求めず、新しい客はそれだけを求める: 新しい装備が古い販売を食うことはない */
    for (int c = 0; c < CT_KNIGHT; c++) {
        CHECK(CUSTOMER_DEFS[c].wants_eq < EQ_WARHAMMER);
    }
    for (int c = CT_KNIGHT; c < CUST_TYPE_COUNT; c++) {
        CHECK(CUSTOMER_DEFS[c].wants_eq >= EQ_WARHAMMER);
    }
    /* 4 人の新しい客は、他のどの客とも見た目が異なる */
    for (int x = 0; x < CUST_TYPE_COUNT; x++) {
        for (int y = 0; y < x; y++) {
            CHECK(memcmp(&CUSTOMER_DEFS[x].hat, &CUSTOMER_DEFS[y].hat, sizeof(IconColor)) ||
                  memcmp(&CUSTOMER_DEFS[x].armor, &CUSTOMER_DEFS[y].armor, sizeof(IconColor)));
        }
    }
    /* 設計図装備の客はすべて、妥当なレシピで対応できる: FIT 65 (ギルドの Hard) は鍛冶場 Lv0 で届く */
    for (int c = CT_KNIGHT; c < CUST_TYPE_COUNT; c++) {
        int best = 0, cheapest = 9999;
        for (int m = 0; m < MAT_COUNT; m++) {
            for (int sb = 0; sb < MAT_COUNT; sb++) {
                Equipment e;
                memset(&e, 0, sizeof e);
                e.type = CUSTOMER_DEFS[c].wants_eq;
                e.main = m;
                e.sub = sb;
                equipment_compute_stats(&e, 0);
                int f = equip_fit(c, &e);
                if (f > best) {
                    best = f;
                }
                if (f >= CUSTOMER_DEFS[c].fit_min_hi) {
                    int cost = 2 * MATERIALS[m].base_price + MATERIALS[sb].base_price;
                    if (cost < cheapest) {
                        cheapest = cost;
                    }
                }
            }
        }
        CHECK(best >= GUILD_DIFFS[GD_HARD].min_fit);
        CHECK(best >= CUSTOMER_DEFS[c].fit_min_hi);
        CHECK(cheapest <= 60); /* 贅沢品ではない: 出荷時の客の最安レシピと同程度 */
    }
    /* 検証関数は、壊れた設計図/ランクのテーブルを拒否する */
    GuildRankDef rk[RANK_COUNT];
    BlueprintDef bp[BP_COUNT];
    memcpy(rk, GUILD_RANKS, sizeof rk);
    memcpy(bp, BLUEPRINTS, sizeof bp);
#define VB() content_validate_blueprints(rk, RANK_COUNT, bp, BP_COUNT)
    CHECK(VB() == NULL);
    {
        int o = bp[0].rank;
        bp[0].rank = 0;
        CHECK(VB() != NULL);
        bp[0].rank = RANK_COUNT;
        CHECK(VB() != NULL);
        bp[0].rank = -1;
        CHECK(VB() != NULL);
        bp[0].rank = o;
    }
    {
        int o = bp[0].equip;
        bp[0].equip = EQ_COUNT;
        CHECK(VB() != NULL);
        bp[0].equip = o;
    }
    {
        int o = bp[0].equip;
        bp[0].equip = EQ_RUNE_BOW;
        CHECK(VB() != NULL);
        bp[0].equip = o;
    } /* 客がもう対応していない / 1 つの装備に設計図が 2 つ */
    {
        int o = bp[1].customer;
        bp[1].customer = CT_KNIGHT;
        CHECK(VB() != NULL);
        bp[1].customer = o;
    } /* 客が別の装備を求めている */
    {
        int o = bp[1].customer;
        bp[1].customer = CUST_TYPE_COUNT;
        CHECK(VB() != NULL);
        bp[1].customer = o;
    }
    {
        const char *o = bp[2].icon;
        bp[2].icon = "no-such-icon";
        CHECK(VB() != NULL);
        bp[2].icon = o;
    }
    {
        const char *o = bp[2].name;
        bp[2].name = "A name that is far too long";
        CHECK(VB() != NULL);
        bp[2].name = NULL;
        CHECK(VB() != NULL);
        bp[2].name = o;
    }
    {
        const char *o = bp[2].desc;
        bp[2].desc = "";
        CHECK(VB() != NULL);
        bp[2].desc = o;
    }
    {
        int o = rk[0].need_done;
        rk[0].need_done = 1;
        CHECK(VB() != NULL);
        rk[0].need_done = o;
    }
    {
        int o = rk[2].need_done;
        rk[2].need_done = rk[1].need_done;
        CHECK(VB() != NULL);
        rk[2].need_done = o;
    }
    {
        const char *o = rk[1].name;
        rk[1].name = "";
        CHECK(VB() != NULL);
        rk[1].name = o;
    }
    CHECK(VB() == NULL);
#undef VB
    /* 客の重みの合計は 100 でなければならず、すべての装備に客が必要 */
    MaterialDef m[MAT_COUNT];
    EquipDef e[EQ_COUNT];
    CustomerDef c[CUST_TYPE_COUNT];
    UpgradeDef u[UP_COUNT];
    memcpy(m, MATERIALS, sizeof m);
    memcpy(e, EQUIPS, sizeof e);
    memcpy(c, CUSTOMER_DEFS, sizeof c);
    memcpy(u, UPGRADES, sizeof u);
#define VT() content_validate_tables(m, MAT_COUNT, e, EQ_COUNT, c, CUST_TYPE_COUNT, u, UP_COUNT)
    CHECK(VT() == NULL);
    {
        int o = c[CT_KNIGHT].w_atk;
        c[CT_KNIGHT].w_atk = 41;
        CHECK(VT() != NULL);
        c[CT_KNIGHT].w_atk = o;
    }
    {
        int o = c[CT_ARCANIST].wants_eq;
        c[CT_ARCANIST].wants_eq = EQ_GRIMOIRE;
        CHECK(VT() != NULL);
        c[CT_ARCANIST].wants_eq = o;
    } /* Crystal Orb は誰にも求められなくなる */
    CHECK(VT() == NULL);
#undef VT
}

static void test_rank_derivation(void) {
    puts("rank_derivation");
    static const struct {
        int done, rank, remaining, next_bp;
    } t[] = {{0, 0, 5, BP_WARHAMMER},
             {4, 0, 1, BP_WARHAMMER},
             {5, 1, 10, BP_RUNE_BOW},
             {14, 1, 1, BP_RUNE_BOW},
             {15, 2, 15, BP_CRYSTAL_ORB},
             {29, 2, 1, BP_CRYSTAL_ORB},
             {30, 3, 20, BP_WYRMCLEAVER},
             {31, 3, 19, BP_WYRMCLEAVER},
             {49, 3, 1, BP_WYRMCLEAVER},
             {50, 4, -1, -1},
             {51, 4, -1, -1},
             {1000, 4, -1, -1},
             {-1, 0, 5, BP_WARHAMMER},
             {-100, 0, 5, BP_WARHAMMER}};
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        CHECK_EQ(guild_rank(t[i].done), t[i].rank);
        CHECK_EQ(guild_next_rank_remaining(t[i].done), t[i].remaining);
        CHECK_EQ(guild_next_blueprint(t[i].done), t[i].next_bp);
    }
    /* しきい値ごとの解放状態: 正確に正しい設計図 / 装備 / 客 */
    static const struct {
        int done;
        int bp[BP_COUNT];
    } unl[] = {{0, {0, 0, 0, 0}},  {4, {0, 0, 0, 0}},  {5, {1, 0, 0, 0}},  {14, {1, 0, 0, 0}},
               {15, {1, 1, 0, 0}}, {29, {1, 1, 0, 0}}, {30, {1, 1, 1, 0}}, {49, {1, 1, 1, 0}},
               {50, {1, 1, 1, 1}}, {51, {1, 1, 1, 1}}, {99, {1, 1, 1, 1}}};
    for (size_t i = 0; i < sizeof unl / sizeof *unl; i++) {
        for (int b = 0; b < BP_COUNT; b++) {
            CHECK_EQ(blueprint_unlocked(unl[i].done, b), unl[i].bp[b]);
            CHECK_EQ(equip_unlocked(unl[i].done, BLUEPRINTS[b].equip), unl[i].bp[b]);
            CHECK_EQ(customer_unlocked(unl[i].done, BLUEPRINTS[b].customer), unl[i].bp[b]);
        }
    }
    /* 出荷時のコンテンツは常に利用できる */
    for (int d = 0; d <= 40; d += 5) {
        for (int e = 0; e < EQ_WARHAMMER; e++) {
            CHECK(equip_unlocked(d, e));
        }
        for (int c = 0; c < CT_KNIGHT; c++) {
            CHECK(customer_unlocked(d, c));
        }
    }
    /* 全範囲での整合性: ランクは単調で、解放された設計図が再びロックされることはなく、ランクと解放が一致する */
    int prev_rank = 0, prev_unlocked[BP_COUNT] = {0};
    for (int d = 0; d <= 60; d++) {
        int r = guild_rank(d);
        CHECK(r >= prev_rank && r <= prev_rank + 1);
        prev_rank = r;
        for (int b = 0; b < BP_COUNT; b++) {
            int un = blueprint_unlocked(d, b);
            CHECK(!(prev_unlocked[b] && !un));
            prev_unlocked[b] = un;
            CHECK_EQ(un, d >= GUILD_RANKS[BLUEPRINTS[b].rank].need_done);
            CHECK_EQ(un, equip_unlocked(d, BLUEPRINTS[b].equip));
            CHECK_EQ(un, customer_unlocked(d, BLUEPRINTS[b].customer));
        }
        CHECK_EQ(guild_next_rank_remaining(d) == -1, r == RANK_COUNT - 1);
    }
    /* 無意味な引数にも安全な答えを返す */
    CHECK_EQ(blueprint_unlocked(99, -1), 0);
    CHECK_EQ(blueprint_unlocked(99, BP_COUNT), 0);
    CHECK_EQ(blueprint_unlocked(99, 1000), 0);
    CHECK_EQ(equip_unlocked(99, -1), 0);
    CHECK_EQ(equip_unlocked(99, EQ_COUNT), 0);
    CHECK_EQ(customer_unlocked(99, -1), 0);
    CHECK_EQ(customer_unlocked(99, CUST_TYPE_COUNT), 0);
    /* 失効した契約は無関係: API が受け取るのは納品数だけ */
    GameState g = fresh();
    g.guild_expired = 500;
    CHECK_EQ(guild_rank(g.guild_done), 0);
    CHECK_EQ(craft_equipment(&g, EQ_WARHAMMER, MAT_IRON, MAT_IRON, NULL), R_ERR_LOCKED);
    g.guild_done = 5;
    g.guild_expired = 0;
    CHECK_EQ(guild_rank(g.guild_done), 1);
    g.guild_expired = 500;
    CHECK_EQ(guild_rank(g.guild_done), 1);

    /* ランクは実際の納品に従う: 5 / 15 / 30 / 50 回目の納品でちょうど変わる */
    GameState p = fresh();
    p.money = 100000;
    p.status = ST_SANDBOX; /* サンドボックス: 日数の上限なし */
    int last = 0, ups = 0;
    for (int n = 1; n <= 51; n++) {
        put_contract(&p, CT_WARRIOR, 0, GD_EASY);
        CHECK_EQ(guild_accept(&p, 0), GUILD_OK);
        int it = give_item(&p, EQ_SWORD, MAT_IRON, MAT_IRON);
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&p, 0, it, &o), GUILD_OK);
        CHECK_EQ(p.guild_done, n);
        int r = guild_rank(p.guild_done);
        if (r != last) {
            ups++;
            CHECK(n == 5 || n == 15 || n == 30 || n == 50);
            last = r;
        }
        CHECK_EQ(guild_rank(p.guild_done), n >= 50 ? 4 : n >= 30 ? 3 : n >= 15 ? 2 : n >= 5 ? 1 : 0);
        CHECK(game_validate(&p));
        p.mat_stock[MAT_IRON] =
            0; /* give_item は毎回素材を足す: 50 件の納品でも stock_cap を超えないよう空にしておく */
        p.mat_cost[MAT_IRON] = 0;
        day_open(&p);
        day_close_early(&p);
        day_advance(&p); /* 毎回新しい日: スロット、掲示板、ランクはそれをまたいで残る */
        CHECK_EQ(guild_rank(p.guild_done), last);
    }
    CHECK_EQ(ups, 4);
    CHECK_EQ(guild_rank(p.guild_done), RANK_MYTHIC); /* 51 件を超えても最高ランクのまま */
    CHECK_EQ(guild_next_rank_remaining(p.guild_done), -1);
    CHECK_EQ(guild_next_blueprint(p.guild_done), -1);
}

static void test_craft_lock(void) {
    puts("craft_lock");
    /* 未解放: ルールで拒否される (UI だけでなく)。何にも触れない */
    for (int done = 0; done < 5; done += 4) {
        GameState g = at_done(done);
        g.mat_stock[MAT_IRON] = 9;
        g.mat_cost[MAT_IRON] = 90;
        g.mat_stock[MAT_WOOD] = 9;
        g.mat_cost[MAT_WOOD] = 54;
        GameState before = g;
        CHECK_EQ(can_craft(&g, EQ_WARHAMMER, MAT_IRON, MAT_WOOD), R_ERR_LOCKED);
        CHECK_EQ(craft_equipment(&g, EQ_WARHAMMER, MAT_IRON, MAT_WOOD, NULL), R_ERR_LOCKED);
        int idx = 77;
        CHECK_EQ(craft_equipment(&g, EQ_WARHAMMER, MAT_IRON, MAT_WOOD, &idx), R_ERR_LOCKED);
        CHECK_EQ(idx, 77);
        CHECK(same_state(&g, &before)); /* 素材、在庫、所持金、乱数、すべて */
        CHECK_EQ(g.rng, before.rng);
        CHECK_EQ(g.mat_stock[MAT_IRON], 9);
        CHECK_EQ(g.inv_count, 0);
        CHECK_EQ(can_craft(&g, EQ_RUNE_BOW, MAT_IRON, MAT_WOOD), R_ERR_LOCKED);
        CHECK_EQ(can_craft(&g, EQ_CRYSTAL_ORB, MAT_IRON, MAT_WOOD), R_ERR_LOCKED);
        CHECK_EQ(can_craft(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_WOOD), R_ERR_LOCKED);
        CHECK_EQ(can_craft(&g, EQ_SWORD, MAT_IRON, MAT_WOOD), R_OK); /* 出荷時の装備は影響を受けない */
    }
    GameState bad = at_done(0);
    CHECK_EQ(can_craft(&bad, -1, MAT_IRON, MAT_IRON), R_ERR_RANGE);
    CHECK_EQ(can_craft(&bad, EQ_COUNT, MAT_IRON, MAT_IRON), R_ERR_RANGE);
    GameState open = at_done(0);
    day_open(&open);
    CHECK_EQ(can_craft(&open, EQ_WARHAMMER, MAT_IRON, MAT_IRON), R_ERR_PHASE); /* まずフェーズを見る */
    /* 各設計図は、それぞれのしきい値で、それぞれの装備だけを開く */
    static const struct {
        int eq, need;
    } gate[] = {{EQ_WARHAMMER, 5}, {EQ_RUNE_BOW, 15}, {EQ_CRYSTAL_ORB, 30}, {EQ_WYRMCLEAVER, 50}};
    for (int i = 0; i < 4; i++) {
        for (int done = gate[i].need - 1; done <= gate[i].need; done++) {
            GameState g = at_done(done);
            g.mat_stock[MAT_IRON] = 3;
            g.mat_cost[MAT_IRON] = 30;
            CHECK_EQ(can_craft(&g, gate[i].eq, MAT_IRON, MAT_IRON), done >= gate[i].need ? R_OK : R_ERR_LOCKED);
        }
    }
    /* 解放後: 通常のルール (main 2 + sub 1)、通常の能力の混合式、鍛冶場と価格の計算式 */
    GameState g = at_done(5);
    g.mat_stock[MAT_IRON] = 5;
    g.mat_cost[MAT_IRON] = 50;
    g.mat_stock[MAT_WOOD] = 4;
    g.mat_cost[MAT_WOOD] = 24;
    int idx = -1;
    pin_craft(&g, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&g, EQ_WARHAMMER, MAT_IRON, MAT_WOOD, &idx), R_OK);
    CHECK_EQ(idx, 0);
    CHECK_EQ(g.inv_count, 1);
    CHECK_EQ(g.mat_stock[MAT_IRON], 3);
    CHECK_EQ(g.mat_stock[MAT_WOOD], 3); /* ちょうど 2 + 1 */
    const Equipment *w = &g.inv[0];
    CHECK_EQ(w->type, EQ_WARHAMMER);
    CHECK_EQ(w->atk, roundx(13 + (2.0 * 8 + 2) / 3));
    CHECK_EQ(w->def, roundx(3 + (2.0 * 8 + 3) / 3));
    CHECK_EQ(w->mag, roundx(0 + (2.0 * 0 + 2) / 3));
    CHECK_EQ(w->dur, roundx(8 + (2.0 * 10 + 3) / 3));
    CHECK_EQ(w->wt, roundx(7 + (2.0 * 6 + 1) / 3));
    CHECK_EQ(w->atk, 19);
    CHECK_EQ(w->def, 9);
    CHECK_EQ(w->mag, 1);
    CHECK_EQ(w->dur, 16);
    CHECK_EQ(w->wt, 11);
    CHECK_EQ(w->cost, 2 * 10 + 6);
    CHECK(w->rec_price > w->cost);
    CHECK_EQ(w->price, w->rec_price);
    Equipment ref = *w;
    equipment_price(&ref);
    CHECK_EQ(ref.rec_price, w->rec_price); /* 他とまったく同じ価格式 */
    /* 鍛冶場のアップグレードは他の装備と同様に拡大する (重量を除く) */
    GameState f = at_done(15);
    f.up_level[UP_FORGE] = 2;
    f.mat_stock[MAT_STEEL] = 3;
    f.mat_cost[MAT_STEEL] = 36;
    pin_craft(&f, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&f, EQ_RUNE_BOW, MAT_STEEL, MAT_STEEL, &idx), R_OK);
    double mult = upgrade_effect(UP_FORGE, 2) / 1000.0;
    CHECK_EQ(f.inv[idx].atk, roundx((10 + 10.0) * mult));
    CHECK_EQ(f.inv[idx].dur, roundx((5 + 11.0) * mult));
    CHECK_EQ(f.inv[idx].wt, 3 + 6);
    GameState o = at_done(30);
    o.mat_stock[MAT_SILVER] = 3;
    o.mat_cost[MAT_SILVER] = 39;
    pin_craft(&o, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&o, EQ_CRYSTAL_ORB, MAT_SILVER, MAT_SILVER, &idx), R_OK);
    CHECK_EQ(o.inv[idx].mag, 14 + 8);
    CHECK_EQ(o.inv[idx].def, 2 + 4);
    CHECK_EQ(o.inv[idx].atk, 3);
    CHECK_EQ(o.inv[idx].dur, 4 + 5);
    CHECK_EQ(o.inv[idx].wt, 4);
    /* Wyrmcleaver: 49 件では未解放、50 件でちょうど解放。通常の混合式で基本能力が決まる (専用のクラフト処理はない) */
    GameState wy = at_done(50);
    wy.mat_stock[MAT_STEEL] = 3;
    wy.mat_cost[MAT_STEEL] = 36;
    pin_craft(&wy, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&wy, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL, &idx), R_OK);
    CHECK_EQ(wy.inv[idx].type, EQ_WYRMCLEAVER);
    CHECK_EQ(wy.inv[idx].atk, 14 + 10);
    CHECK_EQ(wy.inv[idx].def, 2 + 8);
    CHECK_EQ(wy.inv[idx].mag, 1);
    CHECK_EQ(wy.inv[idx].dur, 9 + 11);
    CHECK_EQ(wy.inv[idx].wt, 5 + 6);
    CHECK_EQ(wy.inv[idx].quality, QUALITY_COMMON);
    GameState wy49 = at_done(49);
    wy49.mat_stock[MAT_STEEL] = 3;
    wy49.mat_cost[MAT_STEEL] = 36;
    GameState wy49_before = wy49;
    CHECK_EQ(craft_equipment(&wy49, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL, &idx), R_ERR_LOCKED);
    CHECK(same_state(&wy49, &wy49_before)); /* 失敗しても何も変わらない (乱数も消費しない) */
    /* 一度解放されたら解放されたまま (失効した契約や日数で再ロックされない) */
    g.guild_expired = 99;
    for (int i = 0; i < 3; i++) {
        pass_day(&g);
    }
    g.mat_stock[MAT_IRON] = 0;
    g.mat_cost[MAT_IRON] = 0;
    CHECK_EQ(can_craft(&g, EQ_WARHAMMER, MAT_IRON, MAT_IRON), R_ERR_MATERIAL); /* 足りないのは素材だけ */
    g.mat_stock[MAT_IRON] = 3;
    g.mat_cost[MAT_IRON] = 30;
    CHECK_EQ(can_craft(&g, EQ_WARHAMMER, MAT_IRON, MAT_IRON), R_OK);
    /* 出荷時の装備: 結果は設計図が存在する前と同じ */
    GameState s = at_done(0);
    s.mat_stock[MAT_IRON] = 3;
    s.mat_cost[MAT_IRON] = 30;
    pin_craft(&s, QUALITY_COMMON);
    CHECK_EQ(craft_equipment(&s, EQ_SWORD, MAT_IRON, MAT_IRON, &idx), R_OK);
    CHECK_EQ(s.inv[idx].atk, 18);
    CHECK_EQ(s.inv[idx].def, 9);
    CHECK_EQ(s.inv[idx].mag, 0);
    CHECK_EQ(s.inv[idx].dur, 16);
    CHECK_EQ(s.inv[idx].wt, 10);
    CHECK_EQ(s.inv[idx].rec_price, 74);
    /* 未解放の装備は有効な状態には存在しえない (そう主張するセーブは拒否される。セーブのテストを参照) */
    GameState bogus = at_done(0);
    bogus.inv[0] = w ? *w : bogus.inv[0];
    bogus.inv_count = 1;
    CHECK(!game_validate(&bogus));
    bogus.guild_done = 5;
    CHECK(game_validate(&bogus));
}

static void test_rank_customers(void) {
    puts("rank_customers");
    /* ランク 1 (何も解放されていない): 設計図が存在する前と同じ 7 タイプに対する、客ごとの同じ 1 回の抽選 */
    for (uint32_t seed = 1; seed <= 50; seed++) {
        uint32_t a = seed * 2654435761u | 1, b = a;
        Customer got, want;
        for (int i = 0; i < 300; i++) {
            customer_generate(&a, 10 + (int)(seed % 60), 0, &got);
            int t = rng_range(&b, 0, CT_SCHOLAR); /* 設計図以前のアルゴリズムをそのまま書き出したもの */
            const CustomerDef *d = &CUSTOMER_DEFS[t];
            int budget = rng_range(&b, d->budget_min, d->budget_max);
            budget = budget * (BAL.budget_rep_base_pm + BAL.budget_rep_per_pm * (10 + (int)(seed % 60))) / 1000;
            want.type = t;
            want.budget = budget < 1 ? 1 : budget;
            want.min_fit = rng_range(&b, d->fit_min_lo, d->fit_min_hi);
            want.tol_pm = rng_range(&b, d->tol_lo, d->tol_hi);
            want.patience = BAL.customer_patience;
            CHECK(!memcmp(&got, &want, sizeof got));
            CHECK_EQ(a, b); /* 客も乱数の消費も同一 */
            CHECK(got.type <= CT_SCHOLAR);
        }
    }
    /* 既知のシードで開いた日の列は、設計図以前のリリースが生成したものと等しい */
    static const struct {
        uint32_t seed;
        int board0[6];
        int queues[15];
        int board3[6];
        uint32_t rng;
    } pin[] = {{1,
                {
                    4,
                    3,
                    5,
                    0,
                    1,
                    0,
                },
                {
                    1,
                    6,
                    1,
                    -1,
                    -1,
                    4,
                    5,
                    6,
                    -1,
                    -1,
                    5,
                    6,
                    5,
                    -1,
                    -1,
                },
                {
                    1,
                    4,
                    6,
                    0,
                    1,
                    1,
                },
                932611783},
               {2,
                {
                    3,
                    4,
                    5,
                    1,
                    0,
                    1,
                },
                {
                    1,
                    0,
                    2,
                    -1,
                    -1,
                    4,
                    6,
                    4,
                    -1,
                    -1,
                    1,
                    3,
                    1,
                    -1,
                    -1,
                },
                {
                    1,
                    4,
                    2,
                    0,
                    1,
                    0,
                },
                1112633538},
               {3,
                {
                    1,
                    2,
                    0,
                    0,
                    1,
                    0,
                },
                {
                    2,
                    6,
                    6,
                    -1,
                    -1,
                    0,
                    1,
                    6,
                    -1,
                    -1,
                    1,
                    0,
                    4,
                    -1,
                    -1,
                },
                {
                    4,
                    5,
                    1,
                    0,
                    0,
                    0,
                },
                1976038917},
               {777,
                {
                    2,
                    1,
                    3,
                    0,
                    0,
                    0,
                },
                {
                    5,
                    1,
                    0,
                    -1,
                    -1,
                    6,
                    4,
                    2,
                    -1,
                    -1,
                    5,
                    5,
                    3,
                    -1,
                    -1,
                },
                {
                    4,
                    5,
                    0,
                    1,
                    0,
                    0,
                },
                2689687984u},
               {4242,
                {
                    2,
                    1,
                    5,
                    0,
                    0,
                    0,
                },
                {
                    0,
                    6,
                    1,
                    -1,
                    -1,
                    6,
                    2,
                    2,
                    -1,
                    -1,
                    4,
                    4,
                    1,
                    -1,
                    -1,
                },
                {
                    5,
                    3,
                    1,
                    0,
                    0,
                    0,
                },
                589638698}};
    for (size_t k = 0; k < sizeof pin / sizeof *pin; k++) {
        GameState g;
        game_init(&g, pin[k].seed);
        for (int i = 0; i < 3; i++) {
            CHECK_EQ(g.guild_board[i].customer_type, pin[k].board0[i]);
            CHECK_EQ(g.guild_board[i].difficulty, pin[k].board0[3 + i]);
        }
        for (int d = 0; d < 3; d++) {
            day_open(&g);
            for (int i = 0; i < 5; i++) {
                CHECK_EQ(i < g.queue_count ? g.queue[i].type : -1, pin[k].queues[d * 5 + i]);
            }
            while (g.phase == PH_OPEN) {
                customer_decline(&g);
            }
            day_advance(&g);
        }
        for (int i = 0; i < 3; i++) {
            CHECK_EQ(g.guild_board[i].customer_type, pin[k].board3[i]);
            CHECK_EQ(g.guild_board[i].difficulty, pin[k].board3[3 + i]);
        }
        CHECK_EQ(g.rng, pin[k].rng);
    }
    /* しきい値ごとに、客タイプがちょうど 1 つプールに加わる。未解放のものは決して現れない */
    static const int gate[] = {0, 5, 15, 30};
    for (int step = 0; step < 4; step++) {
        int done = gate[step], seen[CUST_TYPE_COUNT] = {0};
        uint32_t rng = 4711 + step;
        for (int i = 0; i < 6000; i++) {
            Customer c;
            customer_generate(&rng, 50, done, &c);
            CHECK(customer_unlocked(done, c.type));
            CHECK(equip_unlocked(done, CUSTOMER_DEFS[c.type].wants_eq));
            seen[c.type]++;
        }
        for (int t = 0; t < CUST_TYPE_COUNT; t++) {
            int should = customer_unlocked(done, t);
            CHECK_EQ(seen[t] > 0, should);
            if (should) {
                CHECK(seen[t] > 6000 / (CT_SCHOLAR + 1 + step) / 2);
            }
        }
        CHECK_EQ(seen[CT_KNIGHT] > 0, step >= 1);
        CHECK_EQ(seen[CT_RANGER] > 0, step >= 2);
        CHECK_EQ(seen[CT_ARCANIST] > 0, step >= 3);
    }
    for (int d = 0; d <= 35; d++) { /* どのランクでも、未解放の装備の客は現れない */
        uint32_t rng = 99 + d;
        for (int i = 0; i < 400; i++) {
            Customer c;
            customer_generate(&rng, 30, d, &c);
            CHECK(equip_unlocked(d, CUSTOMER_DEFS[c.type].wants_eq));
        }
    }
    /* ランクアップで待ち行列は作り直されない。1 日あたりの乱数の抽選回数はランクに依存しない */
    GameState a = fresh(), b = fresh();
    b.guild_done = 4;
    day_open(&a);
    day_open(&b);
    CHECK_EQ(a.rng, b.rng);
    CHECK_EQ(a.queue_count, b.queue_count);
    CHECK(!memcmp(a.queue, b.queue, sizeof a.queue));
    GameState q = fresh();
    day_open(&q);
    GameState qb = q;
    q.guild_done = 30;
    CHECK(!memcmp(q.queue, qb.queue, sizeof q.queue));
    CHECK_EQ(q.queue_pos, qb.queue_pos);
    CHECK_EQ(q.rng, qb.rng);
    CHECK(game_validate(&q));
    /* ランクアップしたキャンペーンは、していないものとまったく同じ数の乱数を引く (同じ市場、同じすべて) */
    GameState lo = fresh(), hi = fresh();
    hi.guild_done = 30;
    for (int day = 0; day < 10; day++) {
        pass_day(&lo);
        pass_day(&hi);
        CHECK_EQ(lo.rng, hi.rng);
        CHECK(!memcmp(lo.price, hi.price, sizeof lo.price));
        CHECK(game_validate(&hi));
    }
    /* 新しい装備は通常の流れでその客に売れ、その客にしか売れない */
    GameState g = at_done(30);
    g.up_level[UP_FORGE] = 1;
    int hammer = give_item(&g, EQ_WARHAMMER, MAT_IRON, MAT_STEEL),
        bow = give_item(&g, EQ_RUNE_BOW, MAT_OBSIDIAN, MAT_COPPER),
        orb = give_item(&g, EQ_CRYSTAL_ORB, MAT_SILVER, MAT_SILVER);
    CHECK_EQ(g.inv_count, 3);
    static const struct {
        int ct, eq;
    } pair[] = {{CT_KNIGHT, EQ_WARHAMMER}, {CT_RANGER, EQ_RUNE_BOW}, {CT_ARCANIST, EQ_CRYSTAL_ORB}};
    for (int k = 0; k < 3; k++) {
        GameState h = g;
        int at = -1;
        for (int i = 0; i < h.inv_count; i++) {
            if (h.inv[i].type == pair[k].eq) {
                at = i;
            }
        }
        Customer c = mk_customer(pair[k].ct, 400, 20, 1500);
        Equipment e = h.inv[at];
        SaleJudgement j = judge_sale(&c, &e, e.price);
        CHECK_EQ(j.fit, equip_fit(pair[k].ct, &e)); /* FIT と判定は通常の関数から得られる */
        CHECK_EQ(j.result, SALE_OK);
        force_open(&h, c);
        int money = h.money;
        SaleOutcome so;
        CHECK_EQ(customer_offer(&h, at, &so), R_OK);
        CHECK_EQ(so.sold, 1);
        CHECK(h.money > money);
        CHECK_EQ(h.total_sold, 1);
        CHECK(game_validate(&h));
        /* 他の客はそれを求めない (既存の WRONG_TYPE ルール) */
        GameState w = g;
        force_open(&w, mk_customer(pair[k].ct == CT_KNIGHT ? CT_WARRIOR : CT_KNIGHT, 400, 20, 1500));
        CHECK_EQ(customer_offer(&w, at, &so), R_OK);
        CHECK_EQ(so.result, SALE_WRONG_TYPE);
        CHECK_EQ(so.sold, 0);
    }
    (void)hammer;
    (void)bow;
    (void)orb;
    /* 剣を持った Knight: 種類が違う。ウォーハンマーを持った Warrior: 種類が違う */
    GameState x = at_done(30);
    give_item(&x, EQ_SWORD, MAT_IRON, MAT_IRON);
    Customer kn = mk_customer(CT_KNIGHT, 400, 20, 1500);
    CHECK_EQ(judge_sale(&kn, &x.inv[0], 50).result, SALE_WRONG_TYPE);
}

static void test_rank_guild(void) {
    puts("rank_guild");
    /* 掲示板は解放済みの客だけを求める。しきい値ごとに 1 つ増える */
    static const int gate[] = {0, 5, 15, 30};
    for (int step = 0; step < 4; step++) {
        GameState g = fresh();
        g.guild_done = gate[step];
        int seen[CUST_TYPE_COUNT] = {0};
        for (int round = 0; round < 3000; round++) {
            guild_refresh_board(&g);
            int mask = 0;
            for (int i = 0; i < g.guild_board_count; i++) {
                int ct = g.guild_board[i].customer_type;
                CHECK(customer_unlocked(g.guild_done, ct));
                CHECK(!(mask & (1 << ct)));
                mask |= 1 << ct;
                seen[ct]++; /* 重複なし、解放済み */
            }
            CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);
        }
        for (int t = 0; t < CUST_TYPE_COUNT; t++) {
            CHECK_EQ(seen[t] > 0, customer_unlocked(gate[step], t));
        }
        CHECK(game_validate(&g));
    }
    /* 既知のシードの最初の掲示板は、設計図以前のリリースが生成したもの (上のピン留めを参照) */
    /* ランクアップは、掲示済みの掲示板、受注中の契約、どの乱数も書き換えない */
    GameState g = fresh();
    g.guild_done = 4;
    g.money = 9000;
    guild_refresh_board(&g);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    int it = give_item(&g, CUSTOMER_DEFS[g.guild_active[0].customer_type].wants_eq, MAT_STEEL, MAT_STEEL);
    g.guild_active[0].min_fit = 0;
    GameState before = g;
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
    CHECK_EQ(g.guild_done, 5);
    CHECK_EQ(guild_rank(g.guild_done), 1);
    CHECK_EQ(g.guild_board_count, before.guild_board_count);
    CHECK(!memcmp(g.guild_board, before.guild_board, sizeof g.guild_board));
    CHECK_EQ(g.guild_rng, before.guild_rng);
    CHECK_EQ(g.guild_next_id, before.guild_next_id);
    CHECK_EQ(g.rng, before.rng);
    /* 2 つ目の受注中の契約はランクアップを経ても手つかずで残り、その後も納品できる */
    GameState h = fresh();
    h.guild_done = 4;
    h.money = 9000;
    put_contract(&h, CT_WARRIOR, 35, GD_EASY);
    guild_accept(&h, 0);
    put_contract(&h, CT_MAGE, 35, GD_STANDARD);
    guild_accept(&h, 0);
    GuildContract held1 = h.guild_active[1];
    int sw = give_item(&h, EQ_SWORD, MAT_IRON, MAT_IRON);
    CHECK_EQ(guild_deliver(&h, 0, sw, &o), GUILD_OK);
    CHECK_EQ(h.guild_done, 5);
    CHECK_EQ(h.guild_active_count, 1);
    CHECK(!memcmp(&h.guild_active[0], &held1, sizeof held1));
    int st = give_item(&h, EQ_STAFF, MAT_MANA, MAT_MANA);
    CHECK_EQ(guild_deliver(&h, 0, st, &o), GUILD_OK);
    CHECK_EQ(h.guild_done, 6);

    /* 新しい客の契約: 解放前は不可、解放後は受注でき、新しい装備で納品できる */
    GameState z = fresh();
    put_contract(&z, CT_KNIGHT, 40, GD_EASY);
    CHECK(!game_validate(&z)); /* 掲示板に未解放の客 */
    z = at_done(5);
    put_contract(&z, CT_KNIGHT, 40, GD_EASY);
    CHECK(game_validate(&z));
    z = at_done(5);
    put_contract(&z, CT_RANGER, 40, GD_EASY);
    CHECK(!game_validate(&z)); /* Ranger にはランク 3 が必要 */
    z = at_done(15);
    put_contract(&z, CT_RANGER, 40, GD_EASY);
    CHECK(game_validate(&z));
    z = at_done(15);
    put_contract(&z, CT_ARCANIST, 40, GD_EASY);
    CHECK(!game_validate(&z));
    z = at_done(30);
    put_contract(&z, CT_ARCANIST, 40, GD_EASY);
    CHECK(game_validate(&z));
    z = fresh();
    z.guild_active[0] =
        (GuildContract){z.guild_next_id++, CT_KNIGHT, 40, GD_EASY, 35, 1, 3, 3, GUILD_CONTRACT_STANDARD, 0, 0};
    z.guild_active_count = 1;
    CHECK(!game_validate(&z));
    for (int d = 0; d < GD_COUNT; d++) { /* すべての新しい客について、鍛冶場 Lv0 ですべての難易度が納品可能 */
        for (int ct = CT_KNIGHT; ct < CUST_TYPE_COUNT; ct++) {
            GameState y = at_done(50); /* Wyrm Hunter まで解放済み */
            put_contract(&y, ct, GUILD_DIFFS[d].min_fit, d);
            CHECK_EQ(guild_accept(&y, 0), GUILD_OK);
            int bm = -1, bs = -1;
            for (int m = 0; m < MAT_COUNT; m++) {
                for (int sb = 0; sb < MAT_COUNT; sb++) {
                    Equipment e;
                    memset(&e, 0, sizeof e);
                    e.type = CUSTOMER_DEFS[ct].wants_eq;
                    e.main = m;
                    e.sub = sb;
                    equipment_compute_stats(&e, 0);
                    if (equip_fit(ct, &e) >= GUILD_DIFFS[d].min_fit && bm < 0) {
                        bm = m;
                        bs = sb;
                    }
                }
            }
            CHECK(bm >= 0);
            int item = give_item(&y, CUSTOMER_DEFS[ct].wants_eq, bm, bs);
            CHECK_EQ(guild_deliver(&y, 0, item, &o), GUILD_OK);
            CHECK_EQ(o.gold, GUILD_DIFFS[d].gold);
            CHECK_EQ(y.guild_done, 51);
            CHECK(game_validate(&y));
        }
    }
    { /* 新しい装備を誤った客に出すと、契約のルールで拒否される */
        GameState y = at_done(30);
        put_contract(&y, CT_KNIGHT, 40, GD_EASY);
        guild_accept(&y, 0);
        int orb2 = give_item(&y, EQ_CRYSTAL_ORB, MAT_SILVER, MAT_SILVER);
        GameState keep = y;
        CHECK_EQ(guild_deliver(&y, 0, orb2, &o), GUILD_ERR_WRONG_TYPE);
        CHECK(same_state(&y, &keep));
    }
    /* ランクアップ後の *翌朝* の掲示板には新しい客が含まれうる (そして未解放の客は依然としていない) */
    int knight_boards = 0, bad = 0;
    for (uint32_t seed = 1; seed <= 120; seed++) {
        GameState y;
        game_init(&y, seed);
        y.guild_done = 4;
        y.money = 9000;
        int posted_done = 4; /* 現在の掲示板が掲示された時点のランク */
        for (int round = 0; round < 6; round++) {
            for (int i = 0; i < y.guild_board_count; i++) {
                if (y.guild_board[i].customer_type == CT_KNIGHT && posted_done < 5) {
                    bad++; /* 解放前は Knight なし */
                }
            }
            while (y.guild_board_count > 0 && y.guild_active_count < GUILD_ACTIVE_MAX) {
                guild_accept(&y, 0);
            }
            for (int a = y.guild_active_count - 1; a >= 0; a--) {
                y.guild_active[a].min_fit = 0;
                int item = give_item(&y, CUSTOMER_DEFS[y.guild_active[a].customer_type].wants_eq, MAT_STEEL, MAT_STEEL);
                GuildOutcome oo;
                guild_deliver(&y, a, item, &oo);
            }
            pass_day(&y);
            posted_done = y.guild_done;
            CHECK(game_validate(&y));
            for (int i = 0; i < y.guild_board_count; i++) {
                CHECK((y.guild_board[i].customer_type != CT_RANGER && y.guild_board[i].customer_type != CT_ARCANIST) ||
                      y.guild_done >= 15);
                if (y.guild_board[i].customer_type == CT_KNIGHT) {
                    knight_boards++;
                }
            }
        }
    }
    CHECK_EQ(bad, 0);
    CHECK(knight_boards > 0);
    /* ランクアップはゲームの乱数を乱さない: ギルドを使うプレイヤーと放置するプレイヤーの市場と客は歩調が揃ったまま */
    GameState idle = fresh(), busy = fresh();
    busy.money = 50000;
    int day_ranked = -1;
    for (int day = 1; day <= 12; day++) {
        while (busy.guild_board_count > 0 && busy.guild_active_count < GUILD_ACTIVE_MAX) {
            guild_accept(&busy, 0);
        }
        for (int a = busy.guild_active_count - 1; a >= 0; a--) {
            busy.guild_active[a].min_fit = 0;
            int item =
                give_item(&busy, CUSTOMER_DEFS[busy.guild_active[a].customer_type].wants_eq, MAT_STEEL, MAT_STEEL);
            GuildOutcome oo;
            guild_deliver(&busy, a, item, &oo);
        }
        if (day_ranked < 0 && guild_rank(busy.guild_done) > 0) {
            day_ranked = day;
        }
        pass_day(&idle);
        pass_day(&busy);
        CHECK_EQ(idle.rng, busy.rng);
        CHECK(!memcmp(idle.price, busy.price, sizeof idle.price));
        CHECK(game_validate(&busy));
    }
    CHECK(day_ranked > 0 && day_ranked <= 12);
}

static void test_rank_save(void) {
    puts("rank_save");
    /* ランク 3 (Master Craftsman) で、設計図装備とその客を持つセーブ */
    GameState g = at_done(15);
    g.guild_expired = 7;
    int w = give_item(&g, EQ_WARHAMMER, MAT_IRON, MAT_STEEL), b = give_item(&g, EQ_RUNE_BOW, MAT_OBSIDIAN, MAT_COPPER);
    (void)w;
    (void)b;
    give_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
    force_open(&g, mk_customer(CT_RANGER, 120, 45, 1100));
    CHECK(game_validate(&g));
    char *t = save_text(&g);
    CHECK(!strncmp(t, "LOOTSMITH_SAVE 4\n", 17));
    CHECK_EQ(SAVE_VERSION, 4);
    CHECK(strstr(t, "\ncounts 8 10 11 3\n") != NULL);
    CHECK(strstr(t, "rank") == NULL && strstr(t, "blueprint") == NULL &&
          strstr(t, "unlock") == NULL); /* 何も二重には保存されない */
    GameState r;
    CHECK_EQ(load_text(&r, t), SAVE_OK);
    CHECK_EQ(r.guild_done, 15);
    CHECK_EQ(guild_rank(r.guild_done), RANK_MASTER);
    CHECK(blueprint_unlocked(r.guild_done, BP_WARHAMMER) && blueprint_unlocked(r.guild_done, BP_RUNE_BOW) &&
          !blueprint_unlocked(r.guild_done, BP_CRYSTAL_ORB));
    CHECK(equip_unlocked(r.guild_done, EQ_WARHAMMER) && equip_unlocked(r.guild_done, EQ_RUNE_BOW) &&
          !equip_unlocked(r.guild_done, EQ_CRYSTAL_ORB));
    CHECK_EQ(r.inv_count, 3);
    CHECK(!memcmp(r.inv, g.inv, sizeof r.inv)); /* タイプとすべての能力値が保たれる */
    CHECK_EQ(r.inv[0].type, EQ_WARHAMMER);
    CHECK_EQ(r.inv[1].type, EQ_RUNE_BOW);
    CHECK(r.queue_count >= 1);
    CHECK_EQ(r.queue[0].type, CT_RANGER);
    CHECK_EQ(r.guild_expired, 7);
    {
        char *t2 = save_text(&r);
        CHECK(!strcmp(t, t2));
        free(t2);
    }
    {
        GameState prep = r;
        prep.phase = PH_PREP;
        prep.queue_count = 0;
        prep.queue_pos = 0;
        CHECK_EQ(can_craft(&prep, EQ_CRYSTAL_ORB, MAT_IRON, MAT_IRON), R_ERR_LOCKED);
    } /* 再読み込み後もロックされたまま */
    /* しきい値ごとに、それぞれのランクと解放セットが復元される */
    static const int done[] = {0, 4, 5, 14, 15, 29, 30, 120};
    for (size_t i = 0; i < sizeof done / sizeof *done; i++) {
        GameState x = at_done(done[i]), y;
        char *tx = save_text(&x);
        CHECK_EQ(load_text(&y, tx), SAVE_OK);
        CHECK_EQ(guild_rank(y.guild_done), guild_rank(done[i]));
        for (int bp = 0; bp < BP_COUNT; bp++) {
            CHECK_EQ(blueprint_unlocked(y.guild_done, bp), blueprint_unlocked(done[i], bp));
        }
        free(tx);
    }
    free(t);

    /* バージョン 1 (ギルド以前): すべて保たれ、新しいコンテンツが突然現れることはない */
    GameState l;
    CHECK_EQ(load_text(&l, LEGACY_SAVE), SAVE_OK);
    CHECK_EQ(l.guild_done, 0);
    CHECK_EQ(guild_rank(l.guild_done), RANK_APPRENTICE);
    CHECK_EQ(guild_next_blueprint(l.guild_done), BP_WARHAMMER);
    CHECK_EQ(l.money, 502);
    CHECK_EQ(l.rep, 10);
    CHECK_EQ(l.inv_count, 3);
    CHECK_EQ(l.queue_count, 3);
    CHECK_EQ(l.up_level[UP_FORGE], 1);
    CHECK_EQ(l.price[MAT_IRON], 9);
    CHECK_EQ(l.price[MAT_MANA], 19);
    CHECK_EQ(l.mat_stock[MAT_MANA], 2);
    CHECK_EQ(l.report.money_start, 250);
    for (int i = 0; i < l.queue_count; i++) {
        CHECK(l.queue[i].type <= CT_GUARDIAN);
    }
    for (int i = 0; i < l.guild_board_count; i++) {
        CHECK(l.guild_board[i].customer_type <= CT_SCHOLAR);
    }
    CHECK_EQ(can_craft(&l, EQ_WARHAMMER, MAT_IRON, MAT_IRON),
             R_ERR_PHASE); /* 日中: どちらにせよ、クラフトは次の準備フェーズまで待つ */
    day_close_early(&l);
    CHECK_EQ(day_advance(&l), R_OK);
    CHECK_EQ(can_craft(&l, EQ_WARHAMMER, MAT_IRON, MAT_IRON), R_ERR_LOCKED);
    for (int d = 0; d < 8; d++) {
        day_open(&l);
        for (int i = 0; i < l.queue_count; i++) {
            CHECK(l.queue[i].type <= CT_SCHOLAR);
        }
        while (l.phase == PH_OPEN) {
            customer_decline(&l);
        }
        day_advance(&l);
        CHECK(game_validate(&l));
    }
    /* 設計図以前のビルドの v2 セーブ (counts 7 6 7 3) が読み込め、ランクは guild_done から導出される */
    GameState old = fresh();
    old.guild_done = 31;
    give_item(&old, EQ_GRIMOIRE, MAT_SILVER, MAT_SILVER);
    char *to = save_text(&old);
    char *o2 = as_older_build(to, "counts 7 6 7 3");
    GameState ol;
    CHECK_EQ(load_text(&ol, o2), SAVE_OK);
    CHECK_EQ(guild_rank(ol.guild_done), RANK_GRANDMASTER);
    CHECK(equip_unlocked(ol.guild_done, EQ_CRYSTAL_ORB));
    CHECK_EQ(ol.inv[0].type, EQ_GRIMOIRE);
    CHECK(game_validate(&ol));
    free(to);
    free(o2);

    /* 壊れたデータは拒否され、出力先には触れない */
    GameState keep = fresh(), probe = keep;
    GameState src = at_done(15);
    give_item(&src, EQ_WARHAMMER, MAT_IRON, MAT_IRON);
    force_open(&src, mk_customer(CT_KNIGHT, 120, 45, 1100));
    char *bad;
    {
        GameState x = src;
        x.guild_done = 4;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    } /* 解放前の warhammer + Knight */
    {
        GameState x = src;
        x.guild_done = 4;
        x.inv_count = 0;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    } /* Knight だけでも十分 */
    {
        GameState x = src;
        x.queue[0].type = CT_ARCANIST;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    } /* ランク 2 でのランク 3 の客 */
    {
        GameState x = src;
        x.inv[0].type = EQ_CRYSTAL_ORB;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = src;
        x.inv[0].type = EQ_COUNT;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = src;
        x.inv[0].type = -1;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = src;
        x.queue[0].type = CUST_TYPE_COUNT;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = src;
        x.queue[0].type = -2;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = at_done(15);
        x.guild_board[0].customer_type = CUST_TYPE_COUNT;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = at_done(4);
        x.guild_board[0].customer_type = CT_KNIGHT;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = at_done(15);
        x.guild_active[0] =
            (GuildContract){x.guild_next_id++, CT_ARCANIST, 40, GD_EASY, 35, 1, 3, 3, GUILD_CONTRACT_STANDARD, 0, 0};
        x.guild_active_count = 1;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    {
        GameState x = src;
        x.guild_done = -1;
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
    /* このビルドより多くのコンテンツを知っているセーブは、より新しいゲームのもの */
    {
        char *ts = save_text(&src);
        char *nw = replace_line(ts, "counts ", "counts 7 11 11 3");
        CHECK_EQ(load_text(&probe, nw), SAVE_ERR_VERSION);
        CHECK(same_state(&probe, &keep));
        free(ts);
        free(nw);
    }
    {
        char *ts = save_text(&src);
        char *nw = replace_line(ts, "counts ", "counts 7 10 12 3");
        CHECK_EQ(load_text(&probe, nw), SAVE_ERR_VERSION);
        free(ts);
        free(nw);
    }
    /* ランクがあれば同じアイテムは問題ない (上のケースの対照実験) */
    {
        GameState x = src;
        CHECK(game_validate(&x));
        bad = save_text(&x);
        CHECK_EQ(load_text(&probe, bad), SAVE_OK);
        CHECK_EQ(probe.inv[0].type, EQ_WARHAMMER);
        free(bad);
    }
}

/* ---- 状態 / fail_reason の整合性 (game_validate) ---- */
static void test_fail_consistency(void) {
    puts("fail_consistency");
    GameState g = fresh();
    CHECK(game_validate(&g)); /* プレイ中 + FAIL_NONE */
    g.status = ST_FAILED;
    g.fail_reason = FAIL_TIMEOUT;
    CHECK(game_validate(&g)); /* 1 */
    g.fail_reason = FAIL_BANKRUPT;
    CHECK(game_validate(&g));
    g.fail_reason = FAIL_NONE;
    CHECK(!game_validate(&g)); /* 2 */
    g.fail_reason = 7;
    CHECK(!game_validate(&g)); /* 3 */
    g.fail_reason = -1;
    CHECK(!game_validate(&g));
    int others[] = {ST_PLAYING, ST_CLEARED, ST_SANDBOX};
    for (int i = 0; i < 3; i++) { /* 4 */
        g = fresh();
        g.status = others[i];
        CHECK(game_validate(&g)); /* 5 */
        g.fail_reason = FAIL_TIMEOUT;
        CHECK(!game_validate(&g));
        g.fail_reason = FAIL_BANKRUPT;
        CHECK(!game_validate(&g));
    }
    /* 実際のプレイで生じる状態は有効のまま: クリア済み、失敗 (両方の経路)、サンドボックス */
    g = fresh();
    g.money = 3000;
    g.rep = 95;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_CLEARED);
    CHECK(game_validate(&g));
    CHECK_EQ(campaign_continue_sandbox(&g), R_OK);
    CHECK(game_validate(&g));
    g = fresh();
    g.day = BAL.max_days;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_FAILED);
    CHECK(game_validate(&g));
    g = fresh();
    g.money = BAL.bankrupt_line;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.fail_reason, FAIL_BANKRUPT);
    CHECK(game_validate(&g));

    /* 6: 矛盾したデータは game_load() に拒否され、対象の状態には触れない */
    GameState keep = fresh();
    market_buy(&keep, MAT_IRON, 2);
    for (int variant = 0; variant < 2; variant++) {
        GameState bad = fresh();
        if (variant == 0) {
            bad.status = ST_FAILED;
            bad.fail_reason = FAIL_NONE;
        } else {
            bad.status = ST_PLAYING;
            bad.fail_reason = FAIL_BANKRUPT;
        }
        char *txt = save_text(&bad); /* game_save は検証しない */
        GameState probe = keep;
        CHECK_EQ(load_text(&probe, txt), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(txt);
    }
    GameState ok = fresh();
    ok.status = ST_FAILED;
    ok.fail_reason = FAIL_BANKRUPT;
    ok.phase = PH_SETTLE;
    char *t = save_text(&ok);
    GameState l;
    CHECK_EQ(load_text(&l, t), SAVE_OK);
    CHECK(same_state(&ok, &l));
    free(t);
}

/* ---- game_save_file: 原子的な置き換え。古いセーブを決して壊さない ---- */
#ifndef _WIN32
static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}
static int is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static int failing_replace(const char *tmp, const char *path) {
    (void)tmp;
    (void)path;
    return -1;
}
static void test_save_file_safety(void) {
    puts("save_file_safety");
    const char *P = "ls_test_a.sav", *PT = "ls_test_a.sav.tmp";
    remove(P);
    remove(PT);
    GameState a = fresh();
    a.money = 111;
    GameState b = fresh();
    b.money = 222;
    market_buy(&b, MAT_WOOD, 4);
    GameState l;

    CHECK_EQ(game_save_file(&a, P), SAVE_OK); /* 1 新規保存 */
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&a, &l));
    CHECK(!file_exists(PT));
    CHECK_EQ(game_save_file(&b, P), SAVE_OK); /* 2 上書き */
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&b, &l));
    CHECK(!file_exists(PT));
    for (int i = 0; i < 20; i++) {
        b.money = 300 + i;
        CHECK_EQ(game_save_file(&b, P), SAVE_OK);
    } /* 繰り返し */
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK_EQ(l.money, 319);

    /* 5a 一時ファイルを作れない (<path>.tmp をディレクトリが占有している): 古いセーブは残らなければならない */
    CHECK_EQ(mkdir(PT, 0755), 0);
    GameState old = l;
    CHECK_EQ(game_save_file(&a, P), SAVE_ERR_IO); /* 4 偽りの成功を返さない */
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&old, &l)); /* 3 */
    CHECK(is_dir(PT));           /* 他人のオブジェクトは削除していない */
    rmdir(PT);

    /* 5b 書き込み失敗 (ファイルサイズ制限 => フラッシュ時に EFBIG): 古いセーブは残り、tmp は片付けられる */
    struct rlimit saved, small;
    CHECK_EQ(getrlimit(RLIMIT_FSIZE, &saved), 0);
    signal(SIGXFSZ, SIG_IGN);
    small = saved;
    small.rlim_cur = 64;
    CHECK_EQ(setrlimit(RLIMIT_FSIZE, &small), 0);
    SaveResult wr = game_save_file(&a, P);
    setrlimit(RLIMIT_FSIZE, &saved);
    CHECK_EQ(wr, SAVE_ERR_IO);
    CHECK(!file_exists(PT));
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&old, &l));

    /* 5c 置き換え失敗 (宛先が空でないディレクトリ): エラー、ディレクトリは無傷、tmp は削除される */
    const char *D = "ls_test_dir.sav", *DF = "ls_test_dir.sav/keep.txt", *DT = "ls_test_dir.sav.tmp";
    remove(DF);
    rmdir(D);
    remove(DT);
    CHECK_EQ(mkdir(D, 0755), 0);
    FILE *k = fopen(DF, "w");
    CHECK(k != NULL);
    if (k) {
        fputs("precious", k);
        fclose(k);
    }
    CHECK_EQ(game_save_file(&a, D), SAVE_ERR_IO);
    CHECK(is_dir(D));
    CHECK(file_exists(DF));
    CHECK(!file_exists(DT));
    remove(DF);
    rmdir(D);

    /* 5c2 古いセーブが通常ファイルのときに置き換えが失敗する: それでも存在し、読み込めなければならない */
    save_test_set_replace(failing_replace);
    GameState c = fresh();
    c.money = 999;
    SaveResult rr = game_save_file(&c, P);
    save_test_set_replace(NULL);
    CHECK_EQ(rr, SAVE_ERR_IO);
    CHECK(file_exists(P));
    CHECK(!file_exists(PT));
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&old, &l));

    /* 5d 使えない宛先: 存在しないディレクトリ、NULL、長すぎるパス */
    CHECK_EQ(game_save_file(&a, "ls_no_such_dir/x.sav"), SAVE_ERR_IO);
    CHECK_EQ(game_save_file(&a, NULL), SAVE_ERR_IO);
    char longp[700];
    memset(longp, 'x', sizeof longp - 1);
    longp[sizeof longp - 1] = 0;
    CHECK_EQ(game_save_file(&a, longp), SAVE_ERR_IO);

    /* これらの失敗の後でも、通常の保存は動く */
    CHECK_EQ(game_save_file(&a, P), SAVE_OK);
    CHECK_EQ(game_load_file(&l, P), SAVE_OK);
    CHECK(same_state(&a, &l));
    remove(P);
    remove(PT);
}
#endif

/* ---- 12 ---- */
static void test_campaign_end(void) {
    puts("campaign_end");
    /* クリア */
    GameState g = fresh();
    g.money = BAL.goal_money + 20;
    g.rep = BAL.goal_rep + 5;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_CLEARED);
    CHECK_EQ(day_advance(&g), R_ERR_PHASE); /* キャンペーンは終了している */
    CHECK_EQ(market_buy(&g, MAT_IRON, 1), R_ERR_PHASE);
    CHECK_EQ(campaign_continue_sandbox(&g), R_OK);
    CHECK_EQ(g.status, ST_SANDBOX);
    CHECK_EQ(g.phase, PH_PREP);
    CHECK_EQ(g.day, 2);
    g.money = 5;
    g.day = 500;
    day_open(&g);
    day_close_early(&g); /* サンドボックス: タイムアウトも破産もなし */
    CHECK_EQ(g.status, ST_SANDBOX);
    CHECK(g.money >= 0);
    CHECK_EQ(day_advance(&g), R_OK);
    /* 所持金だけ、または評判だけでは足りない */
    g = fresh();
    g.money = 5000;
    g.rep = BAL.goal_rep - 1;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_PLAYING);
    g = fresh();
    g.money = BAL.goal_money - 1 + BAL.upkeep;
    g.rep = 100;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_PLAYING);
    /* タイムアウト */
    g = fresh();
    g.day = BAL.max_days;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_FAILED);
    CHECK_EQ(g.fail_reason, FAIL_TIMEOUT);
    CHECK_EQ(day_advance(&g), R_ERR_PHASE);
    /* 破産 */
    g = fresh();
    g.money = BAL.bankrupt_line + BAL.upkeep;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_PLAYING); /* ちょうど基準線上なら生き残れる */
    g = fresh();
    g.money = BAL.bankrupt_line + BAL.upkeep - 1;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_FAILED);
    CHECK_EQ(g.fail_reason, FAIL_BANKRUPT);
    /* 両方の目標を達成した最終日は、タイムアウトではなくクリアとして数える */
    g = fresh();
    g.day = BAL.max_days;
    g.money = 3000;
    g.rep = 95;
    day_open(&g);
    day_close_early(&g);
    CHECK_EQ(g.status, ST_CLEARED);
    /* ゲームオーバー状態は保存/読み込みを越えて保たれる */
    FILE *f = tmpfile();
    game_save(&g, f);
    rewind(f);
    GameState l;
    CHECK_EQ(game_load(&l, f), SAVE_OK);
    CHECK_EQ(l.status, ST_CLEARED);
    fclose(f);
}

/* ======================================================================
 * 装備の品質 + 特殊特性 (セーブ v3)
 * ====================================================================== */
/* 品質が存在する前のビルドが書いた、本物のバージョン 2 のセーブ (アイテム 4 つ、受注中の契約 1 件、日中)。 */
static const char V2_SAVE[] =
    "LOOTSMITH_SAVE 2\ncounts 7 9 10 3\nrng 2643077358\nday 1\nphase 1\nstatus 0\nfail 0\nmoney 900\nrep 10\n"
    "mat 0 3 30 9 9 -100\nmat 1 4 24 6 6 21\nmat 2 1 20 19 19 -75\nmat 3 0 0 8 8 37\nmat 4 1 12 13 13 73\nmat 5 0 0 13 "
    "13 -18\nmat 6 0 0 13 13 24\n"
    "up 0 1\nup 1 0\nup 2 0\ntot 0 0 0\nreport 3 0 0 0 0 0 250\ngreport 0 0 0\nguild 3204434081 4 15 0\n"
    "gboard 2\ngb 2 1 35 0 35 1 3 0\ngb 3 3 35 0 35 1 3 0\ngactive 1\nga 1 2 35 0 35 1 3 3\n"
    "inv 4\nitem 0 0 1 16 7 1 14 8 26 55 67 67\nitem 2 2 1 4 3 19 6 5 46 70 85 92\nitem 6 0 0 21 11 0 18 13 30 68 82 "
    "82\nitem 4 4 0 19 12 0 19 10 34 70 85 85\n"
    "queue 3 0\ncust 7 131 61 1229 2\ncust 2 147 53 1130 2\ncust 2 153 51 1324 2\nend\n";

/* 新しく *build* したアイテム (クラフトしたものではない) を在庫に入れる */
static int put_built(GameState *g, int eq, int main, int sub, int quality, int trait) {
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = eq;
    e.main = main;
    e.sub = sub;
    e.cost = 20;
    equipment_build(&e, g->up_level[UP_FORGE], quality, trait);
    equipment_price(&e);
    e.price = e.rec_price;
    g->inv[g->inv_count] = e;
    return g->inv_count++;
}
static int qmul(int base, int mult_pm) {
    return (base * mult_pm + 500) / 1000;
} /* 四捨五入 (0.5 は切り上げ)、整数のみ */

static void test_quality_tables(void) {
    puts("quality_tables");
    CHECK(content_validate() == NULL);
    CHECK_EQ(QUALITY_COMMON, 0);
    CHECK_EQ(EQ_TRAIT_NONE, 0); /* ゼロ初期化されたデータは、有効な無印のアイテム */
    CHECK_EQ(QUALITY_COUNT, 4);
    CHECK_EQ(EQ_TRAIT_STANDARD_COUNT, 6); /* 通常の抽選の対象: 通常特性 5 つ + なし (ID の追記では変わらない) */
    CHECK_EQ(EQ_TRAIT_COUNT, 8);          /* 通常特性 5 つ + なし + Dragonforging の特殊特性 2 つ */
    static const struct {
        const char *name;
        int mult, has;
    } q[] = {{"Common", 1000, 0}, {"Fine", 1080, 0}, {"Masterwork", 1180, 1}, {"Legendary", 1320, 1}};
    for (int i = 0; i < QUALITY_COUNT; i++) {
        CHECK(!strcmp(QUALITY_DEFS[i].name, q[i].name));
        CHECK_EQ(QUALITY_DEFS[i].mult_pm, q[i].mult);
        CHECK_EQ(QUALITY_DEFS[i].has_trait, q[i].has);
        CHECK(!strcmp(quality_name(i), q[i].name));
    }
    CHECK(!strcmp(quality_name(-1), "?"));
    CHECK(!strcmp(quality_name(QUALITY_COUNT), "?"));
    static const char *tn[] = {"None",   "Keen",        "Warding",       "Arcane",
                               "Sturdy", "Lightweight", "Draconic Edge", "Wyrmscale"};
    for (int t = 0; t < EQ_TRAIT_COUNT; t++) {
        CHECK(!strcmp(TRAIT_DEFS[t].name, tn[t]));
        CHECK(!strcmp(trait_name(t), tn[t]));
    }
    CHECK(!strcmp(trait_name(-1), "?"));
    CHECK(!strcmp(trait_name(EQ_TRAIT_COUNT), "?"));
    CHECK_EQ(TRAIT_DEFS[EQ_TRAIT_KEEN].stat, TS_ATK);
    CHECK_EQ(TRAIT_DEFS[EQ_TRAIT_WARDING].stat, TS_DEF);
    CHECK_EQ(TRAIT_DEFS[EQ_TRAIT_ARCANE].stat, TS_MAG);
    CHECK_EQ(TRAIT_DEFS[EQ_TRAIT_STURDY].stat, TS_DUR);
    CHECK_EQ(TRAIT_DEFS[EQ_TRAIT_LIGHTWEIGHT].stat, TS_WT_DOWN);
    for (int t = EQ_TRAIT_KEEN; t < EQ_TRAIT_STANDARD_COUNT; t++) { /* 通常特性 (既存の仕様のまま) */
        int mw = t == EQ_TRAIT_LIGHTWEIGHT ? 1 : 2, lg = t == EQ_TRAIT_LIGHTWEIGHT ? 2 : 3;
        CHECK_EQ(TRAIT_DEFS[t].amount[QUALITY_COMMON], 0);
        CHECK_EQ(TRAIT_DEFS[t].amount[QUALITY_FINE], 0);
        CHECK_EQ(TRAIT_DEFS[t].amount[QUALITY_MASTERWORK], mw);
        CHECK_EQ(TRAIT_DEFS[t].amount[QUALITY_LEGENDARY], lg);
    }
    /* ギルドランクごとの基本確率は仕様どおり */
    static const int odds[RANK_COUNT][QUALITY_COUNT] = {
        {70, 25, 5, 0}, {55, 32, 12, 1}, {40, 35, 20, 5}, {25, 35, 28, 12}, {20, 33, 30, 17}};
    for (int r = 0; r < RANK_COUNT; r++) {
        int sum = 0;
        for (int i = 0; i < QUALITY_COUNT; i++) {
            CHECK_EQ(QUALITY_RATE_PCT[r][i], odds[r][i]);
            sum += QUALITY_RATE_PCT[r][i];
        }
        CHECK_EQ(sum, 100);
    }
    CHECK_EQ(QUALITY_RATE_PCT[RANK_APPRENTICE][QUALITY_LEGENDARY], 0); /* 最初の昇格前は Legendary なし */
    CHECK_EQ(BAL.quality_forge_shift_pct, 5);
    /* 検証関数は形だけのものではない: content_validate()
     * のメッセージはテーブル名を挙げるので、それが依存する関数は存在する */
    CHECK(strlen(quality_name(QUALITY_LEGENDARY)) <= NAME_MAX_LEN);
}

static void test_quality_rates(void) {
    puts("quality_rates");
    int r[QUALITY_COUNT];
    for (int rank = 0; rank < RANK_COUNT; rank++) {
        for (int forge = 0; forge <= UPGRADES[UP_FORGE].max_level; forge++) {
            quality_rates(rank, forge, r);
            int sum = 0, shift = 5 * forge;
            for (int i = 0; i < QUALITY_COUNT; i++) {
                CHECK(r[i] >= 0);
                sum += r[i];
            }
            CHECK_EQ(sum, 100);
            if (shift > QUALITY_RATE_PCT[rank][QUALITY_COMMON]) {
                shift = QUALITY_RATE_PCT[rank][QUALITY_COMMON];
            }
            CHECK_EQ(r[QUALITY_COMMON], QUALITY_RATE_PCT[rank][QUALITY_COMMON] -
                                            shift); /* 1 レベルにつき 5 ポイントが Common から出ていき ... */
            CHECK_EQ(r[QUALITY_FINE], QUALITY_RATE_PCT[rank][QUALITY_FINE] + shift); /* ... Fine に入る。他は動かない */
            CHECK_EQ(r[QUALITY_MASTERWORK], QUALITY_RATE_PCT[rank][QUALITY_MASTERWORK]);
            CHECK_EQ(r[QUALITY_LEGENDARY], QUALITY_RATE_PCT[rank][QUALITY_LEGENDARY]);
        }
    }
    quality_rates(RANK_APPRENTICE, 2, r); /* 仕様の計算例 */
    CHECK_EQ(r[0], 60);
    CHECK_EQ(r[1], 35);
    CHECK_EQ(r[2], 5);
    CHECK_EQ(r[3], 0);
    quality_rates(RANK_GRANDMASTER, 3, r);
    CHECK_EQ(r[0], 10);
    CHECK_EQ(r[1], 50);
    CHECK_EQ(r[2], 28);
    CHECK_EQ(r[3], 12);
    quality_rates(RANK_MYTHIC, 0, r); /* Mythic Smith: 仕様の初期値 20/33/30/17 */
    CHECK_EQ(r[0], 20);
    CHECK_EQ(r[1], 33);
    CHECK_EQ(r[2], 30);
    CHECK_EQ(r[3], 17);
    quality_rates(RANK_MYTHIC, 3,
                  r); /* Forge 3: Common から 15 ポイントが Fine へ (Common は 20 残っているので全量移る) */
    CHECK_EQ(r[0], 5);
    CHECK_EQ(r[1], 48);
    CHECK_EQ(r[2], 30);
    CHECK_EQ(r[3], 17);
    /* 範囲外の引数は丸められ、範囲外を読むことは決してない */
    int a[QUALITY_COUNT], b[QUALITY_COUNT];
    quality_rates(-5, -2, a);
    quality_rates(RANK_APPRENTICE, 0, b);
    CHECK(!memcmp(a, b, sizeof a));
    quality_rates(99, 99, a); /* 範囲外のランクは最高ランクに丸められる */
    quality_rates(RANK_MYTHIC, UPGRADES[UP_FORGE].max_level, b);
    CHECK(!memcmp(a, b, sizeof a));
}

static void test_quality_from_roll(void) {
    puts("quality_from_roll");
    for (int rank = 0; rank < RANK_COUNT; rank++) {
        for (int forge = 0; forge <= UPGRADES[UP_FORGE].max_level; forge++) {
            int rates[QUALITY_COUNT], seen[QUALITY_COUNT] = {0}, prev = 0;
            quality_rates(rank, forge, rates);
            for (int roll = 0; roll < 100; roll++) {
                int q = quality_from_roll(rank, forge, roll);
                CHECK(q >= 0 && q < QUALITY_COUNT);
                CHECK(q >= prev);
                prev = q; /* 高いロールが低い品質を返すことは決してない */
                seen[q]++;
            }
            for (int i = 0; i < QUALITY_COUNT; i++) {
                CHECK_EQ(seen[i], rates[i]); /* 100 通りのロールが確率を正確に再現する */
            }
            CHECK(quality_from_roll(rank, forge, -7) ==
                  quality_from_roll(rank, forge, 0)); /* 範囲外のロールは丸められる */
            CHECK(quality_from_roll(rank, forge, 100) == quality_from_roll(rank, forge, 99));
            CHECK(quality_from_roll(rank, forge, 5000) == quality_from_roll(rank, forge, 99));
            if (rank == RANK_APPRENTICE) {
                for (int roll = -1; roll <= 101; roll++) {
                    CHECK(quality_from_roll(rank, forge, roll) != QUALITY_LEGENDARY);
                }
            }
        }
    }
    /* 境界、Apprentice / Forge 0 (70 / 25 / 5 / 0) と Forge 2 (60 / 35 / 5 / 0) */
    CHECK_EQ(quality_from_roll(0, 0, 0), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(0, 0, 69), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(0, 0, 70), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(0, 0, 94), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(0, 0, 95), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(0, 0, 99), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(0, 2, 59), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(0, 2, 60), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(0, 2, 94), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(0, 2, 95), QUALITY_MASTERWORK);
    /* Journeyman (55/32/12/1): Legendary は最上位の 1 ロールだけ */
    CHECK_EQ(quality_from_roll(1, 0, 98), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(1, 0, 99), QUALITY_LEGENDARY);
    CHECK_EQ(quality_from_roll(1, 0, 54), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(1, 0, 55), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(1, 0, 86), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(1, 0, 87), QUALITY_MASTERWORK);
    /* Grandmaster / Forge 3 (10/50/28/12) */
    CHECK_EQ(quality_from_roll(3, 3, 9), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(3, 3, 10), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(3, 3, 59), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(3, 3, 60), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(3, 3, 87), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(3, 3, 88), QUALITY_LEGENDARY);
    /* Mythic Smith / Forge 0 (20/33/30/17): 境界は 19|20、52|53、82|83 */
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 19), QUALITY_COMMON);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 20), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 52), QUALITY_FINE);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 53), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 82), QUALITY_MASTERWORK);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 83), QUALITY_LEGENDARY);
    CHECK_EQ(quality_from_roll(RANK_MYTHIC, 0, 99), QUALITY_LEGENDARY);
    /* クラフトが使うランクは、guild_done から導出されたもの */
    CHECK_EQ(guild_rank(0), RANK_APPRENTICE);
    CHECK_EQ(guild_rank(5), RANK_JOURNEYMAN);
    CHECK_EQ(guild_rank(30), RANK_GRANDMASTER);
    /* 特性: ロールは 5 つの実在の特性のどれかを選び、「なし」は決して選ばない */
    int hit[EQ_TRAIT_COUNT] = {0};
    for (int roll = 0; roll < EQ_TRAIT_STANDARD_COUNT - 1; roll++) {
        int t = trait_from_roll(roll);
        CHECK(t > EQ_TRAIT_NONE && t < EQ_TRAIT_STANDARD_COUNT);
        hit[t]++;
    }
    for (int t = 1; t < EQ_TRAIT_STANDARD_COUNT; t++) {
        CHECK_EQ(hit[t], 1);
    }
    for (int t = EQ_TRAIT_STANDARD_COUNT; t < EQ_TRAIT_COUNT; t++) {
        CHECK_EQ(hit[t], 0); /* 通常の抽選は、特殊特性を決して選ばない */
    }
    CHECK_EQ(trait_from_roll(-3), EQ_TRAIT_KEEN);
    CHECK_EQ(trait_from_roll(1000), EQ_TRAIT_LIGHTWEIGHT);
}

static void test_quality_stats(void) {
    puts("quality_stats");
    /* 文字どおりの抜き取り確認: Iron/Iron Sword は ATK 18 DEF 9 MAG 0 DUR 16 WT 10 */
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = EQ_SWORD;
    e.main = MAT_IRON;
    e.sub = MAT_IRON;
    equipment_compute_stats(&e, 0);
    CHECK_EQ(e.atk, 18);
    CHECK_EQ(e.def, 9);
    CHECK_EQ(e.mag, 0);
    CHECK_EQ(e.dur, 16);
    CHECK_EQ(e.wt, 10);
    CHECK_EQ(e.quality, QUALITY_COMMON);
    CHECK_EQ(e.trait, EQ_TRAIT_NONE); /* 基本関数は品質を決して設定しない */
    Equipment c = e;
    equipment_build(&c, 0, QUALITY_COMMON, EQ_TRAIT_NONE);
    CHECK(!memcmp(&c, &e, sizeof c)); /* Common == 通常の基本計算 */
    Equipment f;
    memset(&f, 0, sizeof f);
    f.type = EQ_SWORD;
    f.main = MAT_IRON;
    f.sub = MAT_IRON;
    equipment_build(&f, 0, QUALITY_FINE, EQ_TRAIT_NONE);
    CHECK_EQ(f.atk, 19);
    CHECK_EQ(f.def, 10);
    CHECK_EQ(f.dur, 17);
    CHECK_EQ(f.wt, 10);
    CHECK_EQ(f.quality, QUALITY_FINE);
    CHECK_EQ(f.trait, EQ_TRAIT_NONE);
    Equipment m = f;
    memset(&m, 0, sizeof m);
    m.type = EQ_SWORD;
    m.main = MAT_IRON;
    m.sub = MAT_IRON;
    equipment_build(&m, 0, QUALITY_MASTERWORK, EQ_TRAIT_KEEN);
    CHECK_EQ(m.atk, 21 + 2);
    CHECK_EQ(m.def, 11);
    CHECK_EQ(m.dur, 19);
    CHECK_EQ(m.wt, 10);
    Equipment l;
    memset(&l, 0, sizeof l);
    l.type = EQ_SWORD;
    l.main = MAT_IRON;
    l.sub = MAT_IRON;
    equipment_build(&l, 0, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
    CHECK_EQ(l.atk, 24);
    CHECK_EQ(l.def, 12);
    CHECK_EQ(l.dur, 21);
    CHECK_EQ(l.wt, 10 - 2);

    /* すべてのレシピ x 鍛冶場レベル x 品質 x 特性: 正確な計算式、適用は 1 回、範囲は有効 */
    long combos = 0;
    for (int eq = 0; eq < EQ_COUNT; eq++) {
        for (int mm = 0; mm < MAT_COUNT; mm++) {
            for (int sm = 0; sm < MAT_COUNT; sm++) {
                for (int forge = 0; forge <= 3; forge++) {
                    Equipment base;
                    memset(&base, 0, sizeof base);
                    base.type = eq;
                    base.main = mm;
                    base.sub = sm;
                    equipment_compute_stats(&base, forge);
                    for (int q = 0; q < QUALITY_COUNT; q++) {
                        for (int t = 0; t < EQ_TRAIT_COUNT; t++) {
                            int legal = (QUALITY_DEFS[q].has_trait != 0) == (t != EQ_TRAIT_NONE);
                            if (t >= EQ_TRAIT_STANDARD_COUNT && mm != MAT_DRAGON_SCALE && sm != MAT_DRAGON_SCALE) {
                                legal = 0; /* 特殊特性は Dragon Scale を使った装備だけ */
                            }
                            Equipment x;
                            memset(&x, 0, sizeof x);
                            x.type = eq;
                            x.main = mm;
                            x.sub = sm;
                            x.cost = 7;
                            equipment_build(&x, forge, q, t);
                            if (!legal) {
                                CHECK(!memcmp(&x.atk, &base.atk, 5 * sizeof(int)));
                                CHECK_EQ(x.quality, QUALITY_COMMON);
                                CHECK_EQ(x.trait, EQ_TRAIT_NONE);
                                continue;
                            }
                            int mult = QUALITY_DEFS[q].mult_pm;
                            int ea = qmul(base.atk, mult), ed = qmul(base.def, mult), em = qmul(base.mag, mult),
                                eu = qmul(base.dur, mult), ew = base.wt;
                            int amt = q == QUALITY_MASTERWORK ? (t == EQ_TRAIT_LIGHTWEIGHT ? 1 : 2)
                                                              : (t == EQ_TRAIT_LIGHTWEIGHT ? 2 : 3);
                            if (t >= EQ_TRAIT_STANDARD_COUNT) {
                                amt = q == QUALITY_MASTERWORK ? 3 : 4; /* Draconic Edge / Wyrmscale: +3 / +4 */
                            }
                            switch (t) {
                                case EQ_TRAIT_DRACONIC_EDGE:
                                    ea += amt;
                                    break;
                                case EQ_TRAIT_WYRMSCALE:
                                    eu += amt;
                                    break;
                                case EQ_TRAIT_KEEN:
                                    ea += amt;
                                    break;
                                case EQ_TRAIT_WARDING:
                                    ed += amt;
                                    break;
                                case EQ_TRAIT_ARCANE:
                                    em += amt;
                                    break;
                                case EQ_TRAIT_STURDY:
                                    eu += amt;
                                    break;
                                case EQ_TRAIT_LIGHTWEIGHT:
                                    ew -= amt;
                                    if (ew < 1) {
                                        ew = 1;
                                    }
                                    break;
                                default:
                                    break;
                            }
                            CHECK_EQ(x.atk, ea);
                            CHECK_EQ(x.def, ed);
                            CHECK_EQ(x.mag, em);
                            CHECK_EQ(x.dur, eu);
                            CHECK_EQ(x.wt, ew);
                            CHECK_EQ(x.quality, q);
                            CHECK_EQ(x.trait, t);
                            CHECK_EQ(x.cost, 7);
                            CHECK(x.atk >= base.atk && x.def >= base.def && x.mag >= base.mag &&
                                  x.dur >= base.dur); /* 品質が能力を下げることは決してない */
                            CHECK(x.wt >= 1 && x.wt <= base.wt);
                            CHECK(x.atk >= 0 && x.def >= 0 && x.mag >= 0 && x.dur >= 0 && x.atk < 9999 && x.dur < 9999);
                            /* まったく同じアイテムを作り直すと、まったく同じアイテムになる: 何も累積しない */
                            Equipment again = x;
                            equipment_build(&again, forge, q, t);
                            CHECK(!memcmp(&again, &x, sizeof x));
                            combos++;
                        }
                    }
                }
            }
        }
    }
    CHECK(combos > 1000);
    /* equipment_apply_quality は、すでに品質/特性を持つアイテムに対して拒否される (二重適用なし) */
    Equipment once;
    memset(&once, 0, sizeof once);
    once.type = EQ_STAFF;
    once.main = MAT_MANA;
    once.sub = MAT_WOOD;
    equipment_compute_stats(&once, 1);
    Equipment plain = once;
    CHECK_EQ(equipment_apply_quality(&once, QUALITY_MASTERWORK, EQ_TRAIT_ARCANE), 1);
    Equipment snap = once;
    CHECK_EQ(equipment_apply_quality(&once, QUALITY_MASTERWORK, EQ_TRAIT_ARCANE), 0);
    CHECK_EQ(equipment_apply_quality(&once, QUALITY_LEGENDARY, EQ_TRAIT_KEEN), 0);
    CHECK_EQ(equipment_apply_quality(&once, QUALITY_FINE, EQ_TRAIT_NONE), 0);
    CHECK(!memcmp(&once, &snap, sizeof once));
    CHECK_EQ(once.mag, qmul(plain.mag, 1180) + 2);
    Equipment fine_twice = plain;
    CHECK_EQ(equipment_apply_quality(&fine_twice, QUALITY_FINE, EQ_TRAIT_NONE), 1);
    snap = fine_twice;
    CHECK_EQ(equipment_apply_quality(&fine_twice, QUALITY_FINE, EQ_TRAIT_NONE), 0);
    CHECK(!memcmp(&fine_twice, &snap, sizeof snap));
    /* 不正な品質/特性の組み合わせや ID は拒否され、アイテムには触れない */
    Equipment bad = plain, keep = plain;
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_FINE, EQ_TRAIT_KEEN), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_COMMON, EQ_TRAIT_STURDY), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_MASTERWORK, EQ_TRAIT_NONE), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_LEGENDARY, EQ_TRAIT_NONE), 0);
    CHECK_EQ(equipment_apply_quality(&bad, -1, EQ_TRAIT_NONE), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_COUNT, EQ_TRAIT_NONE), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_MASTERWORK, -1), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_MASTERWORK, EQ_TRAIT_COUNT), 0);
    CHECK(!memcmp(&bad, &keep, sizeof bad));
    /* 重量の下限: Lightweight で WT が 1 未満になることはない */
    Equipment lw;
    memset(&lw, 0, sizeof lw);
    lw.type = EQ_DAGGER;
    lw.main = MAT_WOOD;
    lw.sub = MAT_WOOD;
    equipment_build(&lw, 0, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
    CHECK_EQ(lw.wt, 1); /* Dagger/Wood は WT 2。2 を引くと 0 になってしまう */
    Equipment one;
    memset(&one, 0, sizeof one);
    one.atk = 5;
    one.def = 1;
    one.dur = 4;
    one.wt = 1;
    CHECK_EQ(equipment_apply_quality(&one, QUALITY_MASTERWORK, EQ_TRAIT_LIGHTWEIGHT), 1);
    CHECK_EQ(one.wt, 1);
    Equipment zero;
    memset(&zero, 0, sizeof zero);
    zero.wt = 1;
    CHECK_EQ(equipment_apply_quality(&zero, QUALITY_LEGENDARY, EQ_TRAIT_STURDY), 1);
    CHECK_EQ(zero.atk, 0);
    CHECK_EQ(zero.def, 0);
    CHECK_EQ(zero.dur, 3); /* 0 の能力値は 0 のまま (特性だけが加える) */

    /* 特性のテキストはテーブルから取る */
    char buf[24];
    trait_effect_text(EQ_TRAIT_KEEN, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "+2 ATK"));
    trait_effect_text(EQ_TRAIT_KEEN, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK(!strcmp(buf, "+3 ATK"));
    trait_effect_text(EQ_TRAIT_WARDING, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "+2 DEF"));
    trait_effect_text(EQ_TRAIT_ARCANE, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK(!strcmp(buf, "+3 MAG"));
    trait_effect_text(EQ_TRAIT_STURDY, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "+2 DUR"));
    trait_effect_text(EQ_TRAIT_LIGHTWEIGHT, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "-1 WT"));
    trait_effect_text(EQ_TRAIT_LIGHTWEIGHT, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK(!strcmp(buf, "-2 WT"));
    trait_effect_text(EQ_TRAIT_NONE, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK_EQ(buf[0], 0);
    trait_effect_text(EQ_TRAIT_KEEN, QUALITY_FINE, buf, sizeof buf);
    CHECK_EQ(buf[0], 0);
    trait_effect_text(99, 99, buf, sizeof buf);
    CHECK_EQ(buf[0], 0);
    trait_effect_text(EQ_TRAIT_KEEN, QUALITY_LEGENDARY, buf, 0); /* 空のバッファでもクラッシュしない */
}

static void test_quality_value_and_fit(void) {
    puts("quality_value_and_fit");
    GameState g = at_done(30);
    g.up_level[UP_FORGE] = 1;
    int c = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    int f = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_FINE, EQ_TRAIT_NONE);
    int m = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_MASTERWORK, EQ_TRAIT_KEEN);
    int l = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_LEGENDARY, EQ_TRAIT_KEEN);
    CHECK(game_validate(&g));
    CHECK(g.inv[c].value < g.inv[f].value && g.inv[f].value < g.inv[m].value && g.inv[m].value < g.inv[l].value);
    CHECK(g.inv[c].rec_price < g.inv[f].rec_price && g.inv[f].rec_price < g.inv[m].rec_price &&
          g.inv[m].rec_price < g.inv[l].rec_price);
    CHECK_EQ(g.inv[c].cost, g.inv[l].cost); /* 素材の費用は同じ */
    for (int i = 0; i < g.inv_count; i++) { /* value / price は最終的な能力から、従来の計算式だけで求める */
        Equipment re = g.inv[i];
        re.value = re.rec_price = 0;
        equipment_price(&re);
        CHECK_EQ(re.value, g.inv[i].value);
        CHECK_EQ(re.rec_price, g.inv[i].rec_price);
        CHECK(g.inv[i].rec_price <= BAL.price_cap);
    }
    int ct[] = {CT_WARRIOR, CT_THIEF};
    for (int k = 0; k < 2; k++) {
        int fc = equip_fit(ct[k], &g.inv[c]), ff = equip_fit(ct[k], &g.inv[f]), fl = equip_fit(ct[k], &g.inv[l]);
        CHECK(fc <= ff && ff <= fl && fc < fl); /* FIT は最終的な能力を読む */
    }
    /* judge_sale: Common のアイテムにはもう正当化できない価格が、Legendary なら問題ない */
    Customer cu = mk_customer(CT_WARRIOR, 5000, 1, 1100);
    int pc = judge_sale(&cu, &g.inv[c], 1).max_price, pl = judge_sale(&cu, &g.inv[l], 1).max_price;
    CHECK(pl > pc);
    CHECK_EQ(judge_sale(&cu, &g.inv[c], pl).result, SALE_TOO_EXPENSIVE);
    CHECK_EQ(judge_sale(&cu, &g.inv[l], pl).result, SALE_OK);
    CHECK_EQ(judge_sale(&cu, &g.inv[l], 1).fit, equip_fit(CT_WARRIOR, &g.inv[l]));
    /* 客の最低 FIT は、より良いアイテムでなければ満たせない */
    int need = (equip_fit(CT_WARRIOR, &g.inv[c]) + equip_fit(CT_WARRIOR, &g.inv[l]) + 1) / 2;
    cu = mk_customer(CT_WARRIOR, 5000, need, 1300);
    CHECK_EQ(judge_sale(&cu, &g.inv[c], 1).result, SALE_LOW_FIT);
    CHECK_EQ(judge_sale(&cu, &g.inv[l], 1).result, SALE_OK);
    /* 特性の寄与は FIT に現れる: Arcane は Mage が重視する MAG を上げ、Warding の DEF は Mage には意味がない */
    GameState tg = at_done(30);
    int arc = put_built(&tg, EQ_STAFF, MAT_MANA, MAT_WOOD, QUALITY_MASTERWORK, EQ_TRAIT_ARCANE);
    int war = put_built(&tg, EQ_STAFF, MAT_MANA, MAT_WOOD, QUALITY_MASTERWORK, EQ_TRAIT_WARDING);
    CHECK_EQ(tg.inv[arc].mag, tg.inv[war].mag + 2);
    CHECK(equip_fit(CT_MAGE, &tg.inv[arc]) > equip_fit(CT_MAGE, &tg.inv[war]));
    CHECK(tg.inv[arc].value != tg.inv[war].value);
    /* ギルド契約も最終的な FIT で判定される */
    g.inv_count = 0;
    g.phase = PH_PREP;
    c = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    l = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
    int fcm = equip_fit(CT_THIEF, &g.inv[c]), fle = equip_fit(CT_THIEF, &g.inv[l]);
    CHECK(fle > fcm);
    put_contract(&g, CT_THIEF, fle, GD_STANDARD);
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK_EQ(guild_item_fit(&g, 0, c), fcm);
    CHECK_EQ(guild_item_fit(&g, 0, l), fle);
    GuildOutcome o;
    CHECK_EQ(guild_deliver(&g, 0, c, &o), GUILD_ERR_LOW_FIT);
    CHECK_EQ(o.fit, fcm);
    CHECK_EQ(g.inv_count, 2);
    int gold0 = g.money;
    CHECK_EQ(guild_deliver(&g, 0, l, &o), GUILD_OK);
    CHECK_EQ(o.fit, fle);
    CHECK_EQ(g.inv_count, 1);
    CHECK(g.money > gold0);
    CHECK_EQ(g.inv[0].quality, QUALITY_COMMON); /* もう一方のアイテムは変わらずそこにある */
}

/* 品質が存在する前のビルドから取得した、新しいゲームの 3 本のストリーム。 */
static void test_rng_streams(void) {
    puts("rng_streams");
    GameState g = fresh();
    CHECK_EQ((long long)g.rng, 4285686168LL);
    CHECK_EQ((long long)g.guild_rng, 696431670LL);
    static const int prices0[MAT_COUNT] = {10, 6, 18, 8, 13, 12, 14, 30}; /* Dragon Scale は基準価格のまま動かない */
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(g.price[m], prices0[m]);
    }
    static const int board0[3][3] = {{5, 35, 0}, {4, 35, 0}, {0, 35, 0}}; /* 客、最低 FIT、難易度 */
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(g.guild_board[i].id, i + 1);
        CHECK_EQ(g.guild_board[i].customer_type, board0[i][0]);
        CHECK_EQ(g.guild_board[i].min_fit, board0[i][1]);
        CHECK_EQ(g.guild_board[i].difficulty, board0[i][2]);
    }
    for (int d = 0; d < 4; d++) {
        pass_day(&g);
    }
    CHECK_EQ((long long)g.rng, 1618237454LL);
    CHECK_EQ((long long)g.guild_rng, 2146800925LL);
    CHECK_EQ(g.day, 5);
    static const int prices4[MAT_COUNT] = {9, 6, 20, 7, 12, 14, 13, 30};
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(g.price[m], prices4[m]);
    }
    static const int board4[3][3] = {{4, 50, 1}, {2, 35, 0}, {0, 35, 0}};
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(g.guild_board[i].id, 13 + i);
        CHECK_EQ(g.guild_board[i].customer_type, board4[i][0]);
        CHECK_EQ(g.guild_board[i].min_fit, board4[i][1]);
        CHECK_EQ(g.guild_board[i].difficulty, board4[i][2]);
    }
    day_open(&g);
    static const int q4[3][4] = {{4, 63, 45, 1139}, {0, 86, 45, 1127}, {4, 61, 44, 1172}};
    for (int i = 0; i < 3; i++) {
        CHECK_EQ(g.queue[i].type, q4[i][0]);
        CHECK_EQ(g.queue[i].budget, q4[i][1]);
        CHECK_EQ(g.queue[i].min_fit, q4[i][2]);
        CHECK_EQ(g.queue[i].tol_pm, q4[i][3]);
    }

    /* クラフトのストリーム: シードごとに決定的で、0 ではなく、他の 2 本とは別 */
    GameState a, b, c;
    game_init(&a, 12345);
    game_init(&b, 12345);
    game_init(&c, 54321);
    CHECK(a.craft_rng != 0);
    CHECK_EQ(a.craft_rng, b.craft_rng);
    CHECK(a.craft_rng != c.craft_rng);
    CHECK(a.craft_rng != a.rng && a.craft_rng != a.guild_rng && a.rng != a.guild_rng);
    GameState z;
    game_init(&z, 0);
    CHECK(z.craft_rng != 0);
    CHECK(game_validate(&z));
    for (uint32_t s = 1; s < 300; s++) {
        GameState t;
        game_init(&t, s * 977u);
        CHECK(t.craft_rng != 0 && t.craft_rng != t.rng && t.craft_rng != t.guild_rng);
    }

    /* クラフトは rng / guild_rng に決して触れず、クラフトするゲームは、しないゲームとまったく同じ価格/客/契約を抽選する
     */
    GameState crafter = fresh(), idler = fresh();
    for (int d = 0; d < 12; d++) {
        crafter.money = 5000;
        crafter.mat_stock[MAT_IRON] = 9;
        crafter.mat_cost[MAT_IRON] = 90;
        uint32_t r0 = crafter.rng, gr0 = crafter.guild_rng;
        for (int k = 0; k < 3; k++) {
            int before = crafter.inv_count;
            CHECK_EQ(craft_equipment(&crafter, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
            CHECK_EQ(crafter.inv_count, before + 1);
        }
        CHECK_EQ(crafter.rng, r0);
        CHECK_EQ(crafter.guild_rng, gr0);
        crafter.inv_count = 0;
        memset(crafter.inv, 0, sizeof crafter.inv);
        pass_day(&crafter);
        pass_day(&idler);
        CHECK_EQ(crafter.rng, idler.rng);
        CHECK_EQ(crafter.guild_rng, idler.guild_rng);
        CHECK(!memcmp(crafter.price, idler.price, sizeof crafter.price));
        CHECK(!memcmp(crafter.guild_board, idler.guild_board, sizeof crafter.guild_board));
    }
    CHECK(crafter.craft_rng != idler.craft_rng); /* 違うのはクラフトのストリームだけ */
    GameState oc = fresh();
    day_open(&oc);
    GameState oc2 = fresh();
    craft_equipment(&oc2, EQ_SWORD, MAT_IRON, MAT_IRON, NULL); /* (失敗: 素材なし) */
    GameState untouched = fresh();
    CHECK(same_state(&oc2, &untouched)); /* 失敗したクラフトは何も変えない */
    (void)oc;
}

static void test_craft_quality_flow(void) {
    puts("craft_quality_flow");
    /* A: 成功したクラフトは、文書化されたとおりの回数だけ craft_rng を消費し、他は何も消費しない */
    GameState g = at_done(30);
    g.up_level[UP_FORGE] = 2;
    int seen_q[QUALITY_COUNT] = {0}, seen_t[EQ_TRAIT_COUNT] = {0};
    for (int n = 0; n < 400; n++) {
        g.mat_stock[MAT_IRON] = 9;
        g.mat_cost[MAT_IRON] = 90;
        g.mat_stock[MAT_WOOD] = 9;
        g.mat_cost[MAT_WOOD] = 54;
        g.inv_count = 0;
        memset(g.inv, 0, sizeof g.inv);
        GameState before = g;
        uint32_t t = g.craft_rng;
        int idx = -1;
        int eq =
            n % EQ_WYRMCLEAVER; /* Grandmaster (30 件) で解放済みの装備すべて。Wyrmcleaver は Mythic の別テストで */
        CHECK_EQ(craft_equipment(&g, eq, MAT_IRON, MAT_WOOD, &idx), R_OK);
        int want_q = quality_from_roll(guild_rank(before.guild_done), before.up_level[UP_FORGE], rng_range(&t, 0, 99));
        int want_t = QUALITY_DEFS[want_q].has_trait ? trait_from_roll(rng_range(&t, 0, EQ_TRAIT_STANDARD_COUNT - 2))
                                                    : EQ_TRAIT_NONE;
        const Equipment *e = &g.inv[idx];
        CHECK_EQ(e->quality, want_q);
        CHECK_EQ(e->trait, want_t);
        CHECK_EQ(g.craft_rng, t); /* 1 回、Masterwork / Legendary では 2 回 */
        CHECK_EQ(g.rng, before.rng);
        CHECK_EQ(g.guild_rng, before.guild_rng);
        CHECK_EQ(g.day, before.day);
        CHECK(game_validate(&g));
        seen_q[e->quality]++;
        seen_t[e->trait]++;
        /* 保存された能力値はレシピからの build ちょうど 1 回分: 何も重ならず、価格はその能力値から求める */
        Equipment re;
        memset(&re, 0, sizeof re);
        re.type = eq;
        re.main = MAT_IRON;
        re.sub = MAT_WOOD;
        re.cost = e->cost;
        equipment_build(&re, 2, e->quality, e->trait);
        equipment_price(&re);
        re.price = re.rec_price;
        CHECK(!memcmp(&re, e, sizeof re));
        CHECK_EQ(e->price, e->rec_price);
        CHECK_EQ(g.money, before.money);
        CHECK_EQ(g.mat_stock[MAT_IRON], 9 - 2);
        CHECK_EQ(g.mat_stock[MAT_WOOD], 9 - 1);
    }
    for (int q = 0; q < QUALITY_COUNT; q++) {
        CHECK(seen_q[q] > 0); /* Grandmaster + Forge 2 はすべての品質を生む ... */
    }
    for (int t = 0; t < EQ_TRAIT_STANDARD_COUNT; t++) {
        CHECK(seen_t[t] > 0); /* ... そしてすべての通常特性も (固定シード: 決定的) */
    }
    for (int t = EQ_TRAIT_STANDARD_COUNT; t < EQ_TRAIT_COUNT; t++) {
        CHECK_EQ(seen_t[t], 0); /* Dragon Scale を使わない通常クラフトに、特殊特性は絶対に出ない */
    }
    CHECK_EQ(seen_t[EQ_TRAIT_NONE],
             seen_q[QUALITY_COMMON] + seen_q[QUALITY_FINE]); /* 特性を持つのは Masterwork / Legendary だけ */

    /* B: Apprentice は鍛冶場レベルに関わらず Legendary を決して引かない。Masterwork は出る */
    for (int forge = 0; forge <= 3; forge++) {
        GameState a = fresh();
        a.up_level[UP_FORGE] = forge;
        int lg = 0, mw = 0;
        for (int n = 0; n < 1500; n++) {
            a.mat_stock[MAT_IRON] = 3;
            a.mat_cost[MAT_IRON] = 30;
            a.inv_count = 0;
            CHECK_EQ(craft_equipment(&a, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
            lg += a.inv[0].quality == QUALITY_LEGENDARY;
            mw += a.inv[0].quality == QUALITY_MASTERWORK;
        }
        CHECK_EQ(lg, 0);
        CHECK(mw > 0);
    }
    /* C: 同じシード + 同じ操作 = 同じアイテム。シードが違えば結果も違う */
    GameState x, y, z;
    game_init(&x, 99);
    game_init(&y, 99);
    game_init(&z, 100);
    GameState *all[3] = {&x, &y, &z};
    for (int k = 0; k < 3; k++) {
        GameState *s = all[k];
        s->guild_done = 15;
        s->up_level[UP_FORGE] = 1;
        for (int n = 0; n < 20; n++) {
            s->mat_stock[MAT_IRON] = 3;
            s->mat_cost[MAT_IRON] = 30;
            s->inv_count = n % MAX_INVENTORY;
            craft_equipment(s, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
        }
    }
    CHECK(!memcmp(&x, &y, sizeof x));
    CHECK(memcmp(x.inv, z.inv, sizeof x.inv) != 0 || x.craft_rng != z.craft_rng);
    /* D: どの失敗経路でも、状態全体 (3 つの乱数すべてを含む) には触れない */
    GameState f = fresh();
    f.money = 500;
    f.mat_stock[MAT_IRON] = 2;
    f.mat_cost[MAT_IRON] = 20;
    GameState snap = f;
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_MATERIAL);
    CHECK(same_state(&f, &snap));
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, MAT_IRON, MAT_WOOD, NULL), R_ERR_MATERIAL);
    CHECK(same_state(&f, &snap));
    CHECK_EQ(craft_equipment(&f, EQ_WARHAMMER, MAT_IRON, MAT_IRON, NULL), R_ERR_LOCKED);
    CHECK(same_state(&f, &snap));
    CHECK_EQ(craft_equipment(&f, 99, MAT_IRON, MAT_IRON, NULL), R_ERR_RANGE);
    CHECK(same_state(&f, &snap));
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, -1, MAT_IRON, NULL), R_ERR_RANGE);
    CHECK(same_state(&f, &snap));
    f.mat_stock[MAT_IRON] = 9;
    f.mat_cost[MAT_IRON] = 90;
    snap = f;
    f.inv_count = MAX_INVENTORY;
    snap = f;
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_FULL);
    CHECK(same_state(&f, &snap));
    f.inv_count = 0;
    f.phase = PH_OPEN;
    f.queue_count = 1;
    snap = f;
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_PHASE);
    CHECK(same_state(&f, &snap));
    f.phase = PH_PREP;
    f.queue_count = 0;
    f.status = ST_CLEARED;
    snap = f;
    CHECK_EQ(craft_equipment(&f, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_ERR_PHASE);
    CHECK(same_state(&f, &snap));
}

static void test_quality_inventory(void) {
    puts("quality_inventory");
    GameState g = at_done(30);
    g.money = 100;
    int i0 = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_LEGENDARY, EQ_TRAIT_KEEN);
    int i1 = put_built(&g, EQ_SHIELD, MAT_STEEL, MAT_IRON, QUALITY_FINE, EQ_TRAIT_NONE);
    int i2 = put_built(&g, EQ_STAFF, MAT_MANA, MAT_WOOD, QUALITY_MASTERWORK, EQ_TRAIT_ARCANE);
    int i3 = put_built(&g, EQ_DAGGER, MAT_COPPER, MAT_WOOD, QUALITY_COMMON, EQ_TRAIT_NONE);
    Equipment keep[4] = {g.inv[i0], g.inv[i1], g.inv[i2], g.inv[i3]};
    CHECK(game_validate(&g));
    /* 緊急換金: 従来の原価ベースの価値を支払い、残りは品質/特性を失わずに詰める */
    GameState a = g;
    int got = 0;
    CHECK_EQ(inv_liquidate(&a, 1, &got), R_OK);
    CHECK_EQ(got, liquidation_value(&keep[1]));
    CHECK_EQ(got, keep[1].cost * BAL.liquidate_pct / 100); /* 品質は関与しない */
    CHECK_EQ(a.inv_count, 3);
    CHECK(!memcmp(&a.inv[0], &keep[0], sizeof keep[0]));
    CHECK(!memcmp(&a.inv[1], &keep[2], sizeof keep[2]));
    CHECK(!memcmp(&a.inv[2], &keep[3], sizeof keep[3]));
    CHECK_EQ(a.inv[1].quality, QUALITY_MASTERWORK);
    CHECK_EQ(a.inv[1].trait, EQ_TRAIT_ARCANE);
    CHECK_EQ(a.inv[3].quality, 0);
    CHECK_EQ(a.inv[3].trait, 0); /* 空いたスロットはクリアされる */
    CHECK(game_validate(&a));
    /* 価格の編集はアイテムの他の部分を保つ */
    a = g;
    CHECK_EQ(inv_set_price(&a, 0, 77), R_OK);
    CHECK_EQ(a.inv[0].quality, QUALITY_LEGENDARY);
    CHECK_EQ(a.inv[0].atk, keep[0].atk);
    CHECK_EQ(inv_reset_price(&a, 0), R_OK);
    CHECK(!memcmp(&a.inv[0], &keep[0], sizeof keep[0]));
    /* 通常の販売: 買い手が価格を支払い、売れたアイテムは去り、他のアイテムは品質を保つ */
    a = g;
    Customer cu = mk_customer(CT_MAGE, 9999, 1, 1300);
    a.queue_count = 0;
    day_open(&a);
    for (int i = 0; i < a.queue_count; i++) {
        a.queue[i] = cu;
    }
    SaleOutcome o;
    CHECK_EQ(customer_offer(&a, 2, &o), R_OK);
    CHECK(o.sold);
    CHECK_EQ(o.price, keep[2].price);
    CHECK_EQ(a.inv_count, 3);
    CHECK(!memcmp(&a.inv[0], &keep[0], sizeof keep[0]));
    CHECK(!memcmp(&a.inv[1], &keep[1], sizeof keep[1]));
    CHECK(!memcmp(&a.inv[2], &keep[3], sizeof keep[3]));
    CHECK_EQ(a.report.cogs, keep[2].cost);
    /* 途中にあるアイテムのギルド納品 */
    a = g;
    a.phase = PH_PREP;
    put_contract(&a, CT_SCOUT, 1, GD_EASY);
    CHECK_EQ(guild_accept(&a, 0), GUILD_OK);
    GuildOutcome go;
    CHECK_EQ(guild_deliver(&a, 0, 3, &go), GUILD_OK);
    CHECK_EQ(a.inv_count, 3);
    CHECK(!memcmp(&a.inv[0], &keep[0], sizeof keep[0]));
    CHECK(!memcmp(&a.inv[1], &keep[1], sizeof keep[1]));
    CHECK(!memcmp(&a.inv[2], &keep[2], sizeof keep[2]));
    CHECK_EQ(go.profit, go.gold - keep[3].cost);
    /* 品質付きアイテム全体が状態の一部: コピーは等しく、特性が変わった場合は等しくない */
    a = g;
    GameState b = g;
    CHECK(same_state(&a, &b));
    b.inv[0].trait = EQ_TRAIT_STURDY;
    CHECK(!same_state(&a, &b));
}

static void test_quality_validate(void) {
    puts("quality_validate");
    GameState g = at_done(30);
    put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
    CHECK(game_validate(&g));
    for (int q = 0; q < QUALITY_COUNT; q++) {
        for (int t = 0; t < EQ_TRAIT_COUNT; t++) { /* 有効な組み合わせは、仕様どおりのものだけ */
            GameState x = g;
            x.inv[0].quality = q;
            x.inv[0].trait = t;
            int ok = QUALITY_DEFS[q].has_trait ? t != EQ_TRAIT_NONE : t == EQ_TRAIT_NONE;
            if (t >= EQ_TRAIT_STANDARD_COUNT) {
                ok = 0; /* Iron 製の装備に Dragonforging の特性は付けられない */
            }
            CHECK_EQ(game_validate(&x), ok);
        }
    }
    GameState x = g;
    x.inv[0].quality = -1;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].quality = QUALITY_COUNT;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].trait = -1;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].trait = EQ_TRAIT_COUNT;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].quality = QUALITY_COMMON;
    x.inv[0].trait = EQ_TRAIT_KEEN;
    CHECK(!game_validate(&x)); /* Common は特性を持てない */
    x = g;
    x.inv[0].quality = QUALITY_FINE;
    x.inv[0].trait = EQ_TRAIT_KEEN;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].quality = QUALITY_MASTERWORK;
    x.inv[0].trait = EQ_TRAIT_NONE;
    CHECK(!game_validate(&x)); /* Masterwork には特性が必要 */
    x = g;
    x.craft_rng = 0;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].atk = -1;
    CHECK(!game_validate(&x));
    x = g;
    x.inv[0].wt = 10000;
    CHECK(!game_validate(&x));
    x = g;
    x.inv_count = 0;
    CHECK(game_validate(&x));
    /* ゼロ初期化された装備 (テスト、デモ、ボット) は有効な Common アイテム */
    Equipment z;
    memset(&z, 0, sizeof z);
    CHECK_EQ(z.quality, QUALITY_COMMON);
    CHECK_EQ(z.trait, EQ_TRAIT_NONE);
    /* デモ画面は掲示板をこのように用意する: 日付とランクを変えた後で更新し、GameState.rng には触れない */
    GameState d;
    game_init(&d, 4242);
    d.money = 800;
    d.rep = 45;
    d.day = 7;
    d.guild_done = 20;
    uint32_t rng0 = d.rng, craft0 = d.craft_rng;
    guild_refresh_board(&d);
    CHECK_EQ(d.rng, rng0);
    CHECK_EQ(d.craft_rng, craft0);
    CHECK_EQ(d.guild_board_count, GUILD_OFFER_COUNT);
    for (int i = 0; i < d.guild_board_count; i++) {
        CHECK(customer_unlocked(d.guild_done, d.guild_board[i].customer_type));
    }
    CHECK(game_validate(&d));
    GameState stale;
    game_init(&stale, 4242);
    stale.day = 7;
    stale.guild_done = 20; /* 更新しなければ、掲示板は 1 日目 / Apprentice のもの */
    CHECK_EQ(stale.guild_board[0].id, 1);
    CHECK_EQ(stale.guild_next_id, 4);
    CHECK_EQ(d.guild_board[0].id, 4);
    CHECK_EQ(d.guild_next_id, 7); /* 7 日目 / このランク用の新しい掲示板 */
}

static void test_quality_save(void) {
    puts("quality_save");
    CHECK_EQ(SAVE_VERSION, 4);
    CHECK_EQ(SAVE_MIN_VERSION, 1);
    GameState g = at_done(15);
    g.up_level[UP_FORGE] = 1;
    g.money = 900;
    g.rep = 33;
    g.day = 4;
    put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    put_built(&g, EQ_SHIELD, MAT_STEEL, MAT_IRON, QUALITY_FINE, EQ_TRAIT_NONE);
    put_built(&g, EQ_STAFF, MAT_MANA, MAT_WOOD, QUALITY_MASTERWORK, EQ_TRAIT_ARCANE);
    put_built(&g, EQ_DAGGER, MAT_COPPER, MAT_WOOD, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
    put_built(&g, EQ_WARHAMMER, MAT_IRON, MAT_STEEL, QUALITY_MASTERWORK, EQ_TRAIT_STURDY);
    put_built(&g, EQ_RUNE_BOW, MAT_OBSIDIAN, MAT_COPPER, QUALITY_LEGENDARY, EQ_TRAIT_KEEN);
    put_built(&g, EQ_SPEAR, MAT_STEEL, MAT_IRON, QUALITY_MASTERWORK, EQ_TRAIT_WARDING);
    g.craft_rng = 3141592653u;
    g.inv[1].price += 9;
    CHECK(game_validate(&g));
    char *t1 = save_text(&g);
    CHECK(!strncmp(t1, "LOOTSMITH_SAVE 4\n", 17));
    CHECK(strstr(t1, "\ncraft 3141592653\n") != NULL);
    CHECK(strstr(t1, "\nitem 0 0 0 ") != NULL);
    {
        int lines = 0, fields_ok = 1;
        const char *p = t1; /* すべての item 行は 14 個の数値を持つ */
        while ((p = strstr(p, "\nitem ")) != NULL) {
            p++;
            const char *e = strchr(p, '\n');
            int n = 0;
            for (const char *c = p; c < e; c++) {
                n += *c == ' ';
            }
            if (n != 14) {
                fields_ok = 0;
            }
            lines++;
        }
        CHECK_EQ(lines, 7);
        CHECK(fields_ok);
    }
    GameState r;
    memset(&r, 0xAB, sizeof r);
    CHECK_EQ(load_text(&r, t1), SAVE_OK);
    CHECK(same_state(&g, &r));
    CHECK_EQ(r.craft_rng, 3141592653u);
    for (int i = 0; i < g.inv_count; i++) {
        CHECK_EQ(r.inv[i].quality, g.inv[i].quality);
        CHECK_EQ(r.inv[i].trait, g.inv[i].trait);
        CHECK(!memcmp(&r.inv[i], &g.inv[i], sizeof r.inv[i]));
    }
    char *prev = t1;
    for (int round = 0; round < 4; round++) { /* 保存 -> 読み込み -> 保存でずれない */
        char *tx = save_text(&r);
        CHECK(!strcmp(tx, prev));
        GameState x;
        CHECK_EQ(load_text(&x, tx), SAVE_OK);
        CHECK(same_state(&x, &r));
        r = x;
        if (prev != t1) {
            free(prev);
        }
        prev = tx;
    }
    if (prev != t1) {
        free(prev);
    }
    /* 再読み込みしたゲームは、動き続けたゲームと同じ品質/特性を抽選する */
    GameState live = g, reloaded = r;
    for (int n = 0; n < 12; n++) {
        live.mat_stock[MAT_IRON] = reloaded.mat_stock[MAT_IRON] = 3;
        live.mat_cost[MAT_IRON] = reloaded.mat_cost[MAT_IRON] = 30;
        live.inv_count = reloaded.inv_count = 0;
        craft_equipment(&live, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
        craft_equipment(&reloaded, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
        CHECK(!memcmp(&live.inv[0], &reloaded.inv[0], sizeof live.inv[0]));
        CHECK_EQ(live.craft_rng, reloaded.craft_rng);
    }
    /* ファイルの往復 */
    const char *path = "test_quality_save.tmp";
    CHECK_EQ(game_save_file(&g, path), SAVE_OK);
    GameState lf;
    CHECK_EQ(game_load_file(&lf, path), SAVE_OK);
    CHECK(same_state(&g, &lf));
    remove(path);

    /* ---- 不正なデータは拒否され、出力先には決して触れない ---- */
    GameState keep = fresh(), probe = keep;
    char *bad;
#define REFUSE(prefix, newline, expect)                                                                                \
    do {                                                                                                               \
        bad = replace_line(t1, prefix, newline);                                                                       \
        probe = keep;                                                                                                  \
        SaveResult rr = load_text(&probe, bad);                                                                        \
        CHECK_EQ(rr, expect);                                                                                          \
        CHECK(same_state(&probe, &keep));                                                                              \
        free(bad);                                                                                                     \
    } while (0)
    char *good = save_text(&g);
    REFUSE("item 2 ", NULL, SAVE_ERR_FORMAT);                                      /* アイテムが欠けている (宣言は 7) */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 4 0", SAVE_ERR_VALUE);  /* 品質 ID 4 */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 -1 0", SAVE_ERR_VALUE); /* 品質 ID -1 */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 0 6", SAVE_ERR_VALUE);  /* 特性 ID 6 */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 0 -1", SAVE_ERR_VALUE); /* 特性 ID -1 */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 0 2", SAVE_ERR_VALUE);  /* 特性付きの Common */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 1 5", SAVE_ERR_VALUE);  /* 特性付きの Fine */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 2 0", SAVE_ERR_VALUE);  /* 特性なしの Masterwork */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 3 0", SAVE_ERR_VALUE);  /* 特性なしの Legendary */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74",
           SAVE_ERR_FORMAT); /* v3 のファイルの中の v2 形式のアイテム */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 0", SAVE_ERR_FORMAT);   /* 特性フィールドが欠けている */
    REFUSE("item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 x y", SAVE_ERR_FORMAT); /* 数値ではない */
    REFUSE("craft ", NULL, SAVE_ERR_FORMAT);                                       /* クラフトの乱数が欠けている */
    REFUSE("craft ", "craft 0", SAVE_ERR_VALUE);                                   /* すべて 0 の xorshift 状態は不正 */
    REFUSE("craft ", "craft abc", SAVE_ERR_FORMAT);
    REFUSE("craft ", "craft", SAVE_ERR_FORMAT);
    bad = add_line_after_header(t1, "craft 12345"); /* キーが 2 回 */
    probe = keep;
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
    bad = add_line_after_header(t1, "rng 5");
    probe = keep; /* v3: 他の 1 つしかないキーも繰り返せない */
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
    bad = add_line_after_header(t1, "money 5");
    probe = keep;
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
#undef REFUSE
    /* 壊れたセーブが中途半端に読み込まれることはない: 出力先に置いた目印のゴミは、上のどの拒否でも残る */
    GameState sentinel;
    memset(&sentinel, 0x5A, sizeof sentinel);
    GameState sentinel2 = sentinel;
    bad = replace_line(t1, "item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 9 9");
    CHECK(load_text(&sentinel, bad) != SAVE_OK);
    CHECK(!memcmp(&sentinel, &sentinel2, sizeof sentinel));
    free(bad);
    free(good);

    /* ファジング: 変異させた v3 のセーブが読み込めた場合、それは game_validate を通らなければならない
     * (不正な品質/特性がすり抜けない) */
    uint32_t seed = 4711;
    int survived = 1, accepted = 0;
    size_t len = strlen(t1);
    for (int n = 0; n < 1500; n++) {
        char *copy = malloc(len + 1);
        memcpy(copy, t1, len + 1);
        size_t at = (size_t)rng_range(&seed, 0, (int)len - 1);
        copy[at] = (char)("0123456789 -x\n"[rng_range(&seed, 0, 13)]);
        GameState pr;
        memset(&pr, 0, sizeof pr);
        if (load_text(&pr, copy) == SAVE_OK) {
            accepted++;
            if (!game_validate(&pr)) {
                survived = 0;
            }
            for (int i = 0; i < pr.inv_count; i++) {
                if (QUALITY_DEFS[pr.inv[i].quality].has_trait != (pr.inv[i].trait != 0)) {
                    survived = 0;
                }
            }
        }
        free(copy);
    }
    CHECK(survived);
    (void)accepted;
    free(t1);

    /* ---- バージョン 1 と 2 のセーブ ---- */
    GameState v1a, v1b;
    CHECK_EQ(load_text(&v1a, LEGACY_SAVE), SAVE_OK);
    CHECK_EQ(load_text(&v1b, LEGACY_SAVE), SAVE_OK);
    CHECK(same_state(&v1a, &v1b));              /* 決定的 */
    CHECK_EQ((long long)v1a.rng, 1454000782LL); /* ゲームの乱数は消費されていない */
    CHECK(v1a.craft_rng != 0 && v1a.craft_rng != v1a.rng && v1a.craft_rng != v1a.guild_rng);
    CHECK_EQ(v1a.guild_active_count, 0);
    CHECK_EQ(v1a.guild_board_count, GUILD_OFFER_COUNT); /* v1 -> v2 のギルド移行は今も行われる */
    for (int i = 0; i < v1a.inv_count; i++) {
        CHECK_EQ(v1a.inv[i].quality, QUALITY_COMMON);
        CHECK_EQ(v1a.inv[i].trait, EQ_TRAIT_NONE);
    }
    static const int v1_items[3][12] = {{0, 0, 1, 16, 7, 1, 14, 8, 24, 54, 65, 65},
                                        {1, 0, 0, 8, 18, 0, 18, 12, 27, 59, 71, 78},
                                        {2, 2, 1, 4, 3, 19, 6, 5, 44, 69, 83, 83}};
    for (int i = 0; i < 3; i++) {
        const Equipment *e = &v1a.inv[i];
        int got[12] = {e->type, e->main, e->sub,  e->atk,   e->def,       e->mag,
                       e->dur,  e->wt,   e->cost, e->value, e->rec_price, e->price};
        for (int k = 0; k < 12; k++) {
            CHECK_EQ(got[k], v1_items[i][k]); /* 能力値 / 原価 / 価値 / 価格は保存されたとおり */
        }
    }
    CHECK(game_validate(&v1a));
    GameState v2a, v2b;
    CHECK_EQ(load_text(&v2a, V2_SAVE), SAVE_OK);
    CHECK_EQ(load_text(&v2b, V2_SAVE), SAVE_OK);
    CHECK(same_state(&v2a, &v2b));
    CHECK_EQ((long long)v2a.rng, 2643077358LL);
    CHECK_EQ((long long)v2a.guild_rng, 3204434081LL); /* どちらのストリームも消費されていない */
    CHECK_EQ(v2a.guild_done, 15);
    CHECK_EQ(v2a.guild_next_id, 4);
    CHECK_EQ(v2a.guild_active_count, 1);
    CHECK_EQ(v2a.guild_board_count, 2);
    CHECK_EQ(v2a.guild_active[0].id, 1);
    CHECK_EQ(v2a.guild_board[0].id, 2);
    CHECK_EQ(v2a.guild_board[1].id, 3); /* 契約には触れない (移行による再抽選なし) */
    CHECK(v2a.craft_rng != 0 && v2a.craft_rng != v2a.rng && v2a.craft_rng != v2a.guild_rng);
    CHECK_EQ(v2a.inv_count, 4);
    static const int v2_items[4][12] = {{0, 0, 1, 16, 7, 1, 14, 8, 26, 55, 67, 67},
                                        {2, 2, 1, 4, 3, 19, 6, 5, 46, 70, 85, 92},
                                        {6, 0, 0, 21, 11, 0, 18, 13, 30, 68, 82, 82},
                                        {4, 4, 0, 19, 12, 0, 19, 10, 34, 70, 85, 85}};
    for (int i = 0; i < 4; i++) {
        const Equipment *e = &v2a.inv[i];
        int got[12] = {e->type, e->main, e->sub,  e->atk,   e->def,       e->mag,
                       e->dur,  e->wt,   e->cost, e->value, e->rec_price, e->price};
        for (int k = 0; k < 12; k++) {
            CHECK_EQ(got[k], v2_items[i][k]);
        }
        CHECK_EQ(e->quality, QUALITY_COMMON);
        CHECK_EQ(e->trait, EQ_TRAIT_NONE);
    }
    CHECK(game_validate(&v2a));
    /* 古い能力値は再導出しない: どのレシピ/鍛冶場でも作れないアイテムは、その数値、価値、価格を保つ */
    bad = replace_line(V2_SAVE, "item 0 ", "item 0 0 1 77 7 1 14 8 26 5 3 4");
    GameState odd;
    CHECK_EQ(load_text(&odd, bad), SAVE_OK);
    free(bad);
    CHECK_EQ(odd.inv[0].atk, 77);
    CHECK_EQ(odd.inv[0].value, 5);
    CHECK_EQ(odd.inv[0].rec_price, 3);
    CHECK_EQ(odd.inv[0].price, 4);
    CHECK_EQ(odd.inv[0].quality, QUALITY_COMMON);
    /* 古いファイルは最新の形式 (v4) として再保存でき、同一に再読み込みでき、遊び続けられる */
    char *up = save_text(&v2a);
    CHECK(!strncmp(up, "LOOTSMITH_SAVE 4\n", 17));
    CHECK(strstr(up, "\ncraft ") != NULL);
    GameState up2;
    CHECK_EQ(load_text(&up2, up), SAVE_OK);
    CHECK(same_state(&v2a, &up2));
    free(up);
    GameState played = v2a;
    day_close_early(&played);
    CHECK_EQ(day_advance(&played), R_OK);
    CHECK(game_validate(&played));
    played.mat_stock[MAT_IRON] = 3;
    played.mat_cost[MAT_IRON] = 30;
    CHECK_EQ(craft_equipment(&played, EQ_SWORD, MAT_IRON, MAT_IRON, NULL), R_OK);
    CHECK(game_validate(&played));
    /* 古いファイルから手で導出: v2 のファイルの中の v3 風の item 行は、古いアイテムとして読む (余分なフィールドは無視)
     */
    char *mix = replace_line(V2_SAVE, "item 0 ", "item 0 0 1 16 7 1 14 8 26 55 67 67 3 2");
    GameState mixed;
    CHECK_EQ(load_text(&mixed, mix), SAVE_OK);
    CHECK_EQ(mixed.inv[0].quality, QUALITY_COMMON);
    CHECK_EQ(mixed.inv[0].trait, EQ_TRAIT_NONE);
    free(mix);
    /* すでに "craft" 行を含む v2 ファイルは、それを使わない (未知のキー) */
    char *stray = add_line_after_header(V2_SAVE, "craft 99");
    GameState st;
    CHECK_EQ(load_text(&st, stray), SAVE_OK);
    CHECK(st.craft_rng != 99);
    free(stray);
    /* 移行自体は何も消費せず、読み込んだ状態の純粋関数になる */
    GameState m = v2a;
    uint32_t cr = m.craft_rng;
    craft_migrate_legacy(&m);
    CHECK_EQ(m.craft_rng, cr);
    CHECK_EQ(m.rng, v2a.rng);
    CHECK_EQ(m.guild_rng, v2a.guild_rng);
    /* 壊れた古いセーブは、やはり拒否され、出力先には触れない */
    probe = keep;
    bad = replace_line(V2_SAVE, "item 0 ", "item 99 0 1 16 7 1 14 8 26 55 67 67");
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
    CHECK(same_state(&probe, &keep));
    free(bad);
    bad = replace_line(V2_SAVE, "gboard ", NULL);
    CHECK(load_text(&probe, bad) != SAVE_OK);
    CHECK(same_state(&probe, &keep));
    free(bad);
    bad = replace_line(V2_SAVE, "item 0 ", "item 0 0 1 16 7");
    CHECK_EQ(load_text(&probe, bad), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(bad);
}

/* ======================================================================
 * コードレビューでの修正: 極端な値の検証、飽和するカウンタ、暦の上限、数値の解析、シード
 * ====================================================================== */
static GameState with_one_customer_and_item(void) { /* 1 日の途中で、売るアイテムが 1 つ */
    GameState g = fresh();
    g.money = 1000;
    int it = put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    (void)it;
    force_open(&g, mk_customer(CT_WARRIOR, 5000, 1, 1300));
    return g;
}
static void test_review_validate_limits(void) {
    puts("review_validate_limits");
    GameState g = fresh();
    CHECK(game_validate(&g));
    /* #1: swing_pm: INT_MIN / INT_MAX と正確な境界 (abs(INT_MIN) はかつて未定義動作だった) */
    static const int swing_bad[] = {INT_MIN, INT_MIN + 1, -1001, 1001, INT_MAX, INT_MAX - 1};
    for (size_t i = 0; i < sizeof swing_bad / sizeof *swing_bad; i++) {
        GameState x = g;
        x.swing_pm[MAT_IRON] = swing_bad[i];
        CHECK(!game_validate(&x));
    }
    {
        GameState x = g;
        x.swing_pm[MAT_IRON] = 1000;
        CHECK(game_validate(&x));
        x.swing_pm[MAT_IRON] = -1000;
        CHECK(game_validate(&x));
    }
    /* #2: 累積するすべての値に範囲がある。極端な値は拒否される (単に「正の値」ではない) */
    struct {
        const char *what;
        int bad;
    } t[40];
    int nt = 0;
    (void)t;
    (void)nt;
#define BAD(field, value)                                                                                              \
    do {                                                                                                               \
        GameState x = g;                                                                                               \
        x.field = (value);                                                                                             \
        CHECK(!game_validate(&x));                                                                                     \
    } while (0)
#define OK(field, value)                                                                                               \
    do {                                                                                                               \
        GameState x = g;                                                                                               \
        x.field = (value);                                                                                             \
        CHECK(game_validate(&x));                                                                                      \
    } while (0)
    BAD(total_revenue, INT_MAX);
    BAD(total_revenue, STAT_CAP + 1);
    BAD(total_revenue, -1);
    OK(total_revenue, STAT_CAP);
    BAD(total_sold, INT_MAX);
    BAD(total_sold, STAT_CAP + 1);
    OK(total_sold, STAT_CAP);
    BAD(total_profit, INT_MAX);
    BAD(total_profit, INT_MIN);
    BAD(total_profit, STAT_CAP + 1);
    BAD(total_profit, -STAT_CAP - 1);
    OK(total_profit, STAT_CAP);
    OK(total_profit, -STAT_CAP);
    BAD(guild_done, INT_MAX);
    BAD(guild_done, STAT_CAP + 1);
    OK(guild_done, STAT_CAP);
    BAD(guild_expired, INT_MAX);
    BAD(guild_expired, STAT_CAP + 1);
    OK(guild_expired, STAT_CAP);
    BAD(report.revenue, INT_MAX);
    BAD(report.revenue, DAY_CAP + 1);
    BAD(report.revenue, -1);
    OK(report.revenue, DAY_CAP);
    BAD(report.cogs, INT_MAX);
    BAD(report.cogs, -1);
    OK(report.cogs, DAY_CAP);
    BAD(report.upkeep, INT_MAX);
    BAD(report.upkeep, INT_MIN);
    OK(report.upkeep, DAY_CAP);
    BAD(report.guild_gold, INT_MAX);
    BAD(report.guild_gold, DAY_CAP + 1);
    OK(report.guild_gold, DAY_CAP);
    BAD(report.rep_delta, INT_MAX);
    BAD(report.rep_delta, INT_MIN);
    OK(report.rep_delta, DAY_CAP);
    OK(report.rep_delta, -DAY_CAP);
    BAD(report.money_start, INT_MAX);
    BAD(report.money_start, INT_MIN);
    OK(report.money_start, DAY_CAP);
    BAD(report.customers, MAX_CUSTOMERS_PER_DAY + 1);
    BAD(report.customers, -1);
    BAD(report.sold, MAX_CUSTOMERS_PER_DAY + 1);
    BAD(report.sold, INT_MAX);
    BAD(report.guild_done, INT_MAX);
    BAD(report.guild_expired, INT_MAX);
    BAD(report.guild_done, -1);
    BAD(money, INT_MAX);
    BAD(money, INT_MIN);
    BAD(money, DAY_CAP + 1);
    BAD(money, MONEY_FLOOR - 1);
    OK(money, DAY_CAP);
    OK(money, MONEY_FLOOR);
    BAD(day, 0);
    BAD(day, MAX_DAY + 1);
    BAD(day, INT_MAX);
    OK(day, MAX_DAY);
#undef BAD
#undef OK
    CHECK_EQ(MAX_DAY, 9999);
    /* レビューでの再現: report.revenue = INT_MAX かつ total_profit = 1000 は、かつて検証と保存/読み込みを通ってしまった
     */
    GameState r = g;
    r.report.revenue = INT_MAX;
    r.total_profit = 1000;
    CHECK(!game_validate(&r));
    char *txt = save_text(&r); /* game_save は渡されたものをそのまま書く ... */
    GameState keep = fresh(), probe = keep;
    CHECK(load_text(&probe, txt) != SAVE_OK);
    CHECK(same_state(&probe, &keep)); /* ... だがローダは拒否し、対象には触れない */
    free(txt);
}

static void test_review_saturation(void) {
    puts("review_saturation");
    /* カウンタは上限ちょうどに位置してもオーバーフローせずに動き続ける (符号付きオーバーフローは UBSan が報告する) */
    GameState g = with_one_customer_and_item();
    g.total_revenue = STAT_CAP - 3;
    g.total_sold = STAT_CAP;
    g.total_profit = STAT_CAP - 1;
    g.report.revenue = DAY_CAP - 1;
    g.report.sold = MAX_CUSTOMERS_PER_DAY;
    g.report.cogs = DAY_CAP;
    g.money = DAY_CAP - 2;
    CHECK(game_validate(&g));
    SaleOutcome o;
    CHECK_EQ(customer_offer(&g, 0, &o), R_OK);
    CHECK(o.sold);
    CHECK_EQ(g.total_sold, STAT_CAP);
    CHECK_EQ(g.total_revenue, STAT_CAP); /* 巻き戻らず飽和する */
    CHECK_EQ(g.report.revenue, DAY_CAP);
    CHECK_EQ(g.report.cogs, DAY_CAP);
    CHECK_EQ(g.report.sold, MAX_CUSTOMERS_PER_DAY);
    CHECK_EQ(g.money, DAY_CAP);
    CHECK(game_validate(&g));
    day_close_early(&g); /* 精算は net_of() を total_profit に加える */
    CHECK(g.total_profit <= STAT_CAP && g.total_profit >= -STAT_CAP);
    CHECK(game_validate(&g));
    char *txt = save_text(&g);
    GameState l;
    CHECK_EQ(load_text(&l, txt), SAVE_OK);
    CHECK(same_state(&g, &l));
    free(txt);
    /* 利益は反対側の端でも飽和する */
    GameState n = fresh();
    n.total_profit = -STAT_CAP + 1;
    n.report.cogs = DAY_CAP;
    n.report.upkeep = 0;
    n.money = 5000;
    n.report.money_start = 5000;
    day_open(&n);
    day_close_early(&n);
    CHECK(n.total_profit >= -STAT_CAP);
    CHECK(game_validate(&n));
    /* 契約: 上限にある guild_done はそこに留まる */
    GameState c = at_done(STAT_CAP);
    c.phase = PH_PREP;
    c.money = 1000;
    int it = put_built(&c, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    put_contract(&c, CT_WARRIOR, 1, GD_EASY);
    CHECK_EQ(guild_accept(&c, 0), GUILD_OK);
    c.report.guild_done = STAT_CAP;
    c.report.guild_gold = DAY_CAP - 1;
    c.report.rep_delta = DAY_CAP;
    CHECK(game_validate(&c));
    GuildOutcome go;
    CHECK_EQ(guild_deliver(&c, 0, it, &go), GUILD_OK);
    CHECK_EQ(c.guild_done, STAT_CAP);
    CHECK_EQ(c.report.guild_done, STAT_CAP);
    CHECK_EQ(c.report.guild_gold, DAY_CAP);
    CHECK_EQ(c.report.rep_delta, DAY_CAP);
    CHECK(game_validate(&c));
    /* 上限での失効した契約 */
    GameState e = at_done(0);
    e.guild_expired = STAT_CAP;
    e.report.guild_expired = STAT_CAP;
    e.phase = PH_PREP;
    put_contract(&e, CT_WARRIOR, 1, GD_EASY);
    guild_accept(&e, 0);
    e.guild_active[0].deadline_day = e.day;
    day_open(&e);
    day_close_early(&e);
    CHECK_EQ(e.guild_expired, STAT_CAP);
    CHECK(game_validate(&e));
    /* 維持費で所持金が下限を下回ることはない */
    GameState f = fresh();
    f.status = ST_SANDBOX;
    f.money = MONEY_FLOOR;
    f.report.money_start = MONEY_FLOOR;
    day_open(&f);
    day_close_early(&f);
    CHECK(f.money >= MONEY_FLOOR);
    CHECK(game_validate(&f)); /* (サンドボックスは借金さえ 0 まで引き上げる) */
    /* 極端な差分での inv_adjust_price: 丸められ、整数オーバーフローしない */
    GameState p = fresh();
    put_built(&p, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_COMMON, EQ_TRAIT_NONE);
    CHECK_EQ(inv_adjust_price(&p, 0, INT_MAX), R_OK);
    CHECK_EQ(p.inv[0].price, BAL.price_cap);
    CHECK_EQ(inv_adjust_price(&p, 0, INT_MIN), R_OK);
    CHECK_EQ(p.inv[0].price, 1);
    CHECK(game_validate(&p));
}

static void test_review_calendar(void) {
    puts("review_calendar");
    /* 最終日までのサンドボックスプレイ: 到達可能なすべての状態は、保存でき、かつ再び読み込める */
    GameState g = fresh();
    g.status = ST_SANDBOX;
    g.day = MAX_DAY - 2;
    g.money = 5000;
    g.report.money_start = 5000;
    for (int step = 0; step < 2; step++) {
        day_open(&g);
        day_close_early(&g);
        CHECK_EQ(day_advance(&g), R_OK);
        CHECK(game_validate(&g));
        char *txt = save_text(&g);
        GameState l;
        CHECK_EQ(load_text(&l, txt), SAVE_OK);
        char *again = save_text(&l);
        CHECK(!strcmp(txt, again));
        free(again);
        free(txt); /* (テキストで比較: 使われていない待ち行列スロットは保存されない) */
    }
    CHECK_EQ(g.day, MAX_DAY);
    day_open(&g);
    day_close_early(&g); /* 最終日はまだ遊べる ... */
    GameState before = g;
    CHECK_EQ(day_advance(&g), R_ERR_LIMIT); /* ... が 10000 日目は存在しない */
    CHECK(same_state(&g, &before));
    CHECK_EQ(g.day, MAX_DAY);
    CHECK(game_validate(&g));
    CHECK_EQ(g.phase, PH_SETTLE);
    char *txt = save_text(&g);
    GameState l;
    CHECK_EQ(load_text(&l, txt), SAVE_OK);
    CHECK_EQ(l.day, MAX_DAY);
    CHECK(game_validate(&l));
    CHECK_EQ(day_advance(&l), R_ERR_LIMIT);
    free(txt); /* 再読み込みの後も安定している */
    const char *path = "test_calendar.tmp";
    CHECK_EQ(game_save_file(&g, path), SAVE_OK);
    GameState lf;
    CHECK_EQ(game_load_file(&lf, path), SAVE_OK);
    CHECK_EQ(lf.day, MAX_DAY);
    remove(path);
    /* 通常の 30 日キャンペーンは影響を受けない */
    GameState c = fresh();
    CHECK_EQ(c.status, ST_PLAYING);
    pass_day(&c);
    CHECK_EQ(c.day, 2);
}

static void test_review_parsing(void) {
    puts("review_parsing");
    GameState g = fresh();
    g.money = 777;
    put_built(&g, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_FINE, EQ_TRAIT_NONE);
    char *good = save_text(&g);
    GameState keep = fresh(), probe;
    struct {
        const char *prefix, *line;
        SaveResult want;
    } bad[] = {
        {"money ", "money 2147483648", SAVE_ERR_FORMAT}, /* INT_MAX を 1 超える */
        {"money ", "money -2147483649", SAVE_ERR_FORMAT},
        {"money ", "money 99999999999999999999999", SAVE_ERR_FORMAT}, /* 64 ビットにも収まらない */
        {"money ", "money 12x", SAVE_ERR_FORMAT},                     /* トークンの中に余分なゴミがある */
        {"money ", "money 0x10", SAVE_ERR_FORMAT},                    /* 10 進のみ */
        {"money ", "money 1e3", SAVE_ERR_FORMAT},
        {"money ", "money --5", SAVE_ERR_FORMAT},
        {"money ", "money", SAVE_ERR_FORMAT},
        {"money ", "money -", SAVE_ERR_FORMAT},
        {"money ", "money 2147483647", SAVE_ERR_VALUE}, /* 解析はできるが、有効な額ではない */
        {"money ", "money -2147483648", SAVE_ERR_VALUE},
        {"day ", "day 4294967297", SAVE_ERR_FORMAT},
        {"rng ", "rng 4294967296", SAVE_ERR_FORMAT}, /* uint32 に収まらない */
        {"rng ", "rng -1", SAVE_ERR_FORMAT},         /* 負の値が巻き戻らない */
        {"rng ", "rng 99999999999999999999", SAVE_ERR_FORMAT},
        {"craft ", "craft 4294967296", SAVE_ERR_FORMAT},
        {"craft ", "craft -5", SAVE_ERR_FORMAT},
        {"mat 0 ", "mat 0 3 30 9 9 -2147483648", SAVE_ERR_VALUE}, /* INT_MIN の swing: 拒否され、未定義動作もない */
        {"mat 0 ", "mat 0 3 30 9 9 2147483647", SAVE_ERR_VALUE},
        {"mat 0 ", "mat 0 3 30 9 9 99999999999", SAVE_ERR_FORMAT},
        {"tot ", "tot 0 0 2147483647", SAVE_ERR_VALUE}, /* レビューでの profit のケース */
        {"tot ", "tot 2147483647 0 0", SAVE_ERR_VALUE},
        {"tot ", "tot 0 0 -2147483648", SAVE_ERR_VALUE},
        {"report ", "report 3 0 2147483647 0 0 0 250", SAVE_ERR_VALUE}, /* レビューでの revenue のケース */
        {"report ", "report 3 0 0 0 2147483647 0 250", SAVE_ERR_VALUE},
        {"greport ", "greport 2147483647 0 0", SAVE_ERR_VALUE},
        {"guild ", "guild 5 7 2147483647 0", SAVE_ERR_VALUE}, /* guild_done = INT_MAX */
        {"guild ", "guild 5 7 0 2147483647", SAVE_ERR_VALUE},
        {"guild ", "guild 4294967296 7 0 0", SAVE_ERR_FORMAT},
        {"inv ", "inv 99999999999", SAVE_ERR_FORMAT},
        {"item 0 ", "item 0 0 0 18 9 0 16 10 20 62 74 74 0 99999999999", SAVE_ERR_FORMAT},
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        char *t = replace_line(good, bad[i].prefix, bad[i].line);
        probe = keep;
        SaveResult r = load_text(&probe, t);
        if (r != bad[i].want) {
            printf("  (case %zu: got %d, wanted %d: %s)\n", i, (int)r, (int)bad[i].want, bad[i].line);
        }
        CHECK_EQ(r, bad[i].want);
        CHECK(same_state(&probe, &keep));
        free(t);
    }
    /* ヘッダ */
    static const char *hdr[] = {
        "LOOTSMITH_SAVE\nend\n",    "LOOTSMITH_SAVE x\nend\n",  "LOOTSMITH_SAVE 99999999999999999999\nend\n",
        "LOOTSMITH_SAVEX 3\nend\n", "LOOTSMITH_SAVE 3x\nend\n", "LOOTSMITH_SAVE -9999999999\nend\n"};
    for (size_t i = 0; i < sizeof hdr / sizeof *hdr; i++) {
        probe = keep;
        CHECK(load_text(&probe, hdr[i]) != SAVE_OK);
        CHECK(same_state(&probe, &keep));
    }
    /* 有効な極端値は、今も読み込める: ジェネレータの uint32 最大値、末尾の空白 / CRLF、プラス記号 */
    char *edge = replace_line(good, "rng ", "rng 4294967295");
    probe = keep;
    CHECK_EQ(load_text(&probe, edge), SAVE_OK);
    CHECK_EQ((long long)probe.rng, 4294967295LL);
    free(edge);
    edge = replace_line(good, "craft ", "craft 4294967295  ");
    CHECK_EQ(load_text(&probe, edge), SAVE_OK);
    CHECK_EQ((long long)probe.craft_rng, 4294967295LL);
    free(edge);
    edge = replace_line(good, "money ", "money +777\r");
    CHECK_EQ(load_text(&probe, edge), SAVE_OK);
    CHECK_EQ(probe.money, 777);
    free(edge);
    edge = replace_line(good, "tot ", "tot 1000000000 1000000000 -1000000000");
    CHECK_EQ(load_text(&probe, edge), SAVE_OK);
    CHECK_EQ(probe.total_revenue, STAT_CAP);
    CHECK_EQ(probe.total_profit, -STAT_CAP);
    free(edge);
    /* 古い形式は、従来とまったく同じように解析される */
    CHECK_EQ(load_text(&probe, LEGACY_SAVE), SAVE_OK);
    CHECK_EQ(load_text(&probe, V2_SAVE), SAVE_OK);
    /* 数千回のランダムな 1 文字の変異 (オーバーフローする数字を含む): 読み込めたものは検証を通らなければならない */
    uint32_t seed = 2024;
    size_t len = strlen(good);
    int ok = 1;
    for (int n = 0; n < 3000; n++) {
        char *copy = malloc(len + 12);
        memcpy(copy, good, len + 1);
        size_t at = (size_t)rng_range(&seed, 0, (int)len - 1);
        if (n % 3 == 0) {
            memmove(copy + at + 10, copy + at, len - at + 1);
            memcpy(copy + at, "9999999999", 10);
        } /* 巨大な数 */
        else {
            copy[at] = "0123456789 -+x\n"[rng_range(&seed, 0, 14)];
        }
        GameState pr;
        memset(&pr, 0, sizeof pr);
        if (load_text(&pr, copy) == SAVE_OK && !game_validate(&pr)) {
            ok = 0;
        }
        free(copy);
    }
    CHECK(ok);
    free(good);
}

static void test_review_seed(void) {
    puts("review_seed");
    /* --seed N: 最初のキャンペーンはちょうど N から始まる (UI は N をそのまま渡す) ので、同じシードなら同じゲームになる
     */
    for (uint32_t s = 1; s <= 50; s++) {
        GameState a, b;
        game_init(&a, s * 31337u);
        game_init(&b, s * 31337u);
        CHECK(same_state(&a, &b)); /* 価格、契約、3 つの乱数状態すべて */
        CHECK(memcmp(a.price, b.price, sizeof a.price) == 0);
        CHECK_EQ(a.rng, b.rng);
        CHECK_EQ(a.guild_rng, b.guild_rng);
        CHECK_EQ(a.craft_rng, b.craft_rng);
        CHECK(!memcmp(a.guild_board, b.guild_board, sizeof a.guild_board));
        day_open(&a);
        day_open(&b);
        CHECK(same_state(&a, &b)); /* ... そして 1 日目の客 */
    }
    /* 以後のキャンペーンは campaign_seed() から来る: (直前のシード, 時計) の純粋関数 */
    CHECK_EQ(campaign_seed(12345, 0), 12345u * 1664525u + 1013904223u);
    CHECK_EQ(campaign_seed(12345, 99), campaign_seed(12345, 0) + 99);
    CHECK(campaign_seed(12345, 1) != campaign_seed(12345, 2));
    CHECK(campaign_seed(1, 7) != campaign_seed(2, 7));
    CHECK_EQ(campaign_seed(7, 1000), campaign_seed(7, 1000));
}

static void test_review2_guild_ids(void) {
    puts("review2_guild_ids");
    CHECK_EQ(GUILD_ID_MAX, 1000000000);
    /* 検証関数が受理するすべての ID (GUILD_ID_MAX まで) は、何度日が変わっても生き残らなければならない */
    for (int start = GUILD_ID_MAX - 8; start <= GUILD_ID_MAX; start++) {
        GameState g = fresh();
        g.status = ST_SANDBOX;
        g.guild_next_id = start;
        g.phase = PH_PREP;
        g.guild_board_count = 0;
        memset(g.guild_board, 0, sizeof g.guild_board);
        guild_refresh_board(&g); /* この ID カウンタ用の掲示板 (必要なら振り直す) */
        CHECK(game_validate(&g));
        guild_accept(&g, 0);
        guild_accept(&g, 0); /* 受注中の 2 件の契約は、日をまたいでも ID を保つ */
        for (int d = 0; d < 12; d++) {
            day_open(&g);
            day_close_early(&g);
            CHECK_EQ(day_advance(&g), R_OK);
            CHECK(game_validate(&g));
            CHECK(g.guild_next_id <= GUILD_ID_MAX);
            char *txt = save_text(&g);
            GameState l;
            CHECK_EQ(load_text(&l, txt), SAVE_OK);
            free(txt);
        }
    }
    /* レビューでのケース: next_id がちょうど上限で、契約を保持しており、そこで日が変わる */
    GameState g = fresh();
    g.status = ST_SANDBOX;
    put_contract(&g, CT_WARRIOR, 40, GD_STANDARD);
    g.guild_board[0].id = GUILD_ID_MAX - 1;
    g.guild_next_id = GUILD_ID_MAX;
    CHECK(game_validate(&g));
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    int fit = g.guild_active[0].min_fit, ct = g.guild_active[0].customer_type, gold = g.guild_active[0].gold,
        dl = g.guild_active[0].deadline_day;
    day_open(&g);
    day_close_early(&g);
    CHECK(game_validate(&g));
    CHECK_EQ(g.guild_next_id, GUILD_ID_MAX); /* day_advance は上限を超えてしまう ... */
    CHECK_EQ(day_advance(&g), R_OK);
    CHECK(game_validate(&g)); /* ... だから ID は振り直された */
    CHECK_EQ(g.guild_active_count, 1);
    CHECK_EQ(g.guild_active[0].id, 1);
    CHECK_EQ(g.guild_active[0].min_fit, fit);
    CHECK_EQ(g.guild_active[0].customer_type, ct); /* 変わったのは ID だけ */
    CHECK_EQ(g.guild_active[0].gold, gold);
    CHECK_EQ(g.guild_active[0].deadline_day, dl);
    CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);
    CHECK_EQ(g.guild_next_id, 1 + 1 + GUILD_OFFER_COUNT);
    for (int i = 0; i < g.guild_board_count; i++) {
        CHECK(g.guild_board[i].id > 1 && g.guild_board[i].id < g.guild_next_id);
    }
    /* 振り直し後も、受注中の契約は納品できる */
    g.phase = PH_PREP;
    int it = put_built(&g, EQ_SWORD, MAT_STEEL, MAT_STEEL, QUALITY_LEGENDARY, EQ_TRAIT_KEEN);
    if (g.guild_active[0].customer_type == CT_WARRIOR) {
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
    }
    /* 通常のプレイは上限に近づくことはなく影響を受けない: ID は今も 1,2,3,... と進む */
    GameState n = fresh();
    CHECK_EQ(n.guild_next_id, 4);
    pass_day(&n);
    CHECK_EQ(n.guild_next_id, 7);
}

static void test_review2_sandbox_continue(void) {
    puts("review2_sandbox_continue");
    /* 最終日でクリア済み、精算待ちの状態: 有効 ... */
    GameState g = fresh();
    g.status = ST_CLEARED;
    g.day = MAX_DAY;
    g.phase = PH_SETTLE;
    g.money = 5000;
    g.rep = 80;
    CHECK(game_validate(&g));
    GameState before = g;
    CHECK_EQ(campaign_continue_sandbox(&g), R_ERR_LIMIT); /* ... そして失敗した呼び出しは、そのどれも変えてはならない */
    CHECK(same_state(&g, &before));
    CHECK_EQ(g.status, ST_CLEARED);
    CHECK_EQ(g.day, MAX_DAY);
    CHECK(game_validate(&g));
    /* 他の失敗経路も同様 */
    GameState w = fresh();
    GameState wb = w; /* クリアしていない / フェーズが違う */
    CHECK_EQ(campaign_continue_sandbox(&w), R_ERR_PHASE);
    CHECK(same_state(&w, &wb));
    w.status = ST_CLEARED;
    w.phase = PH_PREP;
    wb = w;
    CHECK_EQ(campaign_continue_sandbox(&w), R_ERR_PHASE);
    CHECK(same_state(&w, &wb));
    w.status = ST_FAILED;
    w.fail_reason = FAIL_TIMEOUT;
    w.phase = PH_SETTLE;
    wb = w;
    CHECK_EQ(campaign_continue_sandbox(&w), R_ERR_PHASE);
    CHECK(same_state(&w, &wb));
    /* 上限の 1 日前ならまだ動き、結果も保存できる */
    GameState ok = fresh();
    ok.status = ST_CLEARED;
    ok.day = MAX_DAY - 1;
    ok.phase = PH_SETTLE;
    ok.money = 5000;
    ok.rep = 80;
    CHECK(game_validate(&ok));
    CHECK_EQ(campaign_continue_sandbox(&ok), R_OK);
    CHECK_EQ(ok.status, ST_SANDBOX);
    CHECK_EQ(ok.day, MAX_DAY);
    CHECK(game_validate(&ok));
    char *txt = save_text(&ok);
    GameState l;
    CHECK_EQ(load_text(&l, txt), SAVE_OK);
    CHECK_EQ(l.status, ST_SANDBOX);
    free(txt);
    /* 通常の 30 日キャンペーン: クリア後に続行する */
    GameState c = fresh();
    c.status = ST_CLEARED;
    c.phase = PH_SETTLE;
    c.day = 20;
    c.money = 5000;
    c.rep = 80;
    CHECK_EQ(campaign_continue_sandbox(&c), R_OK);
    CHECK_EQ(c.day, 21);
    CHECK_EQ(c.status, ST_SANDBOX);
}

/* ---- ボットによるプレイスルー: バランスの健全性 ---- */
typedef struct {
    int day;
    int status;
    int money, rep, sold;
    long revenue, guild_gold;
} BotResult; /* revenue = 通常の販売、guild_gold = 契約の報酬 */

static int bot_pick(const GameState *g, const Customer *c, int skip_mask) {
    int best = -1, bfit = -1;
    for (int i = 0; i < g->inv_count; i++) {
        if (skip_mask & (1 << i)) {
            continue;
        }
        if (g->inv[i].type != CUSTOMER_DEFS[c->type].wants_eq) {
            continue;
        }
        int fit = equip_fit(c->type, &g->inv[i]);
        if (fit > bfit) {
            bfit = fit;
            best = i;
        }
    }
    return best;
}
/* ボット戦略: 装備ごとに 2 つの {main, sub} バリエーションを交互にクラフトし、その装備を在庫にいくつ持つかを決める。
 * NULL の戦略は放置ボット (店を開けるだけで何もしない)。 */
typedef struct {
    int plan[EQ_COUNT][2][2];
    int target[EQ_COUNT];
} Strategy;

/* 元の戦略 (変更なし): 出荷時の装備 3 種と出荷時の素材 3 種だけ。 */
static const Strategy STRAT_LEGACY = {.plan = {[EQ_SWORD] = {{MAT_IRON, MAT_IRON}, {MAT_WOOD, MAT_IRON}},
                                               [EQ_SHIELD] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}},
                                               [EQ_STAFF] = {{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}}},
                                      .target = {[EQ_SWORD] = 3, [EQ_SHIELD] = 2, [EQ_STAFF] = 2}};
/* すべての客タイプに対応するが、出荷時の素材だけを使う。 */
static const Strategy STRAT_BROAD = {
    .plan = {[EQ_SWORD] = {{MAT_IRON, MAT_IRON}, {MAT_WOOD, MAT_IRON}},
             [EQ_SHIELD] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}},
             [EQ_STAFF] = {{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}},
             [EQ_DAGGER] = {{MAT_IRON, MAT_WOOD}, {MAT_IRON, MAT_IRON}},
             [EQ_SPEAR] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}},
             [EQ_GRIMOIRE] = {{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}}},
    .target = {[EQ_SWORD] = 2, [EQ_SHIELD] = 1, [EQ_STAFF] = 1, [EQ_DAGGER] = 1, [EQ_SPEAR] = 1, [EQ_GRIMOIRE] = 1}};
/* 品質重視の戦略: 耐久系と魔法系の装備に Steel / Silver。 */
static const Strategy STRAT_QUALITY = {
    .plan = {[EQ_SWORD] = {{MAT_STEEL, MAT_IRON}, {MAT_IRON, MAT_STEEL}},
             [EQ_SHIELD] = {{MAT_IRON, MAT_STEEL}, {MAT_STEEL, MAT_IRON}},
             [EQ_STAFF] = {{MAT_MANA, MAT_SILVER}, {MAT_MANA, MAT_MANA}},
             [EQ_DAGGER] = {{MAT_IRON, MAT_STEEL}, {MAT_IRON, MAT_OBSIDIAN}},
             [EQ_SPEAR] = {{MAT_STEEL, MAT_STEEL}, {MAT_IRON, MAT_STEEL}},
             [EQ_GRIMOIRE] = {{MAT_MANA, MAT_SILVER}, {MAT_SILVER, MAT_MANA}}},
    .target = {[EQ_SWORD] = 2, [EQ_SHIELD] = 1, [EQ_STAFF] = 1, [EQ_DAGGER] = 1, [EQ_SPEAR] = 1, [EQ_GRIMOIRE] = 1}};
/* 低予算の戦略: 安価な Copper / Wood。 */
static const Strategy STRAT_CHEAP = {
    .plan = {[EQ_SWORD] = {{MAT_COPPER, MAT_IRON}, {MAT_IRON, MAT_COPPER}},
             [EQ_SHIELD] = {{MAT_IRON, MAT_COPPER}, {MAT_COPPER, MAT_IRON}},
             [EQ_STAFF] = {{MAT_MANA, MAT_COPPER}, {MAT_MANA, MAT_WOOD}},
             [EQ_DAGGER] = {{MAT_COPPER, MAT_WOOD}, {MAT_COPPER, MAT_IRON}},
             [EQ_SPEAR] = {{MAT_IRON, MAT_COPPER}, {MAT_COPPER, MAT_IRON}},
             [EQ_GRIMOIRE] = {{MAT_MANA, MAT_COPPER}, {MAT_MANA, MAT_WOOD}}},
    .target = {[EQ_SWORD] = 2, [EQ_SHIELD] = 1, [EQ_STAFF] = 1, [EQ_DAGGER] = 1, [EQ_SPEAR] = 1, [EQ_GRIMOIRE] = 1}};
/* ガラスの大砲の戦略: 攻撃系の装備に Obsidian。 */
static const Strategy STRAT_OBSIDIAN = {
    .plan = {[EQ_SWORD] = {{MAT_IRON, MAT_OBSIDIAN}, {MAT_OBSIDIAN, MAT_IRON}},
             [EQ_SHIELD] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_OBSIDIAN}},
             [EQ_STAFF] = {{MAT_MANA, MAT_OBSIDIAN}, {MAT_MANA, MAT_MANA}},
             [EQ_DAGGER] = {{MAT_IRON, MAT_OBSIDIAN}, {MAT_OBSIDIAN, MAT_WOOD}},
             [EQ_SPEAR] = {{MAT_IRON, MAT_OBSIDIAN}, {MAT_STEEL, MAT_OBSIDIAN}},
             [EQ_GRIMOIRE] = {{MAT_MANA, MAT_OBSIDIAN}, {MAT_MANA, MAT_IRON}}},
    .target = {[EQ_SWORD] = 2, [EQ_SHIELD] = 1, [EQ_STAFF] = 1, [EQ_DAGGER] = 1, [EQ_SPEAR] = 1, [EQ_GRIMOIRE] = 1}};

typedef struct {
    int done, expired, gold, ranked, new_sold, rank_sum;
} GuildStats;

/* 現在の鍛冶場レベルで、客タイプ `ct` の `min_fit` に届くアイテムが作れる最安のレシピ (基本価格で比較) */
static int bot_guild_recipe(const GameState *g, int ct, int min_fit, int *main, int *sub, int *cost) {
    int found = 0, best = 1 << 30;
    for (int m = 0; m < MAT_COUNT; m++) {
        for (int s = 0; s < MAT_COUNT; s++) {
            if (MATERIALS[m].hunt_only || MATERIALS[s].hunt_only) {
                continue; /* 市場で買えない素材 (Dragon Scale) は仕入れられないので、戦略の候補にしない */
            }
            Equipment e;
            memset(&e, 0, sizeof e);
            e.type = CUSTOMER_DEFS[ct].wants_eq;
            e.main = m;
            e.sub = s;
            equipment_compute_stats(&e, g->up_level[UP_FORGE]);
            if (equip_fit(ct, &e) < min_fit) {
                continue;
            }
            int c = 2 * MATERIALS[m].base_price + MATERIALS[s].base_price;
            if (c < best) {
                best = c;
                *main = m;
                *sub = s;
                *cost = c;
                found = 1;
            }
        }
    }
    return found;
}
/* 採算の取れる最高報酬の契約を取り、足りないものをクラフトし、すぐに納品する。 */
static void bot_guild_turn(GameState *g) {
    while (g->guild_active_count < GUILD_ACTIVE_MAX && g->guild_board_count > 0 && g->inv_count < MAX_INVENTORY - 1) {
        int pick = -1, pick_gold = 0, m, s, c;
        for (int i = 0; i < g->guild_board_count; i++) {
            const GuildContract *k = &g->guild_board[i];
            if (k->gold <= pick_gold) {
                continue;
            }
            if (!bot_guild_recipe(g, k->customer_type, k->min_fit, &m, &s, &c)) {
                continue;
            }
            if (c * 10 > k->gold * 8 || g->money < c + 40) {
                continue; /* 利益幅と手元の現金が少し欲しい */
            }
            pick = i;
            pick_gold = k->gold;
        }
        if (pick < 0 || guild_accept(g, pick) != GUILD_OK) {
            break;
        }
    }
    for (int a = g->guild_active_count - 1; a >= 0; a--) {
        const GuildContract k = g->guild_active[a];
        int best = -1, best_fit = 1000; /* 最低限十分な FIT: 良いものは販売用に取っておく */
        for (int i = 0; i < g->inv_count; i++) {
            int f = guild_item_fit(g, a, i);
            if (f >= k.min_fit && f < best_fit) {
                best = i;
                best_fit = f;
            }
        }
        if (best < 0) {
            int m, s, c;
            if (!bot_guild_recipe(g, k.customer_type, k.min_fit, &m, &s, &c) || g->inv_count >= MAX_INVENTORY) {
                continue;
            }
            int need[MAT_COUNT] = {0};
            need[m] += 2;
            need[s] += 1;
            int ok = 1;
            for (int x = 0; x < MAT_COUNT; x++) {
                if (need[x] > g->mat_stock[x] && market_buy(g, x, need[x] - g->mat_stock[x]) != R_OK) {
                    ok = 0;
                }
            }
            if (!ok || craft_equipment(g, CUSTOMER_DEFS[k.customer_type].wants_eq, m, s, &best) != R_OK) {
                continue;
            }
        }
        GuildOutcome o;
        guild_deliver(g, a, best, &o);
    }
}

/* 9 種の装備すべてに対応する (設計図の装備は解放後のみ。ロック中はスキップする)。 */
static const Strategy STRAT_FULL = {.plan = {[EQ_SWORD] = {{MAT_IRON, MAT_IRON}, {MAT_WOOD, MAT_IRON}},
                                             [EQ_SHIELD] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}},
                                             [EQ_STAFF] = {{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}},
                                             [EQ_DAGGER] = {{MAT_IRON, MAT_WOOD}, {MAT_IRON, MAT_IRON}},
                                             [EQ_SPEAR] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}},
                                             [EQ_GRIMOIRE] = {{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}},
                                             [EQ_WARHAMMER] = {{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_COPPER}},
                                             [EQ_RUNE_BOW] = {{MAT_OBSIDIAN, MAT_COPPER}, {MAT_IRON, MAT_WOOD}},
                                             [EQ_CRYSTAL_ORB] = {{MAT_SILVER, MAT_SILVER}, {MAT_SILVER, MAT_WOOD}}},
                                    .target = {[EQ_SWORD] = 2,
                                               [EQ_SHIELD] = 1,
                                               [EQ_STAFF] = 1,
                                               [EQ_DAGGER] = 1,
                                               [EQ_SPEAR] = 1,
                                               [EQ_GRIMOIRE] = 1,
                                               [EQ_WARHAMMER] = 1,
                                               [EQ_RUNE_BOW] = 1,
                                               [EQ_CRYSTAL_ORB] = 1}};

static BotResult play_bot(uint32_t seed, const Strategy *st, int use_guild, GuildStats *gstats) {
    GameState g;
    game_init(&g, seed);
    int rot = 0;
    long rev_sum = 0, gold_sum = 0;
    while (g.status == ST_PLAYING) {
        if (use_guild) {
            bot_guild_turn(&g);
        }
        if (st) {
            if (g.up_level[UP_STORE] < 2 && g.money > 700) {
                upgrade_buy(&g, UP_STORE);
            }
            if (g.up_level[UP_FORGE] < 2 && g.money > 500) {
                upgrade_buy(&g, UP_FORGE);
            }
            /* バランスの取れた在庫を保つ: 剣は 2 つの客タイプに求められる */
            int have[EQ_COUNT] = {0};
            for (int i = 0; i < g.inv_count; i++) {
                have[g.inv[i].type]++;
            }
            for (int eq = 0; eq < EQ_COUNT; eq++) {
                if (!equip_unlocked(g.guild_done, eq)) {
                    continue; /* 未解放の装備はクラフトできない: そのための購入もしない */
                }
                while (have[eq] < st->target[eq] && have[eq] < customers_per_day(&g)) {
                    const int *p = st->plan[eq][rot++ % 2];
                    int need[MAT_COUNT] = {0};
                    need[p[0]] += 2;
                    need[p[1]] += 1;
                    int ok = 1;
                    for (int m = 0; m < MAT_COUNT; m++) {
                        if (need[m] > g.mat_stock[m] && market_buy(&g, m, need[m] - g.mat_stock[m]) != R_OK) {
                            ok = 0;
                        }
                    }
                    if (!ok || craft_equipment(&g, eq, p[0], p[1], NULL) != R_OK) {
                        break;
                    }
                    have[eq]++;
                }
            }
        }
        day_open(&g);
        while (g.phase == PH_OPEN) {
            Customer c = g.queue[g.queue_pos];
            int skip = 0, sold = 0;
            for (int a = 0; a < 3 && g.phase == PH_OPEN && !sold; a++) {
                int i = bot_pick(&g, &c, skip);
                if (i < 0) {
                    break;
                }
                int sold_type = g.inv[i].type;
                SaleOutcome o;
                customer_offer(&g, i, &o);
                if (o.sold && sold_type >= EQ_WARHAMMER && gstats) {
                    gstats->new_sold++;
                }
                if (o.sold) {
                    sold = 1;
                } else {
                    skip |= 1 << i;
                    if (o.customer_left) {
                        break;
                    }
                }
            }
            if (!sold && g.phase == PH_OPEN && g.queue[g.queue_pos].patience == c.patience) {
                customer_decline(&g);
            } else if (!sold && g.phase == PH_OPEN) {
                customer_decline(&g);
            }
        }
        rev_sum += g.report.revenue;
        gold_sum += g.report.guild_gold; /* その日の合計。レポートがリセットされる前に読む */
        if (g.status == ST_PLAYING) {
            day_advance(&g);
        }
        CHECK(game_validate(&g)); /* キャンペーンのどの時点でも、不整合な状態にならない */
    }
    if (gstats) {
        gstats->done += g.guild_done;
        gstats->expired += g.guild_expired;
        gstats->rank_sum += guild_rank(g.guild_done);
        if (guild_rank(g.guild_done) > 0) {
            gstats->ranked++;
        }
    }
    BotResult r = {g.day, g.status, g.money, g.rep, g.total_sold, rev_sum, gold_sum};
    return r;
}
static int bot_campaigns(const char *label, const Strategy *st, int N, double *avg_day, int use_guild,
                         GuildStats *gs_out) {
    int clears = 0, sum_day = 0, lowrep = 0, lowmoney = 0;
    long sum_rev = 0, sum_gold = 0;
    GuildStats gs = {0};
    for (uint32_t s = 1; s <= (uint32_t)N; s++) {
        BotResult r = play_bot(s * 7919u, st, use_guild, &gs);
        sum_rev += r.revenue;
        sum_gold += r.guild_gold;
        if (r.status == ST_CLEARED) {
            clears++;
            sum_day += r.day;
        } else {
            if (r.rep < BAL.goal_rep) {
                lowrep++;
            }
            if (r.money < BAL.goal_money) {
                lowmoney++;
            }
        }
    }
    double ad = clears ? (double)sum_day / clears : 0.0;
    printf("  %-24s %2d/%d cleared, avg clear day %4.1f   (short on rep %d, on money %d)  sales %ld, guild gold %ld "
           "per campaign",
           label, clears, N, ad, lowrep, lowmoney, sum_rev / N, sum_gold / N);
    if (use_guild) {
        printf("  contracts: %.1f done, %.1f expired, rank %.1f, blueprint gear sold %.1f per campaign",
               (double)gs.done / N, (double)gs.expired / N, (double)gs.rank_sum / N, (double)gs.new_sold / N);
    }
    puts("");
    if (avg_day) {
        *avg_day = ad;
    }
    if (gs_out) {
        *gs_out = gs;
    }
    return clears;
}
static void test_bots(void) {
    puts("bots");
    const int N = 40;
    /* 参考情報: 元の戦略は追加コンテンツの客を無視するので、クリア率が下がる。 */
    bot_campaigns("legacy (launch gear only)", &STRAT_LEGACY, N, NULL, 0, NULL);
    /* すべての客タイプに対応する戦略はどれも成立しなければならない: たいてい勝つが、必勝ではない。 */
    const struct {
        const char *name;
        const Strategy *st;
    } strat[] = {{"broad (launch materials)", &STRAT_BROAD},
                 {"quality (steel/silver)", &STRAT_QUALITY},
                 {"cheap (copper)", &STRAT_CHEAP},
                 {"glass cannon (obsidian)", &STRAT_OBSIDIAN}};
    for (size_t i = 0; i < sizeof strat / sizeof *strat; i++) {
        int clears = bot_campaigns(strat[i].name, strat[i].st, N, NULL, 0, NULL);
        CHECK(clears >= N * 6 / 10);       /* 熟練したプレイならたいてい勝つはず ... */
        CHECK(clears <= N * 98 / 100 + 1); /* ... だが自明に保証されてはならない */
    }
    /* 同じ戦略で、ギルド契約も受ける場合 (受注 -> クラフト -> すぐに納品)。これらは機能しなければならない
     * (契約が果たされ、キャンペーンは勝利可能なままで、game_validate() が毎日成り立つ。play_bot を参照)。
     * 注: 仕様どおりの報酬では、ギルドは強力な追加収入になる: これらのボットは、ギルドなしより約 8〜10 日早く、
     * ほぼ 100% のキャンペーンをクリアする。これはアサーションで隠さず報告している。docs/DEVELOPMENT.md
     * の「バランス検証」を参照。 */
    for (size_t i = 0; i < sizeof strat / sizeof *strat; i++) {
        char label[48];
        snprintf(label, sizeof label, "%s + guild", strat[i].name);
        GuildStats gs;
        int clears = bot_campaigns(label, strat[i].st, N, NULL, 1, &gs);
        CHECK(clears >= N * 6 / 10);
        CHECK(gs.done > 0);
    }
    GuildStats gl;
    bot_campaigns("legacy gear + guild", &STRAT_LEGACY, N, NULL, 1, &gl); /* 参考情報 */
    /* 9 種の装備すべて: ギルドのボットはランクを上げ、設計図の装備を解放し、新しい客に売り続ける。 */
    GuildStats gf;
    int full = bot_campaigns("all gear + guild", &STRAT_FULL, N, NULL, 1, &gf);
    CHECK(full >= N * 6 / 10);
    CHECK(gf.ranked >= N * 9 / 10); /* ほぼすべてのキャンペーンが少なくとも Journeyman に達する */
    CHECK(gf.rank_sum > N * 2);     /* ... そして平均ではそれをはるかに超える */
    CHECK(gf.new_sold > N);         /* 設計図の装備は実際にクラフトされ、その客に売られる */
    GuildStats gn;
    int nog = bot_campaigns("all gear, no guild", &STRAT_FULL, N, NULL, 0,
                            &gn); /* ランクは 1 のまま: 未解放の装備は決して作られない */
    CHECK_EQ(gn.new_sold, 0);
    (void)nog;
    int idle_fail = 0;
    for (uint32_t s = 1; s <= 10; s++) {
        BotResult r = play_bot(s, NULL, 0, NULL);
        if (r.status == ST_FAILED) {
            idle_fail++;
        }
    }
    CHECK_EQ(idle_fail, 10); /* 何もしなければ、常に負ける */
    BotResult r = play_bot(1, NULL, 0, NULL);
    printf("  idle bot: failed on day %d with money %d\n", r.day, r.money);
}

/* ---- 回帰: contract_valid() / rng_range() での符号付きオーバーフロー、game_load() での mat/up レコードの重複 ---- */
static void test_overflow_contract_deadline(void) {
    puts("overflow_contract_deadline");
    GameState g = fresh();
    g.day = 5;
    g.phase = PH_PREP;
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK(game_validate(&g)); /* 手を付けていない受注中の契約は問題ない */
    const int bad[] = {INT_MIN, INT_MIN + 1, INT_MIN + 98, INT_MIN + 99, -1, 0, INT_MAX - 1, INT_MAX};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        GameState x = g;
        x.guild_active[0].deadline_day = bad[i]; /* 壊れた期限: 未定義動作 (UBSan) ではなく、拒否されなければならない */
        CHECK(!game_validate(&x));
    }
    for (int dd = 1; dd <= 2; dd++) { /* 壊れた `days` と極端な期限の組み合わせ */
        GameState x = g;
        x.guild_active[0].days = dd == 1 ? INT_MAX : INT_MIN;
        x.guild_active[0].deadline_day = INT_MIN;
        CHECK(!game_validate(&x));
    }
    GameState y = g;
    y.guild_board[0].deadline_day = INT_MIN; /* 掲示板の提示は、やはり期限が 0 でなければならない */
    CHECK(!game_validate(&y));
    /* 同じ破損がセーブファイル経由で来ても、拒否され、対象には触れない */
    GameState keep = fresh(), probe = keep;
    GameState x = g;
    x.guild_active[0].deadline_day = INT_MIN;
    char *txt = save_text(&x);
    CHECK_EQ(load_text(&probe, txt), SAVE_ERR_VALUE);
    CHECK(same_state(&probe, &keep));
    free(txt);
    /* 有効な期間は変わらない: 期限 = 受注日 + days - 1 (5 日目に受注)。その日のうちはまだ有効 */
    GameState ok = g;
    CHECK_EQ(ok.guild_active[0].deadline_day, 5 + ok.guild_active[0].days - 1);
    CHECK(game_validate(&ok));
    ok.day = ok.guild_active[0].deadline_day;
    CHECK(game_validate(&ok)); /* 納品できる最終日 */
    ok.guild_active[0].deadline_day = ok.day - 1;
    CHECK(!game_validate(&ok)); /* 期限はすでに過ぎている */
}

static void test_overflow_rng_range(void) {
    puts("overflow_rng_range");
    /* 極端な範囲: ゼロ除算なし、オーバーフローなし、結果は常に [lo, hi] の内側 (ASan/UBSan で実行) */
    uint32_t s = 12345u;
    for (int n = 0; n < 2000; n++) {
        int v;
        v = rng_range(&s, INT_MIN, INT_MAX);
        CHECK(v >= INT_MIN && v <= INT_MAX);
        v = rng_range(&s, INT_MIN, INT_MAX - 1);
        CHECK(v >= INT_MIN && v <= INT_MAX - 1);
        v = rng_range(&s, INT_MIN + 1, INT_MAX);
        CHECK(v >= INT_MIN + 1);
        v = rng_range(&s, INT_MIN, 0);
        CHECK(v <= 0);
        v = rng_range(&s, 0, INT_MAX);
        CHECK(v >= 0);
        v = rng_range(&s, -1, INT_MAX);
        CHECK(v >= -1);
        v = rng_range(&s, INT_MAX - 5, INT_MAX);
        CHECK(v >= INT_MAX - 5);
        v = rng_range(&s, INT_MIN, INT_MIN + 5);
        CHECK(v <= INT_MIN + 5);
    }
    /* 全範囲は 32 ビットの抽選を 1:1 に対応づけ (幅 2^32)、ちょうど 1 回の抽選を消費する */
    {
        uint32_t a = 777u, b = 777u;
        uint32_t raw = rng_next(&b);
        int v = rng_range(&a, INT_MIN, INT_MAX);
        CHECK_EQ((long long)v, (long long)INT_MIN + (long long)raw);
        CHECK_EQ(a, b);
    }
    /* 退化した範囲は従来の動作を保つ: lo を返し、何も抽選しない */
    {
        uint32_t a = 99u;
        CHECK_EQ(rng_range(&a, 5, 5), 5);
        CHECK_EQ(rng_range(&a, 7, 3), 7);
        CHECK_EQ(rng_range(&a, INT_MAX, INT_MIN), INT_MAX);
        CHECK_EQ(a, 99u);
    }
    /* 通常の範囲: 元の式 lo + next % (hi - lo + 1) とビット単位で同一 (再現性) */
    const int ranges[][2] = {{0, 99},    {-50, 50},           {1, 6},
                             {0, 1},     {-1000000, 1000000}, {-3, -1},
                             {10, 4000}, {0, INT_MAX - 1},    {-100000, INT_MAX / 2}};
    for (size_t r = 0; r < sizeof ranges / sizeof *ranges; r++) {
        uint32_t a = 424242u + (uint32_t)r, b = a;
        for (int n = 0; n < 500; n++) {
            int lo = ranges[r][0], hi = ranges[r][1];
            int want = lo + (int)(rng_next(&b) % (uint32_t)(hi - lo + 1));
            CHECK_EQ(rng_range(&a, lo, hi), want);
        }
        CHECK_EQ(a, b); /* その後の ストリーム位置も同一 */
    }
    /* 固定シードは固定の列を与える (xorshift のストリームと剰余の対応を固定する) */
    {
        uint32_t a = 1u, b = 1u;
        for (int n = 0; n < 50; n++) {
            int v = rng_range(&a, 0, 99);
            CHECK_EQ(v, (int)(rng_next(&b) % 100u));
        }
    }
}

/* `line` を閉じの "end" 行の直前に挿入する */
static char *add_line_before_end(const char *text, const char *line) {
    const char *e = strstr(text, "\nend\n");
    CHECK(e != NULL);
    size_t head = (size_t)(e - text) + 1, ll = strlen(line);
    char *out = malloc(strlen(text) + ll + 2);
    memcpy(out, text, head);
    memcpy(out + head, line, ll);
    out[head + ll] = '\n';
    strcpy(out + head + ll + 1, text + head);
    return out;
}

static void test_save_duplicate_records(void) {
    puts("save_duplicate_records");
    GameState src = fresh();
    src.status = ST_SANDBOX;
    src.mat_stock[MAT_IRON] = 4;
    src.up_level[UP_STORE] = 1;
    GameState keep = fresh();
    keep.money = 4321;
    GameState probe;
    char *good = save_text(&src);
    {
        GameState l = keep;
        CHECK_EQ(load_text(&l, good), SAVE_OK);
        CHECK(same_state(&l, &src));
    } /* 元のものは問題なく読み込める */
    char *bad;
#define DUP_REFUSED(txt)                                                                                               \
    do {                                                                                                               \
        probe = keep;                                                                                                  \
        CHECK_EQ(load_text(&probe, (txt)), SAVE_ERR_FORMAT);                                                           \
        CHECK(same_state(&probe, &keep));                                                                              \
    } while (0)
    for (int m = 0; m < MAT_COUNT; m++) {
        char same[96], diff[96];
        snprintf(same, sizeof same, "mat %d %d %d %d %d %d", m, src.mat_stock[m], src.mat_cost[m], src.price[m],
                 src.prev_price[m], src.swing_pm[m]);
        snprintf(diff, sizeof diff, "mat %d 9 99 %d %d 0", m, src.price[m], src.prev_price[m]);
        bad = add_line_before_end(good, same);
        DUP_REFUSED(bad);
        free(bad); /* 同一のコピー */
        bad = add_line_before_end(good, diff);
        DUP_REFUSED(bad);
        free(bad); /* 後のレコードが勝ってしまう */
        bad = add_line_after_header(good, diff);
        DUP_REFUSED(bad);
        free(bad); /* 先のレコードが上書きされてしまう */
    }
    for (int u = 0; u < UP_COUNT; u++) {
        char same[48], diff[48];
        snprintf(same, sizeof same, "up %d %d", u, src.up_level[u]);
        snprintf(diff, sizeof diff, "up %d %d", u, UPGRADES[u].max_level);
        bad = add_line_before_end(good, same);
        DUP_REFUSED(bad);
        free(bad);
        bad = add_line_before_end(good, diff);
        DUP_REFUSED(bad);
        free(bad);
        bad = add_line_after_header(good, diff);
        DUP_REFUSED(bad);
        free(bad);
    }
    /* 重複は古い形式 (v1 の旧ファイル、v2 のファイル) でも拒否され、黙って「最後が勝つ」ことはない */
    bad = add_line_before_end(LEGACY_SAVE, "mat 1 9 99 6 6 21");
    DUP_REFUSED(bad);
    free(bad);
    bad = add_line_before_end(LEGACY_SAVE, "up 2 0");
    DUP_REFUSED(bad);
    free(bad);
    bad = add_line_before_end(V2_SAVE, "mat 0 1 1 9 9 0");
    DUP_REFUSED(bad);
    free(bad);
    bad = add_line_before_end(V2_SAVE, "up 0 0");
    DUP_REFUSED(bad);
    free(bad);
    /* 変更していない旧形式 / v2 のファイルは、今も読み込める (移行は手つかず) */
    {
        GameState l = keep;
        CHECK_EQ(load_text(&l, LEGACY_SAVE), SAVE_OK);
        CHECK(game_validate(&l));
    }
    {
        GameState l = keep;
        CHECK_EQ(load_text(&l, V2_SAVE), SAVE_OK);
        CHECK(game_validate(&l));
    }
    /* このビルドを超える ID は従来どおり判定される (SAVE_ERR_VALUE)。1 つだけ余分なレコードは「重複」ではない */
    {
        char line[48];
        snprintf(line, sizeof line, "mat %d 1 1 1 1 0", MAT_COUNT);
        bad = add_line_before_end(good, line);
        probe = keep;
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
    }
#undef DUP_REFUSED
    free(good);
}

/* ---- Mythic Smith (納品 50 件) / Wyrmcleaver / Wyrm Hunter ---- */
static void test_mythic_smith(void) {
    puts("mythic_smith");
    CHECK(content_validate() == NULL);
    CHECK(!strcmp(guild_rank_name(RANK_MYTHIC), "Mythic Smith"));
    CHECK_EQ(guild_rank_need(RANK_MYTHIC), 50);
    CHECK(!strcmp(guild_rank_name(RANK_COUNT), "?")); /* 最上位の次は不正なランク */
    CHECK(!strcmp(BLUEPRINTS[BP_WYRMCLEAVER].name, "Wyrmcleaver Plans"));
    CHECK(!strcmp(EQUIPS[EQ_WYRMCLEAVER].name, "Wyrmcleaver"));
    CHECK(!strcmp(CUSTOMER_DEFS[CT_WYRM_HUNTER].name, "Wyrm Hunter"));
    CHECK(icon_exists("wyrmcleaver"));
    /* 品質確率は仕様どおりで、合計は 100。既存のランクは変わらない (品質のテストが全行を固定している) */
    CHECK_EQ(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_COMMON], 20);
    CHECK_EQ(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_FINE], 33);
    CHECK_EQ(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_MASTERWORK], 30);
    CHECK_EQ(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_LEGENDARY], 17);
    CHECK_EQ(QUALITY_RATE_PCT[RANK_MYTHIC][0] + QUALITY_RATE_PCT[RANK_MYTHIC][1] + QUALITY_RATE_PCT[RANK_MYTHIC][2] +
                 QUALITY_RATE_PCT[RANK_MYTHIC][3],
             100);
    /* Grandmaster より Legendary / Masterwork が多く、Common は少ない (ランクが上がるほど良くなる) */
    CHECK(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_COMMON] < QUALITY_RATE_PCT[RANK_GRANDMASTER][QUALITY_COMMON]);
    CHECK(QUALITY_RATE_PCT[RANK_MYTHIC][QUALITY_LEGENDARY] > QUALITY_RATE_PCT[RANK_GRANDMASTER][QUALITY_LEGENDARY]);

    /* 1. 29 / 30 / 49 / 50 / 51 件のランク、次のランクまでの件数、解放状態 */
    static const struct {
        int done, rank, remaining, next_bp, wyrm;
    } t[] = {{29, RANK_MASTER, 1, BP_CRYSTAL_ORB, 0},
             {30, RANK_GRANDMASTER, 20, BP_WYRMCLEAVER, 0},
             {49, RANK_GRANDMASTER, 1, BP_WYRMCLEAVER, 0},
             {50, RANK_MYTHIC, -1, -1, 1},
             {51, RANK_MYTHIC, -1, -1, 1},
             {5000, RANK_MYTHIC, -1, -1, 1}};
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        CHECK_EQ(guild_rank(t[i].done), t[i].rank);
        CHECK_EQ(guild_next_rank_remaining(t[i].done), t[i].remaining); /* 最高ランクでは -1 (残り件数を出さない) */
        CHECK_EQ(guild_next_blueprint(t[i].done), t[i].next_bp);
        CHECK_EQ(blueprint_unlocked(t[i].done, BP_WYRMCLEAVER), t[i].wyrm);
        CHECK_EQ(equip_unlocked(t[i].done, EQ_WYRMCLEAVER), t[i].wyrm);
        CHECK_EQ(customer_unlocked(t[i].done, CT_WYRM_HUNTER), t[i].wyrm);
        CHECK_EQ(equip_unlocked(t[i].done, EQ_CRYSTAL_ORB), t[i].done >= 30); /* 前のランクの設計図は影響を受けない */
    }

    /* 2. クラフト: 49 件ではロック (何も変えない)、50 件で作れる */
    GameState lk = at_done(49);
    lk.mat_stock[MAT_STEEL] = 3;
    lk.mat_cost[MAT_STEEL] = 36;
    GameState lk_before = lk;
    CHECK_EQ(can_craft(&lk, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL), R_ERR_LOCKED);
    CHECK_EQ(craft_equipment(&lk, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL, NULL), R_ERR_LOCKED);
    CHECK(same_state(&lk, &lk_before));
    lk.guild_done = 50;
    CHECK_EQ(can_craft(&lk, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL), R_OK);

    /* 3. 全素材の組み合わせ x 全品質 x 全 Forge: 既存の 1 つの処理だけで基本 -> 品質 -> 特性がちょうど 1 回適用される
     */
    int combos = 0;
    for (int main = 0; main < MAT_COUNT; main++) {
        for (int sub = 0; sub < MAT_COUNT; sub++) {
            for (int forge = 0; forge <= UPGRADES[UP_FORGE].max_level; forge++) {
                for (int q = 0; q < QUALITY_COUNT; q++) {
                    GameState c = at_done(50);
                    c.up_level[UP_FORGE] = forge;
                    c.mat_stock[main] += 3;
                    c.mat_stock[sub] += 3;
                    c.mat_cost[main] += 3 * MATERIALS[main].base_price;
                    c.mat_cost[sub] += 3 * MATERIALS[sub].base_price;
                    pin_craft(&c, q);
                    GameState before = c;
                    uint32_t tr = c.craft_rng;
                    int idx = -1;
                    CHECK_EQ(craft_equipment(&c, EQ_WYRMCLEAVER, main, sub, &idx), R_OK);
                    const Equipment *e = &c.inv[idx];
                    CHECK_EQ(e->type, EQ_WYRMCLEAVER);
                    CHECK_EQ(e->quality, q);
                    CHECK_EQ(e->trait != EQ_TRAIT_NONE, QUALITY_DEFS[q].has_trait);
                    /* 乱数の消費: 品質に 1 回、特性があればもう 1 回。他のストリームには触れない */
                    int roll_q = quality_from_roll(RANK_MYTHIC, forge, rng_range(&tr, 0, 99));
                    CHECK_EQ(roll_q, q);
                    if (QUALITY_DEFS[q].has_trait) {
                        int uses = (main == MAT_DRAGON_SCALE ? 2 : 0) + (sub == MAT_DRAGON_SCALE ? 1 : 0);
                        int want_t;
                        if (uses > 0 && rng_range(&tr, 0, 99) < DRAGON_TRAIT_PCT[uses]) {
                            /* Dragon Scale を使う場合だけ、先に特殊特性の判定 (0..99) を 1 回引き、当たりなら 50% ずつ
                             */
                            want_t = rng_range(&tr, 0, 1) == 0 ? EQ_TRAIT_DRACONIC_EDGE : EQ_TRAIT_WYRMSCALE;
                        } else {
                            want_t = trait_from_roll(rng_range(&tr, 0, EQ_TRAIT_STANDARD_COUNT - 2));
                        }
                        CHECK_EQ(e->trait, want_t);
                    }
                    CHECK_EQ(c.craft_rng, tr);
                    CHECK_EQ(c.rng, before.rng);
                    CHECK_EQ(c.guild_rng, before.guild_rng);
                    /* 独立に計算した期待値: 基本 + 素材の混合 (主 2 : 副 1) -> Forge -> 品質倍率 -> 特性 */
                    const MaterialDef *a = &MATERIALS[main], *b = &MATERIALS[sub];
                    double fm = upgrade_effect(UP_FORGE, forge) / 1000.0;
                    int ex[5];
                    ex[0] = roundx((14 + (2.0 * a->atk + b->atk) / 3.0) * fm);
                    ex[1] = roundx((2 + (2.0 * a->def + b->def) / 3.0) * fm);
                    ex[2] = roundx((1 + (2.0 * a->mag + b->mag) / 3.0) * fm);
                    ex[3] = roundx((9 + (2.0 * a->dur + b->dur) / 3.0) * fm);
                    ex[4] = roundx(5 + (2.0 * a->wt + b->wt) / 3.0);
                    if (ex[4] < 1) {
                        ex[4] = 1;
                    }
                    for (int k = 0; k < 4; k++) {
                        ex[k] = (ex[k] * QUALITY_DEFS[q].mult_pm + 500) / 1000;
                    }
                    if (e->trait != EQ_TRAIT_NONE) {
                        int amt = TRAIT_DEFS[e->trait].amount[q];
                        if (TRAIT_DEFS[e->trait].stat == TS_WT_DOWN) {
                            ex[4] = ex[4] - amt < 1 ? 1 : ex[4] - amt;
                        } else {
                            ex[TRAIT_DEFS[e->trait].stat] += amt;
                        }
                    }
                    CHECK_EQ(e->atk, ex[0]);
                    CHECK_EQ(e->def, ex[1]);
                    CHECK_EQ(e->mag, ex[2]);
                    CHECK_EQ(e->dur, ex[3]);
                    CHECK_EQ(e->wt, ex[4]);
                    /* 価格はこの最終能力から既存の式で求まり、二重適用は拒否される */
                    Equipment re = *e;
                    equipment_price(&re);
                    CHECK_EQ(re.rec_price, e->rec_price);
                    Equipment again = *e;
                    int applied = equipment_apply_quality(&again, q, e->trait);
                    if (q != QUALITY_COMMON) {
                        CHECK_EQ(applied, 0); /* すでに品質を持つアイテムは拒否される */
                    }
                    CHECK(!memcmp(&again, e,
                                  sizeof again)); /* Common への Common は倍率 x1.00 の恒等変換: どちらでも不変 */
                    CHECK(game_validate(&c));
                    combos++;
                }
            }
        }
    }
    CHECK_EQ(combos, MAT_COUNT * MAT_COUNT * (UPGRADES[UP_FORGE].max_level + 1) * QUALITY_COUNT);
    /* 仕様の基本能力: 素材の補正がなければ ATK 14 / DEF 2 / MAG 1 / DUR 9 / WT 5 (Steel x3 の組み合わせは下で固定) */
    {
        Equipment e;
        memset(&e, 0, sizeof e);
        e.type = EQ_WYRMCLEAVER;
        e.main = MAT_STEEL;
        e.sub = MAT_STEEL;
        equipment_compute_stats(&e, 0);
        CHECK_EQ(e.atk, 14 + 10);
        CHECK_EQ(e.def, 2 + 8);
        CHECK_EQ(e.mag, 1);
        CHECK_EQ(e.dur, 9 + 11);
        CHECK_EQ(e.wt, 5 + 6);
        CHECK_EQ(equip_fit(CT_WYRM_HUNTER, &e), 81); /* 重み ATK 45 / DUR 35 / 軽さ 20 */
        e.main = e.sub = MAT_WOOD;
        equipment_compute_stats(&e, 0);
        CHECK_EQ(equip_fit(CT_WYRM_HUNTER, &e), 60);
    }

    /* 4. 品質の分布 (Mythic Smith、Forge 0): 固定シードの 4000 回のクラフトが 20/33/30/17 に近い */
    {
        GameState d = at_done(50);
        int cnt[QUALITY_COUNT] = {0};
        for (int n = 0; n < 4000; n++) {
            d.mat_stock[MAT_IRON] = 3;
            d.mat_cost[MAT_IRON] = 30;
            d.inv_count = 0;
            int idx = -1;
            CHECK_EQ(craft_equipment(&d, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON, &idx), R_OK);
            cnt[d.inv[idx].quality]++;
        }
        CHECK(abs(cnt[QUALITY_COMMON] - 800) < 120);
        CHECK(abs(cnt[QUALITY_FINE] - 1320) < 120);
        CHECK(abs(cnt[QUALITY_MASTERWORK] - 1200) < 120);
        CHECK(abs(cnt[QUALITY_LEGENDARY] - 680) < 120);
    }

    /* 5. 客: 50 件未満では決して現れず (抽選の列は 30 件のときと完全に同じ)、50 件で出現する */
    {
        uint32_t ra = 9001, rb = 9001;
        for (int i = 0; i < 3000; i++) {
            Customer a, b;
            customer_generate(&ra, 50, 30, &a);
            customer_generate(&rb, 50, 49, &b);
            CHECK(!memcmp(&a, &b, sizeof a));
            CHECK(a.type != CT_WYRM_HUNTER);
        }
        CHECK_EQ(ra, rb);
        uint32_t rng = 31337;
        int seen = 0, total = 6000;
        for (int i = 0; i < total; i++) {
            Customer c;
            customer_generate(&rng, 50, 50, &c);
            if (c.type == CT_WYRM_HUNTER) {
                seen++;
                const CustomerDef *d = &CUSTOMER_DEFS[CT_WYRM_HUNTER];
                CHECK(c.budget >= d->budget_min && c.budget <= d->budget_max); /* 評判 50 で倍率はちょうど 1.0 */
                CHECK(c.min_fit >= 50 && c.min_fit <= 75);
                CHECK(c.tol_pm >= d->tol_lo && c.tol_pm <= d->tol_hi);
            }
        }
        CHECK(seen > total / CUST_TYPE_COUNT / 2); /* 一様な抽選に参加している */
        CHECK(seen < total / CUST_TYPE_COUNT * 2);
        /* 実際の日の進行でも、49 件では待ち行列に現れず、50 件で現れる */
        int in49 = 0, in50 = 0;
        for (int day = 0; day < 60; day++) {
            GameState a49 = at_done(49), a50 = at_done(50);
            a49.rng = a50.rng = 1000u + (uint32_t)day;
            day_open(&a49);
            day_open(&a50);
            for (int i = 0; i < a49.queue_count; i++) {
                in49 += a49.queue[i].type == CT_WYRM_HUNTER;
            }
            for (int i = 0; i < a50.queue_count; i++) {
                in50 += a50.queue[i].type == CT_WYRM_HUNTER;
            }
        }
        CHECK_EQ(in49, 0);
        CHECK(in50 > 0);
    }

    /* 6. 契約の掲示: 49 件では Wyrm Hunter の依頼は出ず、50 件から出る。未解放の契約は状態の検証で拒否される */
    {
        GameState b49 = at_done(49), b50 = at_done(50);
        int w49 = 0, w50 = 0;
        for (int d = 1; d <= 400; d++) {
            b49.day = b50.day = d;
            guild_refresh_board(&b49);
            guild_refresh_board(&b50);
            for (int i = 0; i < b49.guild_board_count; i++) {
                w49 += b49.guild_board[i].customer_type == CT_WYRM_HUNTER;
            }
            for (int i = 0; i < b50.guild_board_count; i++) {
                w50 += b50.guild_board[i].customer_type == CT_WYRM_HUNTER;
            }
        }
        CHECK_EQ(w49, 0);
        CHECK(w50 > 0);
        GameState z = at_done(49);
        put_contract(&z, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK(!game_validate(&z));
        z = at_done(50);
        put_contract(&z, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK(game_validate(&z));
    }

    /* 7. 49 -> 50 件目の納品でちょうど 1 回だけランクが上がり、Wyrmcleaver Plans だけが新たに解放される
     * (UI のバナーはこの納品前後の比較だけで決まる。何も保存しない) */
    {
        GameState x = at_done(48);
        x.money = 100000;
        for (int n = 49; n <= 51; n++) {
            put_contract(&x, CT_WARRIOR, 0, GD_EASY);
            CHECK_EQ(guild_accept(&x, 0), GUILD_OK);
            int it = give_item(&x, EQ_SWORD, MAT_IRON, MAT_IRON);
            int before = guild_rank(x.guild_done);
            GuildOutcome o;
            CHECK_EQ(guild_deliver(&x, 0, it, &o), GUILD_OK);
            int after = guild_rank(x.guild_done);
            int unlocked_now = 0, which = -1;
            for (int b = 0; b < BP_COUNT; b++) {
                if (BLUEPRINTS[b].rank > before && BLUEPRINTS[b].rank <= after) {
                    unlocked_now++;
                    which = b;
                }
            }
            CHECK_EQ(x.guild_done, n);
            CHECK_EQ(after > before, n == 50);
            CHECK_EQ(unlocked_now, n == 50 ? 1 : 0);
            if (n == 50) {
                CHECK_EQ(which, BP_WYRMCLEAVER);
                CHECK_EQ(after, RANK_MYTHIC);
            }
            CHECK(game_validate(&x));
            x.mat_stock[MAT_IRON] = 0;
            x.mat_cost[MAT_IRON] = 0;
        }
        CHECK_EQ(guild_rank(x.guild_done), RANK_MYTHIC); /* 51 件でも下がらない */
    }
    /* バナーの文字列は UI のバッファ (48 / 96 文字) に収まる */
    CHECK(strlen("GUILD RANK UP! ") + strlen(GUILD_RANKS[RANK_MYTHIC].name) < 48);
    CHECK(strlen("BLUEPRINT UNLOCKED: ") + strlen(EQUIPS[EQ_WYRMCLEAVER].name) < 96);

    /* 8. 契約の納品と販売: Wyrm Hunter の契約は Wyrmcleaver だけを受け付け、FIT を既存の式で検査する */
    {
        GameState y = at_done(50);
        put_contract(&y, CT_WYRM_HUNTER, 65, GD_HARD);
        CHECK_EQ(guild_accept(&y, 0), GUILD_OK);
        int sw = give_item(&y, EQ_SWORD, MAT_IRON, MAT_IRON);
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&y, 0, sw, &o), GUILD_ERR_WRONG_TYPE);
        int weak = give_item(&y, EQ_WYRMCLEAVER, MAT_WOOD, MAT_WOOD); /* FIT 60 < 65 */
        CHECK_EQ(guild_item_fit(&y, 0, weak), 60);
        CHECK_EQ(guild_deliver(&y, 0, weak, &o), GUILD_ERR_LOW_FIT);
        int good = give_item(&y, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL); /* FIT 81 */
        CHECK_EQ(guild_item_fit(&y, 0, good), 81);
        CHECK_EQ(guild_deliver(&y, 0, good, &o), GUILD_OK);
        CHECK_EQ(o.gold, GUILD_DIFFS[GD_HARD].gold);
        CHECK_EQ(y.guild_done, 51);
        CHECK(game_validate(&y));
        /* 販売: 正しい装備なら売れ、別の装備や別の客では SALE_WRONG_TYPE */
        GameState s = at_done(50);
        int w = give_item(&s, EQ_WYRMCLEAVER, MAT_STEEL, MAT_STEEL);
        int sw2 = give_item(&s, EQ_SWORD, MAT_IRON, MAT_IRON);
        Customer hunter = mk_customer(CT_WYRM_HUNTER, 210, 50, 1100);
        CHECK_EQ(judge_sale(&hunter, &s.inv[w], s.inv[w].rec_price).result, SALE_OK);
        CHECK_EQ(judge_sale(&hunter, &s.inv[sw2], s.inv[sw2].rec_price).result, SALE_WRONG_TYPE);
        Customer knight = mk_customer(CT_KNIGHT, 180, 40, 1100);
        CHECK_EQ(judge_sale(&knight, &s.inv[w], s.inv[w].rec_price).result, SALE_WRONG_TYPE);
        force_open(&s, hunter);
        SaleOutcome so;
        CHECK_EQ(customer_offer(&s, w, &so), R_OK);
        CHECK_EQ(so.result, SALE_OK);
        CHECK(game_validate(&s));
    }

    /* 9. セーブ: 保存・読み込みが同一で、ランクと解放は何も保存されず guild_done から導かれる */
    {
        GameState s = at_done(50);
        give_item(&s, EQ_WYRMCLEAVER, MAT_STEEL, MAT_IRON);
        put_contract(&s, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK_EQ(guild_accept(&s, 0), GUILD_OK);
        force_open(&s, mk_customer(CT_WYRM_HUNTER, 150, 55, 1100));
        CHECK(game_validate(&s));
        char *txt = save_text(&s);
        CHECK(!strncmp(txt, "LOOTSMITH_SAVE 4\n", 17)); /* 最新の形式 (ランクと解放はこのバージョンでも保存しない) */
        CHECK(strstr(txt, "rank") == NULL && strstr(txt, "blueprint") == NULL && strstr(txt, "unlock") == NULL);
        GameState r;
        CHECK_EQ(load_text(&r, txt), SAVE_OK);
        CHECK(same_state(&r, &s));
        CHECK_EQ(r.inv[0].type, EQ_WYRMCLEAVER);
        CHECK_EQ(guild_rank(r.guild_done), RANK_MYTHIC);
        free(txt);
        /* 50 件未満のセーブに Wyrmcleaver / Wyrm Hunter があれば拒否され、読み込み先は変わらない */
        GameState x = s;
        x.guild_done = 49;
        char *bad = save_text(&x);
        GameState keep = fresh(), probe = keep;
        CHECK_EQ(load_text(&probe, bad), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(bad);
        /* 50 件を大きく超えるセーブ (納品 120) も、ランクと解放が導かれる */
        x.guild_done = 120;
        x.guild_active_count = 0;
        memset(x.guild_active, 0, sizeof x.guild_active);
        char *hi = save_text(&x);
        CHECK_EQ(load_text(&r, hi), SAVE_OK);
        CHECK_EQ(guild_rank(r.guild_done), RANK_MYTHIC);
        CHECK(equip_unlocked(r.guild_done, EQ_WYRMCLEAVER));
        CHECK(game_validate(&r));
        free(hi);
    }
    /* 10. Mythic Smith 以前のビルドのセーブ (counts 7 9 10 3) は、そのまま読み込める */
    {
        GameState o =
            at_done(60); /* 古いビルドでは Grandmaster が最高だったが、guild_done が 50 以上なら新ランクに導かれる */
        give_item(&o, EQ_CRYSTAL_ORB, MAT_SILVER, MAT_SILVER);
        char *tx = save_text(&o);
        char *old = as_older_build(tx, "counts 7 9 10 3");
        GameState r;
        CHECK_EQ(load_text(&r, old), SAVE_OK);
        CHECK_EQ(guild_rank(r.guild_done), RANK_MYTHIC);
        CHECK(equip_unlocked(r.guild_done, EQ_WYRMCLEAVER));
        CHECK_EQ(r.inv[0].type, EQ_CRYSTAL_ORB);
        CHECK(game_validate(&r));
        free(old);
        GameState o2 = at_done(40);
        char *tx2 = save_text(&o2);
        char *old2 = as_older_build(tx2, "counts 7 9 10 3");
        CHECK_EQ(load_text(&r, old2), SAVE_OK);
        CHECK_EQ(guild_rank(r.guild_done), RANK_GRANDMASTER);
        CHECK(!equip_unlocked(r.guild_done, EQ_WYRMCLEAVER));
        free(old2);
        free(tx);
        free(tx2);
    }
}

/* =====================================================================================================
 * Dragon Hunt / Dragon Scale (第 2 弾コンテンツ)
 * ===================================================================================================== */
static GuildContract make_hunt(GameState *g) { /* guild_refresh_board() が作るのと同じ内容 (テーブルから) */
    const DragonHuntDef *h = &GUILD_DRAGON_HUNT;
    GuildContract c;
    memset(&c, 0, sizeof c);
    c.id = g->guild_next_id++;
    c.customer_type = h->customer;
    c.min_fit = h->min_fit;
    c.difficulty = h->difficulty;
    c.gold = h->gold;
    c.rep = h->rep;
    c.days = h->days;
    c.kind = GUILD_CONTRACT_DRAGON_HUNT;
    c.reward_mat = h->reward_mat;
    c.reward_qty = h->reward_qty;
    return c;
}
static GameState hunt_game(void) { /* Mythic Smith、受注済みの Dragon Hunt 1 件 (活動中の契約はそれだけ) */
    GameState g = at_done(50);
    g.rep = 10;
    memset(g.guild_board, 0, sizeof g.guild_board);
    g.guild_board[0] = make_hunt(&g);
    g.guild_board_count = 1;
    CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
    CHECK_EQ(g.guild_board_count, 0);
    CHECK(game_validate(&g));
    return g;
}
static int hunts_on_board(const GameState *g) {
    int n = 0;
    for (int i = 0; i < g->guild_board_count; i++) {
        n += g->guild_board[i].kind == GUILD_CONTRACT_DRAGON_HUNT;
    }
    return n;
}
static int roll_of(uint32_t s) { /* guild_refresh_board() が最初に引くロール (s は guild_rng の状態) */
    return rng_range(&s, 0, 99);
}

static void test_dragon_tables(void) {
    puts("dragon_tables");
    CHECK(content_validate() == NULL);
    const DragonHuntDef *h = &GUILD_DRAGON_HUNT;
    CHECK(!strcmp(h->name, "Dragon Hunt"));
    CHECK_EQ(h->customer, CT_WYRM_HUNTER);
    CHECK_EQ(CUSTOMER_DEFS[h->customer].wants_eq, EQ_WYRMCLEAVER);
    CHECK_EQ(h->min_fit, 70);
    CHECK_EQ(h->difficulty, GD_HARD);
    CHECK_EQ(h->gold, 170);
    CHECK_EQ(h->rep, 4);
    CHECK_EQ(h->days, 3);
    CHECK_EQ(h->reward_mat, MAT_DRAGON_SCALE);
    CHECK_EQ(h->reward_qty, 1);
    CHECK_EQ(h->min_done, 50);
    CHECK_EQ(h->min_done, GUILD_RANKS[RANK_MYTHIC].need_done);
    CHECK_EQ(h->spawn_pct, 25);
    CHECK_EQ(GUILD_CONTRACT_STANDARD, 0); /* ゼロ初期化された契約は通常契約 */
    CHECK_EQ(GUILD_CONTRACT_DRAGON_HUNT, 1);
    CHECK(!strcmp(guild_kind_name(GUILD_CONTRACT_DRAGON_HUNT), "Dragon Hunt"));
    CHECK(!strcmp(guild_kind_name(GUILD_CONTRACT_STANDARD), "Contract"));
    CHECK(!strcmp(guild_kind_name(7), "?") && !strcmp(guild_kind_name(-1), "?"));
    /* 通常契約の調整値は変わっていない */
    CHECK_EQ(GUILD_DIFFS[GD_EASY].gold, 35);
    CHECK_EQ(GUILD_DIFFS[GD_STANDARD].gold, 55);
    CHECK_EQ(GUILD_DIFFS[GD_HARD].gold, 80);
    CHECK_EQ(GUILD_DIFFS[GD_HARD].min_fit, 65);
    CHECK_EQ(GUILD_SPAWN[2].pct[GD_HARD], 40);

    const MaterialDef *m = &MATERIALS[MAT_DRAGON_SCALE];
    CHECK(!strcmp(m->name, "Dragon Scale"));
    CHECK(!strcmp(m->desc, "Rare scales recovered from a hunted wyrm."));
    CHECK_EQ(m->atk, 6);
    CHECK_EQ(m->def, 5);
    CHECK_EQ(m->mag, 4);
    CHECK_EQ(m->dur, 8);
    CHECK_EQ(m->wt, 3);
    CHECK_EQ(m->hunt_only, 1);
    CHECK(icon_exists(m->icon));
    for (int i = 0; i < MAT_DRAGON_SCALE; i++) {
        CHECK_EQ(MATERIALS[i].hunt_only, 0); /* 既存素材は市場で買える */
    }

    /* 不正な定義は拒否される */
    DragonHuntDef x = *h;
    CHECK(content_validate_dragon_hunt(&x) == NULL);
    x = *h;
    x.reward_mat = MAT_IRON; /* 市場で買える素材は報酬にできない (報酬 = 無制限の供給口になる) */
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.reward_mat = MAT_COUNT;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.reward_qty = 0;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.reward_qty = BAL.stock_cap + 1;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.spawn_pct = 101;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.spawn_pct = -1;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.min_done = 49; /* 依頼主がまだ解放されていない */
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.min_fit = 101;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.customer = -1;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.days = 0;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.difficulty = GD_COUNT;
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    x = *h;
    x.name = "";
    CHECK(content_validate_dragon_hunt(&x) != NULL);
    CHECK(content_validate_dragon_hunt(NULL) != NULL);
    {
        MaterialDef mt[MAT_COUNT];
        memcpy(mt, MATERIALS, sizeof mt);
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) == NULL);
        mt[MAT_DRAGON_SCALE].hunt_only = 2;
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) != NULL);
        for (int i = 0; i < MAT_COUNT; i++) {
            mt[i].hunt_only = 1; /* 市場に何も並ばない */
        }
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) != NULL);
    }
}

static void test_dragon_spawn_rule(void) {
    puts("dragon_spawn_rule");
    /* 25% の判定の境界 (roll < 25): 0..24 が当たり、25..99 が外れ。範囲外のロールは丸められる */
    CHECK(guild_dragon_hunt_roll_hits(0));
    CHECK(guild_dragon_hunt_roll_hits(1));
    CHECK(guild_dragon_hunt_roll_hits(24));
    CHECK(!guild_dragon_hunt_roll_hits(25));
    CHECK(!guild_dragon_hunt_roll_hits(26));
    CHECK(!guild_dragon_hunt_roll_hits(99));
    CHECK(guild_dragon_hunt_roll_hits(-1));
    CHECK(guild_dragon_hunt_roll_hits(-1000000));
    CHECK(!guild_dragon_hunt_roll_hits(100));
    CHECK(!guild_dragon_hunt_roll_hits(1000000));
    int hits = 0;
    for (int r = 0; r < 100; r++) {
        hits += guild_dragon_hunt_roll_hits(r);
    }
    CHECK_EQ(hits, GUILD_DRAGON_HUNT.spawn_pct); /* 100 通りのロールのうち、ちょうど spawn_pct 通りが当たり */
    /* 抽選の対象: Mythic Smith (guild_done >= 50) だけ */
    CHECK(!guild_dragon_hunt_eligible(0));
    CHECK(!guild_dragon_hunt_eligible(49));
    CHECK(guild_dragon_hunt_eligible(50));
    CHECK(guild_dragon_hunt_eligible(51));
    CHECK(guild_dragon_hunt_eligible(1000000000));
    CHECK(!guild_dragon_hunt_eligible(-1));
    CHECK(!guild_dragon_hunt_eligible(-2000000000));
}

static void test_dragon_board(void) {
    puts("dragon_board");
    int seen_hit = 0, seen_miss = 0, miss_with_hunter = 0, total = 0, total_hits = 0;
    for (uint32_t s = 1; s <= 2000; s++) {
        GameState g = at_done(50);
        g.guild_rng = s;
        uint32_t before_rng = g.rng, before_craft = g.craft_rng;
        int want = guild_dragon_hunt_roll_hits(roll_of(s));
        guild_refresh_board(&g);
        total++;
        total_hits += want;
        CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);          /* 総数は常に 3 件 */
        CHECK_EQ(hunts_on_board(&g), want ? 1 : 0);                /* Dragon Hunt は最大 1 件 */
        CHECK(g.rng == before_rng && g.craft_rng == before_craft); /* 市場・クラフトの乱数に触れない */
        CHECK(game_validate(&g));
        int hunters = 0, used[CUST_TYPE_COUNT] = {0};
        for (int i = 0; i < g.guild_board_count; i++) {
            const GuildContract *c = &g.guild_board[i];
            hunters += c->customer_type == CT_WYRM_HUNTER;
            if (want) {
                CHECK(!used[c->customer_type]); /* 依頼主が重複しない (通常契約と Dragon Hunt を含め全件) */
                used[c->customer_type] = 1;
            }
            if (c->kind == GUILD_CONTRACT_STANDARD) {
                CHECK_EQ(c->reward_qty, 0);
                CHECK_EQ(c->reward_mat, 0);
                CHECK_EQ(c->min_fit, GUILD_DIFFS[c->difficulty].min_fit); /* 通常契約の数値は従来のまま */
                CHECK_EQ(c->gold, GUILD_DIFFS[c->difficulty].gold);
            }
        }
        if (want) {
            seen_hit++;
            const GuildContract *c = &g.guild_board[GUILD_OFFER_COUNT - 1];
            CHECK_EQ(c->kind, GUILD_CONTRACT_DRAGON_HUNT);
            CHECK_EQ(c->customer_type, CT_WYRM_HUNTER);
            CHECK_EQ(c->min_fit, 70);
            CHECK_EQ(c->difficulty, GD_HARD);
            CHECK_EQ(c->gold, 170);
            CHECK_EQ(c->rep, 4);
            CHECK_EQ(c->days, 3);
            CHECK_EQ(c->deadline_day, 0);
            CHECK_EQ(c->reward_mat, MAT_DRAGON_SCALE);
            CHECK_EQ(c->reward_qty, 1);
            CHECK_EQ(hunters, 1); /* 通常契約の依頼主に Wyrm Hunter はいない */
            CHECK_EQ(g.guild_next_id, 4 + GUILD_OFFER_COUNT);
            for (int i = 0; i < GUILD_OFFER_COUNT; i++) {
                CHECK_EQ(g.guild_board[i].id, 4 + i);
            }
        } else {
            seen_miss++;
            miss_with_hunter += hunters > 0;
        }
        /* 同じ乱数状態からは同じ掲示板になる */
        GameState g2 = at_done(50);
        g2.guild_rng = s;
        guild_refresh_board(&g2);
        CHECK(same_state(&g, &g2));
    }
    CHECK(seen_hit > 0 && seen_miss > 0);
    CHECK(miss_with_hunter > 0); /* 外れの掲示板では、通常契約が Wyrm Hunter を指名することもある (従来どおり) */
    CHECK(total_hits * 100 >= total * 20 && total_hits * 100 <= total * 30); /* 約 25% */
    printf("  hunt board: %d / %d refreshes (%.1f%%)\n", total_hits, total, 100.0 * total_hits / total);

    /* 乱数の消費量: 対象外は従来どおり 6 回、当たりは 1 + 4、外れは 1 + 6 */
    for (uint32_t s = 1; s <= 200; s++) {
        GameState lo = at_done(49), hi = at_done(50);
        lo.guild_rng = hi.guild_rng = s;
        guild_refresh_board(&lo);
        guild_refresh_board(&hi);
        uint32_t e6 = s, e7 = s, e5 = s;
        for (int i = 0; i < 6; i++) {
            rng_next(&e6);
        }
        for (int i = 0; i < 7; i++) {
            rng_next(&e7);
        }
        for (int i = 0; i < 5; i++) {
            rng_next(&e5);
        }
        CHECK(lo.guild_rng == e6); /* guild_done < 50: 抽選もしないし、乱数も余分に消費しない */
        CHECK_EQ(hunts_on_board(&lo), 0);
        CHECK(hi.guild_rng == (hunts_on_board(&hi) ? e5 : e7));
    }
    /* 掲示板は、guild_done が 50 未満なら新コンテンツ導入前と同一: 49 件の状態で長い間回しても Dragon Hunt は出ない */
    {
        GameState g = at_done(49);
        g.status = ST_SANDBOX; /* 30 日で終わらないよう、サンドボックスで回す */
        g.money = 100000;
        for (int d = 0; d < 300; d++) {
            CHECK_EQ(hunts_on_board(&g), 0);
            CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);
            pass_day(&g);
            g.guild_done = 49;
            g.guild_expired = 0;
            g.guild_active_count = 0;
            g.money = 100000;
        }
    }
    /* 実際の日の進行でも 50 件以上なら出る (どちらの結果も決定的) */
    {
        GameState a = at_done(50), b = at_done(50);
        a.status = b.status = ST_SANDBOX;
        int seen = 0;
        for (int d = 0; d < 200; d++) {
            pass_day(&a);
            pass_day(&b);
            a.money = b.money = 100000;
            CHECK(same_state(&a, &b));
            CHECK_EQ(a.guild_board_count, GUILD_OFFER_COUNT);
            CHECK(hunts_on_board(&a) <= 1);
            seen += hunts_on_board(&a);
            a.guild_active_count = b.guild_active_count = 0;
            memset(a.guild_active, 0, sizeof a.guild_active);
            memset(b.guild_active, 0, sizeof b.guild_active);
            CHECK(game_validate(&a));
        }
        CHECK(seen > 10 && seen < 120); /* 200 日で約 50 回 */
    }
    /* 受注中の Dragon Hunt があっても、掲示板の総数と受注の上限は従来どおり */
    {
        GameState g = hunt_game();
        guild_refresh_board(&g);
        CHECK_EQ(g.guild_board_count, GUILD_OFFER_COUNT);
        CHECK_EQ(g.guild_active_count, 1);
        CHECK(game_validate(&g));
        CHECK_EQ(GUILD_ACTIVE_MAX, 2);
        CHECK_EQ(GUILD_OFFER_COUNT, 3);
    }
}

static int fit_of(const GameState *g, int idx) {
    return equip_fit(CT_WYRM_HUNTER, &g->inv[idx]);
}

static void test_dragon_deliver(void) {
    puts("dragon_deliver");
    /* 受注すると期限が付き、種類と報酬は保たれる */
    {
        GameState g = hunt_game();
        CHECK_EQ(g.guild_active[0].kind, GUILD_CONTRACT_DRAGON_HUNT);
        CHECK_EQ(g.guild_active[0].deadline_day, g.day + 3 - 1);
        CHECK_EQ(g.guild_active[0].reward_mat, MAT_DRAGON_SCALE);
        CHECK_EQ(g.guild_active[0].reward_qty, 1);
    }
    /* Wyrmcleaver 以外では納品できない */
    {
        GameState g = hunt_game();
        int sw = give_item(&g, EQ_SWORD, MAT_IRON, MAT_IRON);
        GameState before = g;
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, sw, &o), GUILD_ERR_WRONG_TYPE);
        CHECK_EQ(o.reward_qty, 0);
        CHECK(same_state(&g, &before));
    }
    /* 最低 FIT: 70 未満は不可、70 以上なら可 (通常の equip_fit で判定する) */
    {
        GameState g = hunt_game();
        int weak = give_item(&g, EQ_WYRMCLEAVER, MAT_WOOD, MAT_WOOD);
        CHECK(fit_of(&g, weak) < 70);
        GameState before = g;
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, weak, &o), GUILD_ERR_LOW_FIT);
        CHECK_EQ(o.fit, fit_of(&g, weak));
        CHECK(same_state(&g, &before));
        CHECK_EQ(guild_item_fit(&g, 0, weak), fit_of(&g, weak));
        int strong = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK(fit_of(&g, strong) >= 70);
    }
    /* ちょうど FIT 70 と 69 (能力値を直接調整して境界を作る) */
    {
        GameState g = hunt_game();
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        int found70 = 0, found69 = 0, a70 = 0, d70 = 0, a69 = 0, d69 = 0;
        for (int atk = 0; atk <= 30; atk++) {
            for (int dur = 0; dur <= 30; dur++) {
                g.inv[it].atk = atk;
                g.inv[it].dur = dur;
                int f = fit_of(&g, it);
                if (f == 70 && !found70) {
                    found70 = 1, a70 = atk, d70 = dur;
                }
                if (f == 69 && !found69) {
                    found69 = 1, a69 = atk, d69 = dur;
                }
            }
        }
        CHECK(found70 && found69);
        GameState at69 = g, at70 = g;
        at69.inv[it].atk = a69;
        at69.inv[it].dur = d69;
        at70.inv[it].atk = a70;
        at70.inv[it].dur = d70;
        CHECK_EQ(fit_of(&at69, it), 69);
        CHECK_EQ(fit_of(&at70, it), 70);
        GameState before = at69;
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&at69, 0, it, &o), GUILD_ERR_LOW_FIT);
        CHECK(same_state(&at69, &before));
        CHECK_EQ(guild_deliver(&at70, 0, it, &o), GUILD_OK);
        CHECK_EQ(o.reward_qty, 1);
    }
    /* 成功: ゴールド・評判・納品件数・素材が更新され、原価は 0 */
    {
        GameState g = hunt_game();
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK(fit_of(&g, it) >= 70);
        int cost = g.inv[it].cost, money = g.money, rep = g.rep, done = g.guild_done, inv = g.inv_count;
        int iron = g.mat_stock[MAT_IRON], iron_cost = g.mat_cost[MAT_IRON];
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 0);
        GuildOutcome o;
        uint32_t r0 = g.rng, g0 = g.guild_rng, c0 = g.craft_rng;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
        CHECK_EQ(o.gold, 170);
        CHECK_EQ(o.rep_gain, 4);
        CHECK_EQ(o.reward_mat, MAT_DRAGON_SCALE);
        CHECK_EQ(o.reward_qty, 1);
        CHECK_EQ(o.profit, 170 - cost);
        CHECK_EQ(g.money, money + 170);
        CHECK_EQ(g.rep, rep + 4);
        CHECK_EQ(g.guild_done, done + 1);
        CHECK_EQ(g.report.guild_done, 1);
        CHECK_EQ(g.report.guild_gold, 170);
        CHECK_EQ(g.inv_count, inv - 1);
        CHECK_EQ(g.guild_active_count, 0);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 1);
        CHECK_EQ(g.mat_cost[MAT_DRAGON_SCALE], 0); /* 無料の報酬: 原価 0 (docs/GAMEPLAY.md) */
        CHECK_EQ(g.mat_stock[MAT_IRON], iron);     /* ほかの素材は変わらない */
        CHECK_EQ(g.mat_cost[MAT_IRON], iron_cost);
        CHECK(g.rng == r0 && g.guild_rng == g0 && g.craft_rng == c0); /* 納品は乱数を引かない */
        CHECK(game_validate(&g));
    }
    /* 評判の上限 */
    {
        GameState g = hunt_game();
        g.rep = BAL.rep_cap - 1;
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
        CHECK_EQ(g.rep, BAL.rep_cap);
        CHECK_EQ(o.rep_gain, 1);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 1);
    }
    /* 在庫上限: 満杯では納品できず、何も変わらない。98 なら 99 になる */
    {
        GameState g = hunt_game();
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        g.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap;
        g.mat_cost[MAT_DRAGON_SCALE] = 0;
        CHECK(game_validate(&g));
        GameState before = g;
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_ERR_REWARD_FULL);
        CHECK_EQ(o.reward_qty, 0);
        CHECK(same_state(&g, &before)); /* 装備・ゴールド・評判・契約・納品件数・素材すべてそのまま */
        g.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap - 1;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], BAL.stock_cap);
        CHECK(game_validate(&g));
    }
    /* 満杯でも通常契約は納品できる (報酬がないので在庫は無関係) */
    {
        GameState g = at_done(50);
        g.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap;
        put_contract(&g, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&g, 0, it, NULL), GUILD_OK);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], BAL.stock_cap);
    }
    /* 通常契約 (依頼主が Wyrm Hunter でも) から Dragon Scale は出ない。壊れた報酬欄があっても付与しない */
    {
        GameState g = at_done(50);
        put_contract(&g, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        GuildOutcome o;
        CHECK_EQ(guild_deliver(&g, 0, it, &o), GUILD_OK);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(o.reward_qty, 0);
        CHECK_EQ(o.gold, GUILD_DIFFS[GD_STANDARD].gold);
        GameState b = at_done(50);
        put_contract(&b, CT_WYRM_HUNTER, 50, GD_STANDARD);
        b.guild_board[0].reward_mat = MAT_DRAGON_SCALE;
        b.guild_board[0].reward_qty = 3; /* 通常契約なのに報酬がある: 不正な状態 */
        CHECK(!game_validate(&b));
        CHECK_EQ(guild_accept(&b, 0), GUILD_OK);
        int it2 = give_item(&b, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&b, 0, it2, NULL), GUILD_OK);
        CHECK_EQ(b.mat_stock[MAT_DRAGON_SCALE], 0);
    }
    /* 失効した契約からは何も付与されない。期限日までは有効 */
    {
        GameState g = hunt_game(); /* 1 日目に受注、期限 3 日目 */
        CHECK_EQ(g.guild_active[0].deadline_day, 3);
        pass_day(&g);
        pass_day(&g);
        CHECK_EQ(g.day, 3);
        CHECK_EQ(g.guild_active_count, 1); /* 3 日目の朝はまだ納品できる */
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        GameState keep = g;
        CHECK_EQ(guild_deliver(&keep, 0, it, NULL), GUILD_OK);
        CHECK_EQ(keep.mat_stock[MAT_DRAGON_SCALE], 1);
        int expired0 = g.guild_expired, done0 = g.guild_done;
        pass_day(&g); /* 3 日目の精算で失効 */
        CHECK_EQ(g.guild_expired, expired0 + 1);
        CHECK_EQ(g.guild_done, done0);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(g.guild_active_count, 0);
        CHECK_EQ(guild_deliver(&g, 0, 0, NULL), GUILD_ERR_NO_CONTRACT);
        CHECK(game_validate(&g));
    }
    /* フェーズ違い・添字の誤りは何も変えない */
    {
        GameState g = hunt_game();
        int it = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        GameState before = g;
        CHECK_EQ(guild_deliver(&g, 0, it + 5, NULL), GUILD_ERR_NO_ITEM);
        CHECK_EQ(guild_deliver(&g, 1, it, NULL), GUILD_ERR_NO_CONTRACT);
        CHECK_EQ(guild_deliver(&g, 9, it, NULL), GUILD_ERR_RANGE);
        CHECK(same_state(&g, &before));
        g.guild_active[0].reward_mat = MAT_IRON; /* 壊れた報酬欄: 納品は拒否し、市場素材を無料で増やさない */
        before = g;
        CHECK_EQ(guild_deliver(&g, 0, it, NULL), GUILD_ERR_RANGE);
        CHECK(same_state(&g, &before));
    }
    /* 2 件同時受注でも、それぞれ 1 つずつ */
    {
        GameState g = hunt_game();
        g.guild_board[0] = make_hunt(&g);
        g.guild_board_count = 1;
        CHECK_EQ(guild_accept(&g, 0), GUILD_OK);
        CHECK_EQ(g.guild_active_count, 2);
        int a = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&g, 0, a, NULL), GUILD_OK);
        g.mat_stock[MAT_IRON] = 0;
        g.mat_cost[MAT_IRON] = 0;
        int b = give_item(&g, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&g, 0, b, NULL), GUILD_OK);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 2);
    }
}

static void test_dragon_market(void) {
    puts("dragon_market");
    GameState g = fresh();
    g.money = 100000;
    GameState before = g;
    CHECK_EQ(market_buy(&g, MAT_DRAGON_SCALE, 1), R_ERR_NOT_FOR_SALE);
    CHECK_EQ(market_buy(&g, MAT_DRAGON_SCALE, BAL.stock_cap), R_ERR_NOT_FOR_SALE);
    CHECK(same_state(&g, &before)); /* 在庫・所持金・価格・原価すべて不変 */
    CHECK_EQ(market_quote(&g, MAT_DRAGON_SCALE, 1), -1);
    CHECK_EQ(market_quote(&g, MAT_DRAGON_SCALE, 10), -1);
    CHECK_EQ(market_buy(&g, MAT_DRAGON_SCALE, 0), R_ERR_RANGE); /* 範囲の誤りは従来どおり先に判定する */
    CHECK_EQ(market_buy(&g, MAT_IRON, 1), R_OK);                /* 他の素材は買える */
    CHECK(market_quote(&g, MAT_IRON, 1) > 0);
    /* 価格を操作する手段もない: 在庫を編集する API はなく、市場更新でも価格は動かない */
    for (int d = 0; d < 50; d++) {
        market_update(&g);
        CHECK_EQ(g.price[MAT_DRAGON_SCALE], MATERIALS[MAT_DRAGON_SCALE].base_price);
        CHECK_EQ(g.swing_pm[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 0);
    }
    /* 市場更新: 買える素材の数だけ (7 回) 乱数を引く。買えない素材のために引かない */
    for (uint32_t s = 1; s <= 50; s++) {
        GameState x = fresh();
        x.rng = s;
        uint32_t expect = s;
        int sw[MAT_COUNT];
        memcpy(sw, x.swing_pm, sizeof sw);
        market_update(&x);
        int n = 0;
        for (int m = 0; m < MAT_COUNT; m++) {
            if (MATERIALS[m].hunt_only) {
                continue;
            }
            n++;
            int w =
                sw[m] * BAL.market_persist_pct / 100 + rng_range(&expect, -BAL.market_noise_pm, BAL.market_noise_pm);
            w = w < -BAL.market_swing_pm ? -BAL.market_swing_pm : w > BAL.market_swing_pm ? BAL.market_swing_pm : w;
            CHECK_EQ(x.swing_pm[m], w);
        }
        CHECK_EQ(n, MAT_COUNT - 1);
        CHECK(x.rng == expect);
    }
    /* 同じシードなら、新素材の追加前と同じ価格推移 (固定値は test_rng_streams も検査する) */
    {
        GameState x = fresh(), y = fresh();
        for (int d = 0; d < 10; d++) {
            pass_day(&x);
            pass_day(&y);
            x.money = y.money = 10000;
        }
        CHECK(same_state(&x, &y));
        CHECK_EQ(x.price[MAT_IRON] >= MATERIALS[MAT_IRON].min_price, 1);
    }
    /* 日の進行は、素材の購入しない限り Dragon Scale の在庫を変えない */
    CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 0);
    CHECK(game_validate(&g));
    /* 検証: 負数・上限超え・原価の不整合は拒否 */
    {
        GameState x = fresh();
        x.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap;
        CHECK(game_validate(&x));
        x.mat_stock[MAT_DRAGON_SCALE] = BAL.stock_cap + 1;
        CHECK(!game_validate(&x));
        x.mat_stock[MAT_DRAGON_SCALE] = -1;
        CHECK(!game_validate(&x));
        x.mat_stock[MAT_DRAGON_SCALE] = 0;
        x.mat_cost[MAT_DRAGON_SCALE] = 5; /* 在庫 0 に原価はない */
        CHECK(!game_validate(&x));
        x.mat_stock[MAT_DRAGON_SCALE] = 2;
        x.mat_cost[MAT_DRAGON_SCALE] = 0; /* 無料の報酬は有効 */
        CHECK(game_validate(&x));
        x.mat_cost[MAT_DRAGON_SCALE] = -1;
        CHECK(!game_validate(&x));
    }
}

static void test_dragon_craft(void) {
    puts("dragon_craft");
    const MaterialDef *ds = &MATERIALS[MAT_DRAGON_SCALE];
    for (int variant = 0; variant < 3; variant++) {
        GameState g = at_done(50);
        g.up_level[UP_FORGE] = variant;
        g.mat_stock[MAT_DRAGON_SCALE] = 3;
        g.mat_cost[MAT_DRAGON_SCALE] = 0; /* 報酬の素材: 原価 0 */
        g.mat_stock[MAT_IRON] = 3;
        g.mat_cost[MAT_IRON] = 33; /* 1 個 11 */
        CHECK(game_validate(&g));
        /* 主素材 2 + 副素材 1 の規則 (主 = Dragon Scale、副 = Iron) */
        CHECK_EQ(can_craft(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_IRON), R_OK);
        pin_craft(&g, QUALITY_COMMON);
        uint32_t rng0 = g.rng, guild0 = g.guild_rng;
        int idx = -1;
        CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_IRON, &idx), R_OK);
        CHECK(g.rng == rng0 && g.guild_rng == guild0); /* クラフトは craft_rng だけを使う */
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 1);
        CHECK_EQ(g.mat_stock[MAT_IRON], 2);
        const Equipment *e = &g.inv[idx];
        CHECK_EQ(e->main, MAT_DRAGON_SCALE);
        CHECK_EQ(e->sub, MAT_IRON);
        const EquipDef *q = &EQUIPS[EQ_WYRMCLEAVER];
        const MaterialDef *ir = &MATERIALS[MAT_IRON];
        double mult = upgrade_effect(UP_FORGE, variant) / 1000.0;
        /* 混合: base + (2 * main + sub) / 3 (WT に Forge は掛けない)。既存の式と同じ */
        CHECK_EQ(e->atk, (int)floor((q->atk + (2.0 * ds->atk + ir->atk) / 3.0) * mult + 0.5));
        CHECK_EQ(e->def, (int)floor((q->def + (2.0 * ds->def + ir->def) / 3.0) * mult + 0.5));
        CHECK_EQ(e->mag, (int)floor((q->mag + (2.0 * ds->mag + ir->mag) / 3.0) * mult + 0.5));
        CHECK_EQ(e->dur, (int)floor((q->dur + (2.0 * ds->dur + ir->dur) / 3.0) * mult + 0.5));
        CHECK_EQ(e->wt, (int)floor(q->wt + (2.0 * ds->wt + ir->wt) / 3.0 + 0.5));
        CHECK_EQ(e->quality, QUALITY_COMMON);
        CHECK_EQ(e->cost, 11); /* Iron 1 個分だけ。Dragon Scale は無料の報酬 */
        Equipment chk = *e;
        equipment_price(&chk);
        CHECK_EQ(e->value, chk.value);
        CHECK_EQ(e->rec_price, chk.rec_price);
        CHECK_EQ(e->price, e->rec_price);
        CHECK(game_validate(&g));
        /* 副素材にも使え、主素材 Iron + 副素材 Scale */
        GameState h = g;
        h.mat_stock[MAT_IRON] = 3;
        h.mat_cost[MAT_IRON] = 33;
        h.mat_stock[MAT_DRAGON_SCALE] = 0;
        h.mat_cost[MAT_DRAGON_SCALE] = 0;
        CHECK_EQ(can_craft(&h, EQ_WYRMCLEAVER, MAT_IRON, MAT_DRAGON_SCALE), R_ERR_MATERIAL); /* 副素材の Scale がない */
        h.mat_stock[MAT_DRAGON_SCALE] = 1;
        CHECK_EQ(can_craft(&h, EQ_WYRMCLEAVER, MAT_IRON, MAT_DRAGON_SCALE), R_OK);
        pin_craft(&h, QUALITY_COMMON);
        CHECK_EQ(craft_equipment(&h, EQ_WYRMCLEAVER, MAT_IRON, MAT_DRAGON_SCALE, &idx), R_OK);
        const Equipment *e2 = &h.inv[idx];
        CHECK_EQ(h.mat_stock[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(h.mat_cost[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(e2->atk, (int)floor((q->atk + (2.0 * ir->atk + ds->atk) / 3.0) * mult + 0.5));
        CHECK_EQ(e2->dur, (int)floor((q->dur + (2.0 * ir->dur + ds->dur) / 3.0) * mult + 0.5));
        CHECK_EQ(e2->cost, 22); /* Iron 2 個分 */
        CHECK(game_validate(&h));
        /* 主 = 副 = Dragon Scale は 3 個必要 */
        GameState k = at_done(50);
        k.mat_stock[MAT_DRAGON_SCALE] = 2;
        CHECK_EQ(can_craft(&k, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE), R_ERR_MATERIAL);
        k.mat_stock[MAT_DRAGON_SCALE] = 3;
        CHECK_EQ(can_craft(&k, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE), R_OK);
        CHECK_EQ(craft_equipment(&k, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, NULL), R_OK);
        CHECK_EQ(k.mat_stock[MAT_DRAGON_SCALE], 0);
        CHECK(game_validate(&k));
    }
    /* 在庫がなければクラフトできず、何も変わらない (購入手段がない) */
    {
        GameState g = at_done(50);
        GameState before = g;
        CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_IRON, NULL), R_ERR_MATERIAL);
        CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_IRON, MAT_DRAGON_SCALE, NULL), R_ERR_MATERIAL);
        CHECK(same_state(&g, &before));
    }
    /* 専用の例外はない: ほかの装備にも使える。品質と特性 (Masterwork / Legendary) は equipment_build と一致する */
    for (int quality = QUALITY_COMMON; quality < QUALITY_COUNT; quality++) {
        GameState g = at_done(50);
        g.up_level[UP_FORGE] = 2;
        g.mat_stock[MAT_DRAGON_SCALE] = 3;
        g.mat_stock[MAT_STEEL] = 3;
        g.mat_cost[MAT_STEEL] = 36;
        g.rep = 0;
        /* 品質が quality になる乱数に固定する (guild_rank = Mythic) */
        uint32_t found = 0;
        for (uint32_t s = 1; s < 200000 && !found; s++) {
            uint32_t t = s;
            if (quality_from_roll(guild_rank(g.guild_done), 2, rng_range(&t, 0, 99)) == quality) {
                found = s;
            }
        }
        CHECK(found != 0);
        g.craft_rng = found;
        int idx = -1;
        CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_DRAGON_SCALE, MAT_STEEL, &idx), R_OK);
        Equipment ref;
        memset(&ref, 0, sizeof ref);
        ref.type = EQ_SWORD;
        ref.main = MAT_DRAGON_SCALE;
        ref.sub = MAT_STEEL;
        ref.cost = g.inv[idx].cost;
        equipment_build(&ref, 2, g.inv[idx].quality, g.inv[idx].trait);
        equipment_price(&ref);
        CHECK_EQ(g.inv[idx].quality, quality);
        CHECK_EQ(g.inv[idx].atk, ref.atk);
        CHECK_EQ(g.inv[idx].def, ref.def);
        CHECK_EQ(g.inv[idx].mag, ref.mag);
        CHECK_EQ(g.inv[idx].dur, ref.dur);
        CHECK_EQ(g.inv[idx].wt, ref.wt);
        CHECK_EQ(g.inv[idx].value, ref.value);
        CHECK_EQ(g.inv[idx].rec_price, ref.rec_price);
        CHECK_EQ(g.inv[idx].cost, 12); /* Steel 1 個分 */
        CHECK_EQ(QUALITY_DEFS[g.inv[idx].quality].has_trait != 0, g.inv[idx].trait != EQ_TRAIT_NONE);
        CHECK(game_validate(&g));
    }
    /* 見積もり: 在庫があれば在庫の平均原価 (無料なので 0)、なければ基準価格の目安 */
    {
        GameState g = at_done(50);
        CHECK_EQ(estimate_craft_cost(&g, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE),
                 3 * market_unit_price(&g, MAT_DRAGON_SCALE));
        g.mat_stock[MAT_DRAGON_SCALE] = 3;
        CHECK_EQ(estimate_craft_cost(&g, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE), 0);
    }
    /* 保存して読み込める (Dragon Scale 製の装備と在庫) */
    {
        GameState g = at_done(50);
        g.mat_stock[MAT_DRAGON_SCALE] = 5;
        g.mat_stock[MAT_STEEL] = 3;
        g.mat_cost[MAT_STEEL] = 36;
        pin_craft(&g, QUALITY_MASTERWORK);
        int idx = -1;
        CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_STEEL, &idx), R_OK);
        CHECK_EQ(g.mat_stock[MAT_DRAGON_SCALE], 3);
        char *t = save_text(&g);
        CHECK(strstr(t, "\nmat 7 3 0 ") != NULL);
        GameState r;
        CHECK_EQ(load_text(&r, t), SAVE_OK);
        CHECK(same_state(&g, &r));
        CHECK_EQ(r.inv[0].main, MAT_DRAGON_SCALE);
        char *t2 = save_text(&r);
        CHECK(!strcmp(t, t2));
        free(t);
        free(t2);
    }
}

static int wyrm_value(const GameState *g, int main, int sub, int forge) { /* 装備の価値 (Common、原価は在庫の平均) */
    Equipment e;
    memset(&e, 0, sizeof e);
    e.type = EQ_WYRMCLEAVER;
    e.main = main;
    e.sub = sub;
    equipment_compute_stats(&e, forge);
    e.cost = estimate_craft_cost(g, main, sub);
    equipment_price(&e);
    return e.value;
}

static void test_dragon_rarity(void) {
    puts("dragon_rarity");
    /* 希少度ボーナスは素材テーブルの値: 従来の素材はすべて 0 (価値の式は従来のまま)、Dragon Scale だけが持つ */
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(MATERIALS[m].rarity_value, m == MAT_DRAGON_SCALE ? 18 : 0);
    }
    {
        MaterialDef mt[MAT_COUNT];
        memcpy(mt, MATERIALS, sizeof mt);
        mt[MAT_DRAGON_SCALE].rarity_value = -1;
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) != NULL);
        mt[MAT_DRAGON_SCALE].rarity_value = 201;
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) != NULL);
        mt[MAT_DRAGON_SCALE].rarity_value = 200;
        CHECK(content_validate_tables(mt, MAT_COUNT, EQUIPS, EQ_COUNT, CUSTOMER_DEFS, CUST_TYPE_COUNT, UPGRADES,
                                      UP_COUNT) == NULL);
    }
    /* 式: value = 原価 * 0.6 + 能力スコア * 1.2 + (2 * 主の希少度 +
     * 副の希少度)。同じ能力・原価で素材だけ変えた差が、ちょうど希少度 */
    for (int eq = 0; eq < EQ_COUNT; eq++) {
        for (int mm = 0; mm < MAT_COUNT; mm++) {
            for (int sm = 0; sm < MAT_COUNT; sm++) {
                Equipment a;
                memset(&a, 0, sizeof a);
                a.type = eq;
                a.main = MAT_IRON;
                a.sub = MAT_IRON;
                a.atk = 20;
                a.def = 9;
                a.mag = 3;
                a.dur = 17;
                a.wt = 7;
                a.cost = 41;
                Equipment b = a;
                b.main = mm;
                b.sub = sm;
                equipment_price(&a);
                equipment_price(&b);
                int bonus = 2 * MATERIALS[mm].rarity_value + MATERIALS[sm].rarity_value;
                CHECK_EQ(b.value, a.value + bonus);
                CHECK(b.rec_price >= a.rec_price);
                CHECK(b.rec_price >= 1 && b.rec_price <= BAL.price_cap);
            }
        }
    }
    /* 範囲外の素材 ID の装備 (壊れたデータ) でも価値の計算は落ちず、ボーナスなし */
    {
        Equipment e;
        memset(&e, 0, sizeof e);
        e.atk = 10;
        e.main = -1;
        e.sub = MAT_COUNT + 5;
        equipment_price(&e);
        Equipment f = e;
        f.main = f.sub = MAT_IRON;
        equipment_price(&f);
        CHECK_EQ(e.value, f.value);
    }
    /* ねらい: Dragon Scale を使うと、通常の素材より価値の高い装備になる (Forge 0..2) */
    for (int forge = 0; forge <= 2; forge++) {
        GameState x = at_done(50);
        x.mat_stock[MAT_DRAGON_SCALE] = 3;
        x.mat_stock[MAT_STEEL] = 3;
        x.mat_cost[MAT_STEEL] = 36;
        x.mat_stock[MAT_IRON] = 3;
        x.mat_cost[MAT_IRON] = 30;
        int steel2 = wyrm_value(&x, MAT_STEEL, MAT_STEEL, forge);
        int iron2 = wyrm_value(&x, MAT_IRON, MAT_IRON, forge);
        CHECK(wyrm_value(&x, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, forge) > steel2 + 20);
        CHECK(wyrm_value(&x, MAT_DRAGON_SCALE, MAT_STEEL, forge) > steel2);
        CHECK(wyrm_value(&x, MAT_STEEL, MAT_DRAGON_SCALE, forge) > steel2);
        CHECK(wyrm_value(&x, MAT_DRAGON_SCALE, MAT_IRON, forge) > iron2);
        CHECK(wyrm_value(&x, MAT_IRON, MAT_DRAGON_SCALE, forge) > iron2);
    }
    /* 実際のクラフトでも、品質・特性が付いた後の価値にボーナスが乗る (品質ごと)。値は保存しても変わらない */
    for (int quality = QUALITY_COMMON; quality < QUALITY_COUNT; quality++) {
        GameState x = at_done(50);
        x.mat_stock[MAT_DRAGON_SCALE] = 3;
        uint32_t found = 0;
        for (uint32_t s = 1; s < 200000 && !found; s++) {
            uint32_t t = s;
            if (quality_from_roll(guild_rank(x.guild_done), 0, rng_range(&t, 0, 99)) == quality) {
                found = s;
            }
        }
        CHECK(found != 0);
        x.craft_rng = found;
        int idx = -1;
        CHECK_EQ(craft_equipment(&x, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, &idx), R_OK);
        Equipment plain = x.inv[idx];
        plain.main = plain.sub = MAT_IRON;
        equipment_price(&plain); /* 同じ最終能力・原価で、希少度のない素材として計算した価値 */
        CHECK_EQ(x.inv[idx].value, plain.value + 54);
        CHECK_EQ(x.inv[idx].cost, 0);
        char *t = save_text(&x);
        GameState r;
        CHECK_EQ(load_text(&r, t), SAVE_OK);
        CHECK_EQ(r.inv[0].value, x.inv[idx].value); /* 保存された価値はそのまま (読み込みで再計算しない) */
        CHECK_EQ(r.inv[0].rec_price, x.inv[idx].rec_price);
        free(t);
    }
    /* Dragon Scale 製の装備は、通常どおり客に推奨価格で売れる (Wyrm Hunter) */
    {
        GameState x = at_done(50);
        x.mat_stock[MAT_DRAGON_SCALE] = 3;
        pin_craft(&x, QUALITY_COMMON);
        int idx = -1;
        CHECK_EQ(craft_equipment(&x, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, &idx), R_OK);
        CHECK(x.inv[idx].rec_price > 100);
        int price = x.inv[idx].price, money = x.money;
        force_open(&x, mk_customer(CT_WYRM_HUNTER, 210, 50, 1100));
        SaleOutcome o;
        CHECK_EQ(customer_offer(&x, idx, &o), R_OK);
        CHECK(o.sold);
        CHECK_EQ(x.money, money + price);
        CHECK(game_validate(&x));
    }
}

/* ---- セーブ: v4 と、旧バージョンからの移行 ---- */
static char *gb_line(char *buf, size_t n, const char *key, int id, int ct, int fit, int diff, int gold, int rep,
                     int days, int dl, int kind, int rm, int rq) {
    snprintf(buf, n, "%s %d %d %d %d %d %d %d %d %d %d %d", key, id, ct, fit, diff, gold, rep, days, dl, kind, rm, rq);
    return buf;
}
static char *as_v3_build(const char *t4) { /* v4 の本文から、v3 のビルドが書いたはずの本文を作る */
    size_t len = strlen(t4);
    char *out = malloc(len + 16), *w = out;
    const char *p = t4;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        char line[256];
        memcpy(line, p, n);
        line[n] = 0;
        if (!strncmp(line, "LOOTSMITH_SAVE", 14)) {
            strcpy(line, "LOOTSMITH_SAVE 3");
        } else if (!strncmp(line, "counts ", 7)) {
            strcpy(line, "counts 7 10 11 3");
        } else if (!strncmp(line, "mat 7 ", 6)) {
            p += n + 1;
            continue; /* v3 のビルドに Dragon Scale はなかった */
        } else if (!strncmp(line, "gb ", 3) || !strncmp(line, "ga ", 3)) {
            for (int k = 0; k < 3; k++) { /* 末尾の <kind> <reward_mat> <reward_qty> を落とす */
                char *sp = strrchr(line, ' ');
                *sp = 0;
            }
        }
        w += sprintf(w, "%s\n", line);
        p += n + (e ? 1 : 0);
        if (!e) {
            break;
        }
    }
    *w = 0;
    return out;
}

static GameState hunt_save_state(void) { /* 掲示中 1 件 + 受注中 1 件の Dragon Hunt */
    GameState g = hunt_game();
    g.guild_board[0] = make_hunt(&g);
    g.guild_board_count = 1;
    CHECK(game_validate(&g));
    return g;
}

static void test_dragon_save(void) {
    puts("dragon_save");
    CHECK_EQ(SAVE_VERSION, 4);
    /* 掲示中・受注中の Dragon Hunt を保存して復元できる */
    GameState g = hunt_save_state();
    char *t = save_text(&g);
    CHECK(!strncmp(t, "LOOTSMITH_SAVE 4\n", 17));
    CHECK(strstr(t, "\ngb 5 10 70 2 170 4 3 0 1 7 1\n") != NULL);
    CHECK(strstr(t, "\nga 4 10 70 2 170 4 3 3 1 7 1\n") != NULL);
    GameState r;
    CHECK_EQ(load_text(&r, t), SAVE_OK);
    CHECK(same_state(&g, &r));
    CHECK_EQ(r.guild_board[0].kind, GUILD_CONTRACT_DRAGON_HUNT);
    CHECK_EQ(r.guild_active[0].reward_mat, MAT_DRAGON_SCALE);
    CHECK_EQ(r.guild_active[0].reward_qty, 1);
    char *t2 = save_text(&r);
    CHECK(!strcmp(t, t2));
    free(t2);
    /* 復元した受注中の契約を納品すると素材が付く */
    {
        int it = give_item(&r, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&r, 0, it, NULL), GUILD_OK);
        CHECK_EQ(r.mat_stock[MAT_DRAGON_SCALE], 1);
    }
    /* 通常契約だけの状態の v4 */
    {
        GameState s = at_done(20);
        char *ts = save_text(&s);
        GameState rs;
        CHECK_EQ(load_text(&rs, ts), SAVE_OK);
        CHECK(same_state(&s, &rs));
        free(ts);
    }

    /* 不正な契約は拒否され、読み込み先は変わらない */
    GameState keep = fresh(), probe = keep;
    GameState base = hunt_game(); /* 受注中の 1 件だけ (掲示板は空) */
    base.guild_board[0] = make_hunt(&base);
    base.guild_board_count = 1;
    char *bt = save_text(&base);
    CHECK_EQ(load_text(&probe, bt), SAVE_OK);
    keep = probe;
    char line[128], *v;
#define REJECT_GB(expect, ...)                                                                                         \
    do {                                                                                                               \
        v = replace_line(bt, "gb ", gb_line(line, sizeof line, "gb", __VA_ARGS__));                                    \
        CHECK_EQ(load_text(&probe, v), expect);                                                                        \
        CHECK(same_state(&probe, &keep));                                                                              \
        free(v);                                                                                                       \
    } while (0)
    /* gb: id ct fit diff gold rep days deadline kind mat qty */
    v = replace_line(bt, "gb ", gb_line(line, sizeof line, "gb", 5, 10, 70, 2, 170, 4, 3, 0, 1, 7, 1));
    CHECK_EQ(load_text(&probe, v), SAVE_OK); /* 対照: 正しい行 */
    free(v);
    probe = keep;
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 0, 7, 1);   /* 通常契約に報酬 */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 0, 7, 0);   /* 通常契約の報酬素材は 0 (正規形) */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, 7, 0);   /* Dragon Hunt に報酬がない */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, 7, -1);  /* 負の報酬数 */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, 7, 100); /* 報酬数が stock_cap を超える */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, 0, 1);   /* 市場で買える素材は報酬にできない */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, 99, 1);  /* 不明な素材 ID */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 1, -1, 1);
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, 2, 7, 1); /* 不明な契約種別 */
    REJECT_GB(SAVE_ERR_VALUE, 5, 10, 70, 2, 170, 4, 3, 0, -1, 7, 1);
    REJECT_GB(SAVE_ERR_VALUE, 5, 0, 70, 2, 170, 4, 3, 0, 1, 7, 1); /* 依頼主が Wyrm Hunter ではない */
#undef REJECT_GB
    v = replace_line(bt, "gb ", "gb 5 10 70 2 170 4 3 0"); /* v4 で 8 フィールドの行 */
    CHECK_EQ(load_text(&probe, v), SAVE_ERR_FORMAT);
    CHECK(same_state(&probe, &keep));
    free(v);
    v = replace_line(bt, "ga ", "ga 4 10 70 2 170 4 3 3 1 7"); /* 途中で切れている */
    CHECK_EQ(load_text(&probe, v), SAVE_ERR_FORMAT);
    free(v);
    v = replace_line(bt, "ga ", "ga 4 10 70 2 170 4 3 3 1 7 1x");
    CHECK_EQ(load_text(&probe, v), SAVE_ERR_FORMAT);
    free(v);
    /* 書き手が知らなかった素材 (counts が 7) は、報酬にもできない */
    v = replace_line(bt, "counts ", "counts 7 10 11 3");
    CHECK(load_text(&probe, v) != SAVE_OK);
    free(v);
    /* 状態の不整合 (掲示板に 2 件、50 件未満) */
    {
        GameState x = base;
        x.guild_board[1] = make_hunt(&x);
        x.guild_board_count = 2;
        CHECK(!game_validate(&x));
        char *tx = save_text(&x);
        CHECK_EQ(load_text(&probe, tx), SAVE_ERR_VALUE);
        free(tx);
        GameState y = base;
        y.guild_done = 49;
        CHECK(!game_validate(&y));
        char *ty = save_text(&y);
        CHECK_EQ(load_text(&probe, ty), SAVE_ERR_VALUE);
        CHECK(same_state(&probe, &keep));
        free(ty);
    }
    /* 素材レコード: 上限超え・負数は拒否 */
    {
        const char *bad[] = {"mat 7 100 0 30 30 0", "mat 7 -1 0 30 30 0", "mat 7 0 5 30 30 0", "mat 7 3 -1 30 30 0"};
        for (int i = 0; i < 4; i++) {
            v = replace_line(bt, "mat 7 ", bad[i]);
            CHECK_EQ(load_text(&probe, v), SAVE_ERR_VALUE);
            CHECK(same_state(&probe, &keep));
            free(v);
        }
        v = replace_line(bt, "mat 7 ", "mat 7 3 0 30 30 0");
        CHECK_EQ(load_text(&probe, v), SAVE_OK); /* 無料の報酬: 原価 0 で有効 */
        CHECK_EQ(probe.mat_stock[MAT_DRAGON_SCALE], 3);
        free(v);
        probe = keep;
    }
    free(bt);

    /* v3 のビルドのセーブ (旧契約): 通常契約として復元され、Dragon Scale は付かない */
    {
        GameState s = at_done(50); /* Wyrm Hunter の契約が掲示中・受注中 */
        put_contract(&s, CT_WYRM_HUNTER, 50, GD_STANDARD);
        CHECK_EQ(guild_accept(&s, 0), GUILD_OK);
        put_contract(&s, CT_WYRM_HUNTER, 50, GD_EASY);
        s.guild_board_count = 1;
        memset(&s.guild_board[1], 0, 2 * sizeof s.guild_board[1]); /* 掲示板の残りの枠を空に (読み込み後と同じ) */
        CHECK(game_validate(&s));
        char *t4 = save_text(&s);
        char *t3 = as_v3_build(t4);
        CHECK(!strncmp(t3, "LOOTSMITH_SAVE 3\n", 17));
        CHECK(strstr(t3, "\nga 4 10 50 1 55 2 3 3\n") != NULL || strstr(t3, "\nga ") != NULL);
        GameState l;
        CHECK_EQ(load_text(&l, t3), SAVE_OK);
        CHECK(same_state(&s, &l)); /* 通常契約: 種別 0・報酬なしで復元され、ほかは同一 */
        CHECK_EQ(l.guild_active[0].kind, GUILD_CONTRACT_STANDARD);
        CHECK_EQ(l.guild_active[0].reward_qty, 0);
        CHECK_EQ(l.guild_board[0].kind, GUILD_CONTRACT_STANDARD);
        int it = give_item(&l, EQ_WYRMCLEAVER, MAT_IRON, MAT_IRON);
        CHECK_EQ(guild_deliver(&l, 0, it, NULL), GUILD_OK);
        CHECK_EQ(l.mat_stock[MAT_DRAGON_SCALE], 0); /* 誤って付与されない */
        char *again = save_text(&l);
        CHECK(!strncmp(again, "LOOTSMITH_SAVE 4\n", 17)); /* 再保存は最新の形式 */
        GameState l2;
        CHECK_EQ(load_text(&l2, again), SAVE_OK);
        CHECK(same_state(&l, &l2));
        free(again);
        /* v3 の本文に v4 の 11 フィールドの行があっても、余分なフィールドは通常契約に戻される */
        free(t3);
        free(t4);
    }
    /* v3 の掲示板 / 受注中の契約を持つ状態 (Dragon Scale なし) が、Mythic 到達前でも読み込める */
    {
        GameState s = at_done(20);
        guild_accept(&s, 0);
        char *t4 = save_text(&s);
        char *t3 = as_v3_build(t4);
        free(t4);
        GameState l;
        CHECK_EQ(load_text(&l, t3), SAVE_OK);
        CHECK(same_state(&s, &l));
        CHECK_EQ(l.mat_stock[MAT_DRAGON_SCALE], 0);
        CHECK_EQ(l.price[MAT_DRAGON_SCALE], MATERIALS[MAT_DRAGON_SCALE].base_price);
        /* 古い保存から続けて遊び、50 件に達した後は Dragon Hunt が掲示されうる */
        l.guild_done = 50;
        l.guild_rng = 1;
        for (uint32_t k = 1; k < 100 && !hunts_on_board(&l); k++) {
            l.guild_rng = k;
            guild_refresh_board(&l);
        }
        CHECK(hunts_on_board(&l) == 1);
        CHECK(game_validate(&l));
        free(t3);
    }
    /* v3 のファイルに v4 の記録が混じった場合 (想定外) でも、報酬は付かない */
    {
        GameState s = at_done(50);
        put_contract(&s, CT_WYRM_HUNTER, 50, GD_STANDARD);
        char *t4 = save_text(&s);
        char *t3 = as_v3_build(t4);
        char *forged = replace_line(t3, "gb ", "gb 4 10 50 1 55 2 3 0 1 7 1"); /* v3 では末尾の 3 値は無視される */
        GameState l;
        CHECK_EQ(load_text(&l, forged), SAVE_OK);
        CHECK_EQ(l.guild_board[0].kind, GUILD_CONTRACT_STANDARD);
        CHECK_EQ(l.guild_board[0].reward_qty, 0);
        free(forged);
        free(t3);
        free(t4);
    }
    free(t);
}

/* =====================================================================================================
 * Dragonforging (第 3 弾コンテンツ): Dragon Scale を使った Masterwork / Legendary の特殊特性
 * ===================================================================================================== */
static int expected_pct(int uses) { /* 仕様の確率 (テーブルとは独立に書いた期待値) */
    return uses == 1 ? 20 : uses == 2 ? 30 : uses == 3 ? 35 : 0;
}

/* 「この craft_rng から始まるクラフトが、指定の品質・特殊特性判定になる」最初のシードを探す
 * (ゲームと同じ純粋関数で再現)。 hit: -1 = 判定なし (Dragon Scale を使わない)、0 = 外れ、1 = 当たり。pick:
 * 当たりのときの候補 (0 = Draconic Edge) */
static uint32_t find_craft_seed(int rank, int forge, int quality, int uses, int hit, int pick) {
    for (uint32_t s = 1; s < 3000000; s++) {
        uint32_t t = s;
        if (quality_from_roll(rank, forge, rng_range(&t, 0, 99)) != quality) {
            continue;
        }
        if (hit < 0) {
            return s;
        }
        int r = rng_range(&t, 0, 99);
        int h = r < expected_pct(uses);
        if (h != hit) {
            continue;
        }
        if (h && rng_range(&t, 0, 1) != pick) {
            continue;
        }
        return s;
    }
    printf("  FAIL find_craft_seed: no seed (q=%d uses=%d hit=%d pick=%d)\n", quality, uses, hit, pick);
    g_fails++;
    return 1;
}

static void test_dragon_trait_tables(void) {
    puts("dragon_trait_tables");
    CHECK(content_validate() == NULL);
    /* 既存の特性 ID は変わらない (新 ID は末尾に追記) */
    CHECK_EQ(EQ_TRAIT_NONE, 0);
    CHECK_EQ(EQ_TRAIT_KEEN, 1);
    CHECK_EQ(EQ_TRAIT_WARDING, 2);
    CHECK_EQ(EQ_TRAIT_ARCANE, 3);
    CHECK_EQ(EQ_TRAIT_STURDY, 4);
    CHECK_EQ(EQ_TRAIT_LIGHTWEIGHT, 5);
    CHECK_EQ(EQ_TRAIT_DRACONIC_EDGE, 6);
    CHECK_EQ(EQ_TRAIT_WYRMSCALE, 7);
    CHECK_EQ(EQ_TRAIT_COUNT, 8);
    CHECK_EQ(EQ_TRAIT_STANDARD_COUNT, 6);
    /* 名前・説明・効果 (Common / Fine は 0、Masterwork +3、Legendary +4) */
    const TraitDef *de = &TRAIT_DEFS[EQ_TRAIT_DRACONIC_EDGE], *ws = &TRAIT_DEFS[EQ_TRAIT_WYRMSCALE];
    CHECK(!strcmp(de->name, "Draconic Edge"));
    CHECK(!strcmp(de->desc, "Dragon-forged edge: increased ATK."));
    CHECK(!strcmp(ws->name, "Wyrmscale"));
    CHECK(!strcmp(ws->desc, "Wyrm scales reinforce the item: increased DUR."));
    CHECK(!strcmp(trait_name(EQ_TRAIT_DRACONIC_EDGE), "Draconic Edge"));
    CHECK(!strcmp(trait_name(EQ_TRAIT_WYRMSCALE), "Wyrmscale"));
    CHECK_EQ(de->stat, TS_ATK);
    CHECK_EQ(ws->stat, TS_DUR);
    CHECK_EQ(de->dragon, 1);
    CHECK_EQ(ws->dragon, 1);
    for (int t = 0; t < EQ_TRAIT_STANDARD_COUNT; t++) {
        CHECK_EQ(TRAIT_DEFS[t].dragon, 0);
    }
    int want[QUALITY_COUNT] = {0, 0, 3, 4};
    for (int q = 0; q < QUALITY_COUNT; q++) {
        CHECK_EQ(de->amount[q], want[q]);
        CHECK_EQ(ws->amount[q], want[q]);
    }
    char buf[24];
    trait_effect_text(EQ_TRAIT_DRACONIC_EDGE, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "+3 ATK"));
    trait_effect_text(EQ_TRAIT_DRACONIC_EDGE, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK(!strcmp(buf, "+4 ATK"));
    trait_effect_text(EQ_TRAIT_WYRMSCALE, QUALITY_MASTERWORK, buf, sizeof buf);
    CHECK(!strcmp(buf, "+3 DUR"));
    trait_effect_text(EQ_TRAIT_WYRMSCALE, QUALITY_LEGENDARY, buf, sizeof buf);
    CHECK(!strcmp(buf, "+4 DUR"));
    trait_effect_text(EQ_TRAIT_WYRMSCALE, QUALITY_COMMON, buf, sizeof buf);
    CHECK(!strcmp(buf, ""));
    trait_effect_text(EQ_TRAIT_DRACONIC_EDGE, QUALITY_FINE, buf, sizeof buf);
    CHECK(!strcmp(buf, ""));
    /* 抽選の表 */
    for (int u = 0; u <= DRAGON_SCALE_USE_MAX; u++) {
        CHECK_EQ(DRAGON_TRAIT_PCT[u], expected_pct(u));
        CHECK_EQ(dragon_trait_chance_pct(u), expected_pct(u));
    }
    CHECK_EQ(dragon_trait_chance_pct(-5), 0); /* 範囲外の使用数は丸める */
    CHECK_EQ(dragon_trait_chance_pct(99), 35);
    CHECK_EQ(DRAGON_TRAIT_IDS[0], EQ_TRAIT_DRACONIC_EDGE);
    CHECK_EQ(DRAGON_TRAIT_IDS[1], EQ_TRAIT_WYRMSCALE);
    CHECK(content_validate_dragon_forging(DRAGON_TRAIT_PCT, DRAGON_TRAIT_IDS) == NULL);

    /* 不正な特性定義は content_validate が検出する */
    TraitDef t[EQ_TRAIT_COUNT];
    memcpy(t, TRAIT_DEFS, sizeof t);
    CHECK(content_validate_traits(t) == NULL);
    t[EQ_TRAIT_DRACONIC_EDGE].amount[QUALITY_COMMON] = 1; /* Common に効果がある */
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].amount[QUALITY_FINE] = 2;
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].amount[QUALITY_LEGENDARY] = 0; /* Legendary に効果がない */
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_DRACONIC_EDGE].amount[QUALITY_MASTERWORK] = -3;
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_DRACONIC_EDGE].stat = 99;
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].name = TRAIT_DEFS[EQ_TRAIT_DRACONIC_EDGE].name; /* 名前の重複 */
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].desc = "";
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].dragon = 0; /* 特殊特性の印が外れている */
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_KEEN].dragon = 1; /* 通常特性が特殊扱い */
    CHECK(content_validate_traits(t) != NULL);
    memcpy(t, TRAIT_DEFS, sizeof t);
    t[EQ_TRAIT_WYRMSCALE].dragon = 2;
    CHECK(content_validate_traits(t) != NULL);
    /* 抽選表の不正 */
    int pct[DRAGON_SCALE_USE_MAX + 1], ids[DRAGON_TRAIT_KINDS];
    memcpy(pct, DRAGON_TRAIT_PCT, sizeof pct);
    memcpy(ids, DRAGON_TRAIT_IDS, sizeof ids);
    pct[0] = 5; /* Scale なしで確率がある */
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    memcpy(pct, DRAGON_TRAIT_PCT, sizeof pct);
    pct[2] = 101;
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    memcpy(pct, DRAGON_TRAIT_PCT, sizeof pct);
    pct[1] = -1;
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    memcpy(pct, DRAGON_TRAIT_PCT, sizeof pct);
    pct[3] = 25; /* 使用数が多いのに確率が下がる */
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    memcpy(pct, DRAGON_TRAIT_PCT, sizeof pct);
    pct[1] = 0;
    pct[2] = 0;
    pct[3] = 100; /* 0% と 100% の端は有効 */
    CHECK(content_validate_dragon_forging(pct, ids) == NULL);
    ids[1] = EQ_TRAIT_KEEN; /* 通常特性は候補にできない */
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    ids[1] = EQ_TRAIT_DRACONIC_EDGE; /* 重複 (Wyrmscale が候補に入らない) */
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    ids[1] = EQ_TRAIT_COUNT;
    CHECK(content_validate_dragon_forging(pct, ids) != NULL);
    CHECK(content_validate_dragon_forging(NULL, ids) != NULL);
}

static void test_dragon_trait_apply(void) {
    puts("dragon_trait_apply");
    static const struct {
        int trait, q, atk, dur;
    } cases[] = {{EQ_TRAIT_DRACONIC_EDGE, QUALITY_MASTERWORK, 3, 0},
                 {EQ_TRAIT_DRACONIC_EDGE, QUALITY_LEGENDARY, 4, 0},
                 {EQ_TRAIT_WYRMSCALE, QUALITY_MASTERWORK, 0, 3},
                 {EQ_TRAIT_WYRMSCALE, QUALITY_LEGENDARY, 0, 4}};
    static const int recipes[][2] = {
        {MAT_DRAGON_SCALE, MAT_IRON}, {MAT_IRON, MAT_DRAGON_SCALE}, {MAT_DRAGON_SCALE, MAT_DRAGON_SCALE}};
    for (int forge = 0; forge <= 2; forge++) {
        for (unsigned r = 0; r < sizeof recipes / sizeof *recipes; r++) {
            for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
                Equipment base;
                memset(&base, 0, sizeof base);
                base.type = EQ_WYRMCLEAVER;
                base.main = recipes[r][0];
                base.sub = recipes[r][1];
                equipment_compute_stats(&base, forge);
                Equipment x = base;
                CHECK_EQ(equipment_apply_quality(&x, cases[c].q, cases[c].trait), 1);
                int mult = QUALITY_DEFS[cases[c].q].mult_pm;
                /* 品質倍率の後で、特性を 1 回だけ足す。能力値の残りは倍率だけ */
                CHECK_EQ(x.atk, qmul(base.atk, mult) + cases[c].atk);
                CHECK_EQ(x.dur, qmul(base.dur, mult) + cases[c].dur);
                CHECK_EQ(x.def, qmul(base.def, mult));
                CHECK_EQ(x.mag, qmul(base.mag, mult));
                CHECK_EQ(x.wt, base.wt);
                CHECK_EQ(x.quality, cases[c].q);
                CHECK_EQ(x.trait, cases[c].trait);
                /* 再適用は拒否され、何も累積しない */
                Equipment again = x;
                CHECK_EQ(equipment_apply_quality(&again, cases[c].q, cases[c].trait), 0);
                CHECK(!memcmp(&again, &x, sizeof x));
                /* 作り直しても同じ (累積しない) */
                Equipment rebuilt = x;
                equipment_build(&rebuilt, forge, cases[c].q, cases[c].trait);
                CHECK_EQ(rebuilt.atk, x.atk);
                CHECK_EQ(rebuilt.dur, x.dur);
                equipment_build(&rebuilt, forge, cases[c].q, cases[c].trait);
                CHECK_EQ(rebuilt.atk, x.atk);
                CHECK_EQ(rebuilt.dur, x.dur);
            }
        }
    }
    /* Common / Fine には特殊特性が付かない (どちらも、特性ありの品質ではない) */
    for (int t = EQ_TRAIT_DRACONIC_EDGE; t <= EQ_TRAIT_WYRMSCALE; t++) {
        for (int q = QUALITY_COMMON; q <= QUALITY_FINE; q++) {
            Equipment x;
            memset(&x, 0, sizeof x);
            x.type = EQ_WYRMCLEAVER;
            x.main = MAT_DRAGON_SCALE;
            x.sub = MAT_DRAGON_SCALE;
            equipment_compute_stats(&x, 0);
            Equipment before = x;
            CHECK_EQ(equipment_apply_quality(&x, q, t), 0);
            CHECK(!memcmp(&x, &before, sizeof x));
            equipment_build(&x, 0, q, t); /* 不正な組み合わせは無印の Common になる */
            CHECK_EQ(x.quality, QUALITY_COMMON);
            CHECK_EQ(x.trait, EQ_TRAIT_NONE);
        }
        /* Masterwork / Legendary でも、特性なしは不正 (既存の規則のまま) */
        Equipment y;
        memset(&y, 0, sizeof y);
        y.main = y.sub = MAT_DRAGON_SCALE;
        equipment_compute_stats(&y, 0);
        CHECK_EQ(equipment_apply_quality(&y, QUALITY_MASTERWORK, EQ_TRAIT_NONE), 0);
    }
    /* Dragon Scale を使わない装備に、特殊特性は付けられない */
    for (int t = EQ_TRAIT_DRACONIC_EDGE; t <= EQ_TRAIT_WYRMSCALE; t++) {
        for (int q = QUALITY_MASTERWORK; q <= QUALITY_LEGENDARY; q++) {
            Equipment x;
            memset(&x, 0, sizeof x);
            x.type = EQ_SWORD;
            x.main = MAT_STEEL;
            x.sub = MAT_OBSIDIAN;
            equipment_compute_stats(&x, 1);
            Equipment before = x;
            CHECK_EQ(equipment_apply_quality(&x, q, t), 0);
            CHECK(!memcmp(&x, &before, sizeof x));
            equipment_build(&x, 1, q, t);
            CHECK_EQ(x.quality, QUALITY_COMMON);
            CHECK_EQ(x.trait, EQ_TRAIT_NONE);
        }
    }
    /* 範囲外の特性 ID は拒否 */
    Equipment bad;
    memset(&bad, 0, sizeof bad);
    bad.main = MAT_DRAGON_SCALE;
    equipment_compute_stats(&bad, 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_MASTERWORK, EQ_TRAIT_COUNT), 0);
    CHECK_EQ(equipment_apply_quality(&bad, QUALITY_MASTERWORK, -1), 0);
    /* 通常特性は Dragon Scale を使っても従来どおり付く */
    Equipment k;
    memset(&k, 0, sizeof k);
    k.main = MAT_DRAGON_SCALE;
    k.sub = MAT_IRON;
    equipment_compute_stats(&k, 0);
    CHECK_EQ(equipment_apply_quality(&k, QUALITY_MASTERWORK, EQ_TRAIT_KEEN), 1);
}

static void test_dragon_trait_roll(void) {
    puts("dragon_trait_roll");
    /* Dragon Scale の使用数: 主 2、副 1、両方で 3 */
    CHECK_EQ(dragon_scale_uses(MAT_IRON, MAT_IRON), 0);
    CHECK_EQ(dragon_scale_uses(MAT_DRAGON_SCALE, MAT_IRON), 2);
    CHECK_EQ(dragon_scale_uses(MAT_IRON, MAT_DRAGON_SCALE), 1);
    CHECK_EQ(dragon_scale_uses(MAT_DRAGON_SCALE, MAT_DRAGON_SCALE), 3);
    CHECK_EQ(dragon_scale_uses(-1, MAT_COUNT + 3), 0);
    /* 確率の境界: 使用数 1/2/3 は roll < 20/30/35 */
    static const int pct[4] = {0, 20, 30, 35};
    for (int u = 0; u <= 3; u++) {
        int hits = 0;
        for (int roll = 0; roll < 100; roll++) {
            int h = dragon_trait_hits(u, roll);
            CHECK_EQ(h, roll < pct[u]);
            hits += h;
        }
        CHECK_EQ(hits, pct[u]); /* 100 通りのロールのうち、ちょうど pct 通りが当たり */
        if (pct[u] > 0) {
            CHECK(dragon_trait_hits(u, pct[u] - 1));
            CHECK(!dragon_trait_hits(u, pct[u]));
        }
        CHECK(dragon_trait_hits(u, -1) == (pct[u] > 0)); /* 範囲外は 0..99 に丸める */
        CHECK(!dragon_trait_hits(u, 100));
        CHECK(!dragon_trait_hits(u, 1000000));
    }
    CHECK(!dragon_trait_hits(0, 0)); /* Dragon Scale なしは、どのロールでも当たらない */
    /* 0% / 100% 相当 */
    for (int roll = -3; roll <= 103; roll++) {
        CHECK(!chance_roll_hits(0, roll));
        CHECK(!chance_roll_hits(-5, roll));
        CHECK(chance_roll_hits(100, roll)); /* 100% 以上は、どのロール (範囲外は丸める) でも必ず当たる */
        CHECK(chance_roll_hits(101, roll));
    }
    CHECK(chance_roll_hits(100, 99)); /* 100% は最大のロール (99) でも当たり */
    CHECK(chance_roll_hits(100, 0));
    CHECK(!chance_roll_hits(1, 1));
    CHECK(chance_roll_hits(1, 0));
    CHECK(chance_roll_hits(50, 49));
    CHECK(!chance_roll_hits(50, 50));
    /* 特殊特性が選ばれたときの内訳は均等 (2 択) */
    CHECK_EQ(dragon_trait_from_roll(0), EQ_TRAIT_DRACONIC_EDGE);
    CHECK_EQ(dragon_trait_from_roll(1), EQ_TRAIT_WYRMSCALE);
    CHECK_EQ(dragon_trait_from_roll(-4), EQ_TRAIT_DRACONIC_EDGE);
    CHECK_EQ(dragon_trait_from_roll(9), EQ_TRAIT_WYRMSCALE);
    /* 外れたときは、従来どおりの通常特性 (5 択) だけ。新特性は決して出ない */
    int seen[EQ_TRAIT_COUNT] = {0};
    for (int roll = -2; roll < 20; roll++) {
        int t = trait_from_roll(roll);
        CHECK(t >= EQ_TRAIT_KEEN && t <= EQ_TRAIT_LIGHTWEIGHT);
        seen[t]++;
    }
    CHECK_EQ(seen[EQ_TRAIT_DRACONIC_EDGE] + seen[EQ_TRAIT_WYRMSCALE], 0);
}

/* craft_roll_trait: 乱数の消費量と結果を、仕様の手順どおりに再現して照合する */
static void test_dragon_trait_stream(void) {
    puts("dragon_trait_stream");
    static const int recipes[][2] = {{MAT_IRON, MAT_IRON},
                                     {MAT_STEEL, MAT_OBSIDIAN},
                                     {MAT_DRAGON_SCALE, MAT_IRON},
                                     {MAT_IRON, MAT_DRAGON_SCALE},
                                     {MAT_DRAGON_SCALE, MAT_DRAGON_SCALE}};
    int dragon_seen = 0, standard_with_scale = 0;
    for (uint32_t s = 1; s <= 4000; s++) {
        for (unsigned r = 0; r < sizeof recipes / sizeof *recipes; r++) {
            int main = recipes[r][0], sub = recipes[r][1];
            int uses = (main == MAT_DRAGON_SCALE ? 2 : 0) + (sub == MAT_DRAGON_SCALE ? 1 : 0);
            for (int q = 0; q < QUALITY_COUNT; q++) {
                uint32_t a = s, e = s;
                int got = craft_roll_trait(&a, q, main, sub);
                int want;
                if (!QUALITY_DEFS[q].has_trait) {
                    want = EQ_TRAIT_NONE; /* 特性のない品質: 何も引かない (Dragon Scale を使っていても) */
                } else if (uses > 0 && rng_range(&e, 0, 99) < expected_pct(uses)) {
                    want = rng_range(&e, 0, 1) == 0 ? EQ_TRAIT_DRACONIC_EDGE : EQ_TRAIT_WYRMSCALE;
                } else {
                    want = 1 + rng_range(&e, 0, 4); /* 従来の抽選: 範囲 0..4 の 1 回、ID 1..5 */
                }
                CHECK_EQ(got, want);
                CHECK(a == e); /* 消費した回数まで同じ */
                if (!uses && QUALITY_DEFS[q].has_trait) {
                    CHECK(got >= EQ_TRAIT_KEEN && got <= EQ_TRAIT_LIGHTWEIGHT); /* 通常素材では新特性が絶対に出ない */
                }
                if (uses && QUALITY_DEFS[q].has_trait) {
                    dragon_seen += got >= EQ_TRAIT_STANDARD_COUNT;
                    standard_with_scale += got < EQ_TRAIT_STANDARD_COUNT;
                }
            }
        }
    }
    CHECK(dragon_seen > 0 && standard_with_scale > 0);
    /* 範囲外の品質は何もしない */
    uint32_t z = 77;
    CHECK_EQ(craft_roll_trait(&z, -1, MAT_DRAGON_SCALE, MAT_IRON), EQ_TRAIT_NONE);
    CHECK_EQ(craft_roll_trait(&z, QUALITY_COUNT, MAT_DRAGON_SCALE, MAT_IRON), EQ_TRAIT_NONE);
    CHECK(z == 77);

    /* 境界の乱数 (ちょうど pct - 1 が当たり、pct が外れ) を実際のクラフト抽選で確認する */
    for (int uses = 1; uses <= 3; uses++) {
        int main = uses == 1 ? MAT_IRON : MAT_DRAGON_SCALE, sub = uses == 3   ? MAT_DRAGON_SCALE
                                                                  : uses == 1 ? MAT_DRAGON_SCALE
                                                                              : MAT_IRON;
        CHECK_EQ(dragon_scale_uses(main, sub), uses);
        int hit_at = 0, miss_at = 0;
        for (uint32_t s = 1; s < 400000 && !(hit_at && miss_at); s++) {
            uint32_t t = s;
            int roll = rng_range(&t, 0, 99);
            if (roll == expected_pct(uses) - 1 && !hit_at) {
                uint32_t a = s;
                int tr = craft_roll_trait(&a, QUALITY_MASTERWORK, main, sub);
                CHECK(tr >= EQ_TRAIT_STANDARD_COUNT);
                hit_at = 1;
            }
            if (roll == expected_pct(uses) && !miss_at) {
                uint32_t a = s;
                int tr = craft_roll_trait(&a, QUALITY_LEGENDARY, main, sub);
                CHECK(tr < EQ_TRAIT_STANDARD_COUNT && tr > EQ_TRAIT_NONE);
                miss_at = 1;
            }
        }
        CHECK(hit_at && miss_at);
    }
    /* 十分な標本での割合 (固定シード列: 決定的)。使用数が多いほど高く、特殊特性の 2 種は同程度 */
    int cnt[4] = {0}, tot[4] = {0}, de = 0, ws = 0;
    for (uint32_t s = 1; s <= 60000; s++) {
        for (int uses = 1; uses <= 3; uses++) {
            int main = uses == 1 ? MAT_IRON : MAT_DRAGON_SCALE, sub = uses == 3   ? MAT_DRAGON_SCALE
                                                                      : uses == 1 ? MAT_DRAGON_SCALE
                                                                                  : MAT_IRON;
            uint32_t a = s;
            int tr = craft_roll_trait(&a, QUALITY_MASTERWORK, main, sub);
            tot[uses]++;
            if (tr >= EQ_TRAIT_STANDARD_COUNT) {
                cnt[uses]++;
                de += tr == EQ_TRAIT_DRACONIC_EDGE;
                ws += tr == EQ_TRAIT_WYRMSCALE;
            }
        }
    }
    for (int uses = 1; uses <= 3; uses++) {
        double f = 100.0 * cnt[uses] / tot[uses];
        printf("  scales %d: dragon trait %.1f%% (target %d%%)\n", uses, f, expected_pct(uses));
        CHECK(f > expected_pct(uses) - 1.5 && f < expected_pct(uses) + 1.5);
    }
    CHECK(cnt[1] < cnt[2] && cnt[2] < cnt[3]);
    CHECK(de + ws == cnt[1] + cnt[2] + cnt[3]);
    CHECK(de * 100 > (de + ws) * 48 && de * 100 < (de + ws) * 52); /* 50% ずつ */
}

static void test_dragon_trait_craft(void) {
    puts("dragon_trait_craft");
    for (int forge = 0; forge <= 2; forge++) {
        for (int pick = 0; pick <= 1; pick++) {
            for (int q = QUALITY_MASTERWORK; q <= QUALITY_LEGENDARY; q++) {
                for (int recipe = 0; recipe < 3; recipe++) {
                    int main = recipe == 1 ? MAT_IRON : MAT_DRAGON_SCALE;
                    int sub = recipe == 0 ? MAT_IRON : MAT_DRAGON_SCALE;
                    int uses = dragon_scale_uses(main, sub);
                    GameState g = at_done(50);
                    g.up_level[UP_FORGE] = forge;
                    g.mat_stock[main] += 3;
                    g.mat_stock[sub] += 3;
                    g.mat_cost[MAT_IRON] = g.mat_stock[MAT_IRON] ? 30 : 0;
                    g.craft_rng = find_craft_seed(RANK_MYTHIC, forge, q, uses, 1, pick);
                    GameState before = g;
                    int idx = -1;
                    CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, main, sub, &idx), R_OK);
                    const Equipment *e = &g.inv[idx];
                    int trait = pick == 0 ? EQ_TRAIT_DRACONIC_EDGE : EQ_TRAIT_WYRMSCALE;
                    CHECK_EQ(e->quality, q);
                    CHECK_EQ(e->trait, trait);
                    /* 能力値: 基本 -> 品質倍率 -> 特性 (1 回) */
                    Equipment base;
                    memset(&base, 0, sizeof base);
                    base.type = EQ_WYRMCLEAVER;
                    base.main = main;
                    base.sub = sub;
                    equipment_compute_stats(&base, forge);
                    int mult = QUALITY_DEFS[q].mult_pm, amt = q == QUALITY_MASTERWORK ? 3 : 4;
                    CHECK_EQ(e->atk, qmul(base.atk, mult) + (pick == 0 ? amt : 0));
                    CHECK_EQ(e->dur, qmul(base.dur, mult) + (pick == 1 ? amt : 0));
                    /* FIT と推奨価格は、特性を含んだ最終能力から求まる (特性ぶんを除いた同じ装備より高い) */
                    Equipment without = *e;
                    if (pick == 0) {
                        without.atk -= amt;
                    } else {
                        without.dur -= amt;
                    }
                    equipment_price(&without);
                    CHECK(equip_fit(CT_WYRM_HUNTER, e) >= equip_fit(CT_WYRM_HUNTER, &without));
                    CHECK(e->value > without.value);
                    CHECK(e->rec_price >= without.rec_price);
                    Equipment chk = *e;
                    equipment_price(&chk);
                    CHECK_EQ(e->value, chk.value);
                    CHECK_EQ(e->rec_price, chk.rec_price);
                    CHECK_EQ(e->price, e->rec_price);
                    /* 他のストリームは使わない。craft_rng は品質 1 回 + 判定 1 回 + 選択 1 回 */
                    CHECK(g.rng == before.rng && g.guild_rng == before.guild_rng);
                    uint32_t t = before.craft_rng;
                    rng_range(&t, 0, 99);
                    rng_range(&t, 0, 99);
                    rng_range(&t, 0, 1);
                    CHECK(g.craft_rng == t);
                    CHECK(game_validate(&g));
                }
            }
        }
    }
    /* 判定に外れたら通常特性 (Dragon Scale を使っても): 消費は品質 + 判定 + 通常抽選 */
    {
        GameState g = at_done(50);
        g.mat_stock[MAT_DRAGON_SCALE] = 3;
        g.craft_rng = find_craft_seed(RANK_MYTHIC, 0, QUALITY_LEGENDARY, 3, 0, 0);
        uint32_t t = g.craft_rng;
        rng_range(&t, 0, 99);
        rng_range(&t, 0, 99);
        int want = 1 + rng_range(&t, 0, 4);
        int idx = -1;
        CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, &idx), R_OK);
        CHECK_EQ(g.inv[idx].trait, want);
        CHECK(g.inv[idx].trait < EQ_TRAIT_STANDARD_COUNT);
        CHECK(g.craft_rng == t);
    }
    /* Common / Fine: Dragon Scale を使っても特性なしで、消費は品質の 1 回だけ (従来と同じ) */
    for (int q = QUALITY_COMMON; q <= QUALITY_FINE; q++) {
        GameState g = at_done(50);
        g.mat_stock[MAT_DRAGON_SCALE] = 3;
        g.craft_rng = find_craft_seed(RANK_MYTHIC, 0, q, 3, -1, 0);
        uint32_t t = g.craft_rng;
        rng_range(&t, 0, 99);
        int idx = -1;
        CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, &idx), R_OK);
        CHECK_EQ(g.inv[idx].quality, q);
        CHECK_EQ(g.inv[idx].trait, EQ_TRAIT_NONE);
        CHECK(g.craft_rng == t);
    }
    /* Dragon Scale を使わないクラフト: 従来と完全に同じ消費・結果 (品質 + 通常特性の 1 回)。新特性は出ない */
    {
        int n = 0;
        for (uint32_t s = 1; s <= 3000; s++) {
            GameState g = at_done(50);
            g.up_level[UP_FORGE] = 2;
            g.mat_stock[MAT_STEEL] = 3;
            g.mat_cost[MAT_STEEL] = 36;
            g.craft_rng = s;
            uint32_t t = s;
            int q = quality_from_roll(RANK_MYTHIC, 2, rng_range(&t, 0, 99));
            int want = QUALITY_DEFS[q].has_trait ? 1 + rng_range(&t, 0, 4) : EQ_TRAIT_NONE;
            int idx = -1;
            CHECK_EQ(craft_equipment(&g, EQ_SWORD, MAT_STEEL, MAT_STEEL, &idx), R_OK);
            CHECK_EQ(g.inv[idx].trait, want);
            CHECK(g.inv[idx].trait < EQ_TRAIT_STANDARD_COUNT);
            CHECK(g.craft_rng == t);
            n += want != 0;
        }
        CHECK(n > 0);
    }
    /* 高品質装備が過度に安価 / 高価にならない: 特殊特性の装備の価値は、同じ素材・品質の通常特性の装備に近い */
    for (int q = QUALITY_MASTERWORK; q <= QUALITY_LEGENDARY; q++) {
        Equipment d, k;
        memset(&d, 0, sizeof d);
        d.type = EQ_WYRMCLEAVER;
        d.main = MAT_DRAGON_SCALE;
        d.sub = MAT_IRON;
        d.cost = 10;
        k = d;
        equipment_build(&d, 1, q, EQ_TRAIT_DRACONIC_EDGE);
        equipment_build(&k, 1, q, EQ_TRAIT_KEEN);
        equipment_price(&d);
        equipment_price(&k);
        CHECK(d.value >= k.value); /* 効果が +1 大きいぶん、わずかに高い */
        CHECK(d.value * 100 <= k.value * 108);
    }
}

static void test_dragon_trait_save(void) {
    puts("dragon_trait_save");
    for (int pick = 0; pick <= 1; pick++) {
        for (int q = QUALITY_MASTERWORK; q <= QUALITY_LEGENDARY; q++) {
            GameState g = at_done(50);
            g.up_level[UP_FORGE] = 1;
            g.mat_stock[MAT_DRAGON_SCALE] = 3;
            g.craft_rng = find_craft_seed(RANK_MYTHIC, 1, q, 3, 1, pick);
            int idx = -1;
            CHECK_EQ(craft_equipment(&g, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_DRAGON_SCALE, &idx), R_OK);
            Equipment e = g.inv[idx];
            char *t = save_text(&g);
            GameState r;
            CHECK_EQ(load_text(&r, t), SAVE_OK);
            CHECK(same_state(&g, &r));
            CHECK_EQ(r.inv[0].trait, pick == 0 ? EQ_TRAIT_DRACONIC_EDGE : EQ_TRAIT_WYRMSCALE);
            CHECK_EQ(r.inv[0].atk, e.atk);
            CHECK_EQ(r.inv[0].dur, e.dur);
            CHECK_EQ(r.inv[0].value, e.value);
            char fx[24];
            trait_effect_text(r.inv[0].trait, r.inv[0].quality, fx, sizeof fx);
            CHECK(!strcmp(fx, q == QUALITY_MASTERWORK ? (pick == 0 ? "+3 ATK" : "+3 DUR")
                                                      : (pick == 0 ? "+4 ATK" : "+4 DUR")));
            CHECK(!strcmp(trait_name(r.inv[0].trait), pick == 0 ? "Draconic Edge" : "Wyrmscale"));
            char *t2 = save_text(&r);
            CHECK(!strcmp(t, t2)); /* 保存形式は変わらない: 既存の item 行に特性 ID が入るだけ */
            CHECK(!strncmp(t, "LOOTSMITH_SAVE 4\n", 17));
            free(t2);
            /* Dragon Scale を使っていない装備に特殊特性があるセーブは拒否される (読み込み先は変わらない) */
            GameState bad = g;
            bad.inv[0].main = MAT_IRON;
            bad.inv[0].sub = MAT_IRON;
            CHECK(!game_validate(&bad));
            char *tb = save_text(&bad);
            GameState probe = fresh(), keep = probe;
            CHECK_EQ(load_text(&probe, tb), SAVE_ERR_VALUE);
            CHECK(same_state(&probe, &keep));
            free(tb);
            /* 副素材だけが Dragon Scale でも有効 */
            GameState sub_only = g;
            sub_only.inv[0].main = MAT_IRON;
            CHECK(game_validate(&sub_only));
            /* Common / Fine に特殊特性は不正 */
            GameState low = g;
            low.inv[0].quality = QUALITY_FINE;
            CHECK(!game_validate(&low));
            /* 範囲外の特性 ID は不正 */
            GameState oob = g;
            oob.inv[0].trait = EQ_TRAIT_COUNT;
            CHECK(!game_validate(&oob));
            free(t);
        }
    }
    /* 旧セーブ (通常特性の装備) は変わらず読み込める: v3 形式の本文から */
    {
        GameState s = at_done(30);
        put_built(&s, EQ_SWORD, MAT_IRON, MAT_IRON, QUALITY_LEGENDARY, EQ_TRAIT_LIGHTWEIGHT);
        char *t4 = save_text(&s);
        char *t3 = as_v3_build(t4);
        GameState l;
        CHECK_EQ(load_text(&l, t3), SAVE_OK);
        CHECK(same_state(&s, &l));
        CHECK_EQ(l.inv[0].trait, EQ_TRAIT_LIGHTWEIGHT);
        free(t3);
        free(t4);
    }
}

static void test_dragon_streams(void) {
    puts("dragon_streams");
    /* Dragon Hunt の抽選は guild_rng だけを使う。rng と craft_rng は他の操作の影響を受けない */
    GameState a = at_done(50), b = at_done(50);
    a.status = b.status = ST_SANDBOX;
    for (int d = 0; d < 40; d++) {
        pass_day(&a);
        a.money = 100000;
        a.guild_active_count = 0;
        memset(a.guild_active, 0, sizeof a.guild_active);
    }
    /* b は同じ日数を、ギルドの掲示板を別の乱数で更新しながら進める: 市場と客の乱数列は同一でなければならない */
    for (int d = 0; d < 40; d++) {
        b.guild_rng ^= 0x5A5A5A5Au; /* ギルドのストリームだけを乱す */
        if (!b.guild_rng) {
            b.guild_rng = 1;
        }
        pass_day(&b);
        b.money = 100000;
        b.guild_active_count = 0;
        memset(b.guild_active, 0, sizeof b.guild_active);
    }
    CHECK(a.rng == b.rng); /* 市場・客 */
    CHECK(a.craft_rng == b.craft_rng);
    for (int m = 0; m < MAT_COUNT; m++) {
        CHECK_EQ(a.price[m], b.price[m]);
    }
    CHECK(a.guild_rng != b.guild_rng);
    /* 新素材でのクラフトも craft_rng だけを消費する */
    GameState c = at_done(50);
    c.mat_stock[MAT_DRAGON_SCALE] = 3;
    c.mat_stock[MAT_IRON] = 3;
    c.mat_cost[MAT_IRON] = 30;
    uint32_t r0 = c.rng, g0 = c.guild_rng, k0 = c.craft_rng;
    CHECK_EQ(craft_equipment(&c, EQ_WYRMCLEAVER, MAT_DRAGON_SCALE, MAT_IRON, NULL), R_OK);
    CHECK(c.rng == r0 && c.guild_rng == g0 && c.craft_rng != k0);
}

int main(void) {
    test_init();
    test_market_buy();
    test_craft();
    test_pricing();
    test_judge();
    test_sale_flow();
    test_settle();
    test_upgrades2();
    test_save_load();
    test_save_fail_field();
    test_content_tables();
    test_icons();
    test_save_legacy_and_migration();
    test_content_pack1();
    test_content_pack1_flow();
    test_save_before_pack1();
    test_guild_generation();
    test_guild_accept();
    test_guild_deliver();
    test_guild_deadline();
    test_guild_save();
    test_guild_validate();
    test_rank_tables();
    test_rank_derivation();
    test_craft_lock();
    test_rank_customers();
    test_rank_guild();
    test_rank_save();
    test_mythic_smith();
    test_fail_consistency();
#ifndef _WIN32
    test_save_file_safety();
#endif
    test_quality_tables();
    test_quality_rates();
    test_quality_from_roll();
    test_quality_stats();
    test_quality_value_and_fit();
    test_rng_streams();
    test_craft_quality_flow();
    test_quality_inventory();
    test_quality_validate();
    test_quality_save();
    test_review_validate_limits();
    test_review_saturation();
    test_review_calendar();
    test_review_parsing();
    test_review_seed();
    test_review2_guild_ids();
    test_review2_sandbox_continue();
    test_overflow_contract_deadline();
    test_overflow_rng_range();
    test_save_duplicate_records();
    test_campaign_end();
    test_dragon_tables();
    test_dragon_spawn_rule();
    test_dragon_board();
    test_dragon_deliver();
    test_dragon_market();
    test_dragon_craft();
    test_dragon_rarity();
    test_dragon_save();
    test_dragon_streams();
    test_dragon_trait_tables();
    test_dragon_trait_apply();
    test_dragon_trait_roll();
    test_dragon_trait_stream();
    test_dragon_trait_craft();
    test_dragon_trait_save();
    test_bots();
    printf("\n%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
