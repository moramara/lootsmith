#include "bot.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int plan[2][2];
    int target;
} EquipPlan;

/* ベース装備から設計図装備まで、解放状況に応じて仕入れ・製作する戦略。 */
static const EquipPlan PLANS[EQ_COUNT] = {
    [EQ_SWORD] = {{{MAT_IRON, MAT_IRON}, {MAT_WOOD, MAT_IRON}}, 2},
    [EQ_SHIELD] = {{{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}}, 1},
    [EQ_STAFF] = {{{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}}, 1},
    [EQ_DAGGER] = {{{MAT_IRON, MAT_WOOD}, {MAT_IRON, MAT_IRON}}, 1},
    [EQ_SPEAR] = {{{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_WOOD}}, 1},
    [EQ_GRIMOIRE] = {{{MAT_MANA, MAT_WOOD}, {MAT_MANA, MAT_IRON}}, 1},
    [EQ_WARHAMMER] = {{{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_COPPER}}, 1},
    [EQ_RUNE_BOW] = {{{MAT_OBSIDIAN, MAT_COPPER}, {MAT_IRON, MAT_WOOD}}, 1},
    [EQ_CRYSTAL_ORB] = {{{MAT_SILVER, MAT_SILVER}, {MAT_SILVER, MAT_WOOD}}, 1},
    [EQ_WYRMCLEAVER] = {{{MAT_IRON, MAT_IRON}, {MAT_IRON, MAT_COPPER}}, 1},
};

typedef struct {
    int buys;
    int crafts;
    int upgrades;
    int guild_accepted;
    int guild_delivered;
    int hunt_delivered; /* Dragon Hunt の納品数 */
    int scales;         /* 報酬として受け取った素材の数 */
} BotDayStats;

static void log_action(BotRunner *bot, const char *fmt, ...) {
    for (int i = BOT_LOG_COUNT - 1; i > 0; --i) {
        memcpy(bot->log[i], bot->log[i - 1], BOT_LOG_LEN);
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(bot->log[0], sizeof bot->log[0], fmt, args);
    va_end(args);
    bot->log[BOT_LOG_COUNT - 1][BOT_LOG_LEN - 1] = '\0';
    if (bot->log_count < BOT_LOG_COUNT) {
        bot->log_count++;
    }
}

static Result bot_buy(GameState *g, int mat, int qty, BotDayStats *stats) {
    Result r = market_buy(g, mat, qty);
    if (r == R_OK && stats) {
        stats->buys++;
    }
    return r;
}

static int pick_item(const GameState *g, const Customer *c, int skip_mask) {
    int best = -1, best_fit = -1;
    int wanted = CUSTOMER_DEFS[c->type].wants_eq;
    for (int i = 0; i < g->inv_count; ++i) {
        if (skip_mask & (1 << i)) {
            continue;
        }
        if (g->inv[i].type != wanted) {
            continue;
        }
        int fit = equip_fit(c->type, &g->inv[i]);
        if (fit > best_fit) {
            best = i;
            best_fit = fit;
        }
    }
    return best;
}

/* 報酬素材を受け取る在庫枠があるか。契約に報酬がなければ常に空きあり (guild_deliver が使うのと同じ条件)。 */
static int reward_slot_ok(const GameState *g, const GuildContract *k) {
    if (k->kind != GUILD_CONTRACT_DRAGON_HUNT) {
        return 1;
    }
    return k->reward_mat >= 0 && k->reward_mat < MAT_COUNT && k->reward_qty >= 1 &&
           g->mat_stock[k->reward_mat] + k->reward_qty <= BAL.stock_cap;
}

/* 契約要求を満たす最安レシピを探す。能力値の評価には Common 品の基本能力を使う。
 * 市場で買えない素材 (hunt_only) は仕入れられないので、レシピには使わない (貴重な報酬素材を契約の材料に浪費しない)。
 * 最低 FIT は通常の equip_fit() で判定する (Dragon Hunt 専用の計算はない)。 */
static int guild_recipe(const GameState *g, int customer_type, int min_fit, int *main, int *sub, int *cost) {
    int found = 0, best_cost = 1 << 30;
    int eq = CUSTOMER_DEFS[customer_type].wants_eq;
    for (int m = 0; m < MAT_COUNT; ++m) {
        for (int s = 0; s < MAT_COUNT; ++s) {
            if (MATERIALS[m].hunt_only || MATERIALS[s].hunt_only) {
                continue;
            }
            Equipment e;
            memset(&e, 0, sizeof e);
            e.type = eq;
            e.main = m;
            e.sub = s;
            equipment_compute_stats(&e, g->up_level[UP_FORGE]);
            if (equip_fit(customer_type, &e) < min_fit) {
                continue;
            }
            int c = 2 * MATERIALS[m].base_price + MATERIALS[s].base_price;
            if (c < best_cost) {
                best_cost = c;
                *main = m;
                *sub = s;
                *cost = c;
                found = 1;
            }
        }
    }
    return found;
}

static int ensure_recipe_materials(GameState *g, int main, int sub, BotDayStats *stats) {
    int need[MAT_COUNT] = {0};
    need[main] += 2;
    need[sub] += 1;
    for (int m = 0; m < MAT_COUNT; ++m) {
        if (need[m] > g->mat_stock[m] &&
            bot_buy(g, m, need[m] - g->mat_stock[m], stats) != R_OK) {
            return 0;
        }
    }
    return 1;
}

/* 費用に余裕のある契約を優先し、納品可能ならその日の準備中に納品する。 */
static void bot_guild_turn(GameState *g, BotDayStats *stats) {
    while (g->guild_active_count < GUILD_ACTIVE_MAX && g->guild_board_count > 0 &&
           g->inv_count < MAX_INVENTORY - 1) {
        int pick = -1, pick_gold = 0, m, s, cost;
        for (int i = 0; i < g->guild_board_count; ++i) {
            const GuildContract *k = &g->guild_board[i];
            if (k->gold <= pick_gold || !reward_slot_ok(g, k) ||
                !guild_recipe(g, k->customer_type, k->min_fit, &m, &s, &cost)) {
                continue; /* 受け取れない報酬・満たせない最低 FIT の契約は受注しない */
            }
            if (cost * 10 > k->gold * 8 || g->money < cost + 40) {
                continue;
            }
            pick = i;
            pick_gold = k->gold;
        }
        if (pick < 0 || guild_accept(g, pick) != GUILD_OK) {
            break;
        }
        stats->guild_accepted++;
    }

    for (int active = g->guild_active_count - 1; active >= 0; --active) {
        const GuildContract contract = g->guild_active[active];
        if (!reward_slot_ok(g, &contract)) {
            continue; /* 報酬を受け取れない間は、装備を作っても納品しても無駄になる */
        }
        int item = -1, least_fit = 101;
        for (int i = 0; i < g->inv_count; ++i) {
            int fit = guild_item_fit(g, active, i);
            if (fit >= contract.min_fit && fit < least_fit) {
                item = i;
                least_fit = fit;
            }
        }
        if (item < 0) {
            int m, s, cost;
            if (!guild_recipe(g, contract.customer_type, contract.min_fit, &m, &s, &cost) ||
                g->inv_count >= MAX_INVENTORY || !ensure_recipe_materials(g, m, s, stats)) {
                continue;
            }
            if (craft_equipment(g, CUSTOMER_DEFS[contract.customer_type].wants_eq, m, s, &item) != R_OK) {
                continue;
            }
            stats->crafts++;
        }
        GuildOutcome outcome;
        if (guild_deliver(g, active, item, &outcome) == GUILD_OK) {
            stats->guild_delivered++;
            if (contract.kind == GUILD_CONTRACT_DRAGON_HUNT) {
                stats->hunt_delivered++;
                stats->scales += outcome.reward_qty;
            }
        }
    }
}

static void bot_prepare_day(BotRunner *bot) {
    GameState *g = &bot->game;
    BotDayStats stats = {0};
    int money_before = g->money;
    int rep_before = g->rep;

    bot_guild_turn(g, &stats);
    if (g->up_level[UP_STORE] < 2 && g->money > 700 && upgrade_buy(g, UP_STORE) == R_OK) {
        stats.upgrades++;
    }
    if (g->up_level[UP_FORGE] < 2 && g->money > 500 && upgrade_buy(g, UP_FORGE) == R_OK) {
        stats.upgrades++;
    }

    int have[EQ_COUNT] = {0};
    for (int i = 0; i < g->inv_count; ++i) {
        if (g->inv[i].type >= 0 && g->inv[i].type < EQ_COUNT) {
            have[g->inv[i].type]++;
        }
    }
    for (int eq = 0; eq < EQ_COUNT; ++eq) {
        if (!equip_unlocked(g->guild_done, eq)) {
            continue;
        }
        while (have[eq] < PLANS[eq].target && have[eq] < customers_per_day(g)) {
            const int *recipe = PLANS[eq].plan[bot->recipe_rotation++ % 2];
            int main = recipe[0], sub = recipe[1];
            if (!ensure_recipe_materials(g, main, sub, &stats) ||
                craft_equipment(g, eq, main, sub, NULL) != R_OK) {
                break;
            }
            stats.crafts++;
            have[eq]++;
        }
    }

    if (stats.hunt_delivered > 0) {
        log_action(bot, "Dragon Hunt: delivered %d, +%d %s.", stats.hunt_delivered, stats.scales,
                   MATERIALS[GUILD_DRAGON_HUNT.reward_mat].name);
    }
    if (stats.guild_delivered > 0) {
        log_action(bot, "Guild: delivered %d contract(s).", stats.guild_delivered);
    } else if (stats.guild_accepted > 0) {
        log_action(bot, "Guild: accepted %d contract(s).", stats.guild_accepted);
    }
    if (stats.crafts || stats.buys || stats.upgrades) {
        log_action(bot, "Prep: %d crafted, %d buys, %d upgrade(s).", stats.crafts, stats.buys, stats.upgrades);
    } else if (money_before != g->money || rep_before != g->rep) {
        log_action(bot, "Prep: gold %d, reputation %d.", g->money, g->rep);
    }

    Result r = day_open(g);
    if (r == R_OK) {
        bot->customer_pos = g->queue_pos;
        bot->skip_mask = 0;
        log_action(bot, "Day %d: shop opened; %d customer(s).", g->day, g->queue_count);
    } else {
        log_action(bot, "Day %d: could not open shop (error %d).", g->day, r);
        bot->finished = true;
    }
}

void bot_init(BotRunner *bot, uint32_t seed) {
    if (!bot) {
        return;
    }
    memset(bot, 0, sizeof *bot);
    bot->seed = seed ? seed : 1;
    bot->customer_pos = -1;
    game_init(&bot->game, bot->seed);
    log_action(bot, "BOT ready. Balanced stock + guild contracts.");
}

static void bot_serve_customer(BotRunner *bot) {
    GameState *g = &bot->game;
    if (g->queue_pos >= g->queue_count) {
        Result r = day_close_early(g);
        if (r != R_OK) {
            bot->finished = true;
            log_action(bot, "BOT error: cannot close the shop.");
        }
        return;
    }
    if (bot->customer_pos != g->queue_pos) {
        bot->customer_pos = g->queue_pos;
        bot->skip_mask = 0;
    }

    Customer customer = g->queue[g->queue_pos];
    const char *customer_name = CUSTOMER_DEFS[customer.type].name;
    int item = pick_item(g, &customer, bot->skip_mask);
    if (item < 0) {
        customer_decline(g);
        bot->skip_mask = 0;
        bot->customer_pos = g->queue_pos;
        log_action(bot, "%s left: no suitable stock.", customer_name);
        return;
    }

    int item_type = g->inv[item].type;
    int price = g->inv[item].price;
    SaleOutcome outcome;
    Result r = customer_offer(g, item, &outcome);
    if (r != R_OK) {
        bot->finished = true;
        log_action(bot, "BOT error: customer action failed (%d).", r);
        return;
    }
    if (outcome.sold) {
        log_action(bot, "%s bought %s for %d gold.", customer_name, EQUIPS[item_type].name, price);
    } else if (outcome.customer_left) {
        log_action(bot, "%s left after rejecting %s.", customer_name, EQUIPS[item_type].name);
    } else {
        log_action(bot, "%s rejected %s; trying another.", customer_name, EQUIPS[item_type].name);
    }

    if (g->phase != PH_OPEN || g->queue_pos != bot->customer_pos) {
        bot->skip_mask = 0;
        bot->customer_pos = g->queue_pos;
    } else if (!outcome.sold) {
        bot->skip_mask |= 1 << item;
    } else {
        bot->skip_mask = 0;
    }
}

void bot_step(BotRunner *bot) {
    if (!bot || bot->finished) {
        return;
    }
    GameState *g = &bot->game;
    if (!game_validate(g)) {
        bot->finished = true;
        log_action(bot, "BOT stopped: game state validation failed.");
        return;
    }

    if (g->phase == PH_PREP) {
        if (g->status != ST_PLAYING && g->status != ST_SANDBOX) {
            bot->finished = true;
            log_action(bot, "Campaign ended (status %d).", g->status);
            return;
        }
        bot_prepare_day(bot);
        return;
    }
    if (g->phase == PH_OPEN) {
        bot_serve_customer(bot);
        return;
    }
    if (g->phase == PH_SETTLE) {
        if (!bot->settlement_logged) {
            log_action(bot, "Day %d: sold %d, revenue %d, net %+d.", g->day, g->report.sold,
                       g->report.revenue, net_of(&g->report));
            bot->settlement_logged = true;
            if (g->status == ST_CLEARED) {
                bot->finished = true;
                log_action(bot, "CAMPAIGN CLEARED: %d gold, %d reputation.", g->money, g->rep);
            } else if (g->status == ST_FAILED) {
                bot->finished = true;
                log_action(bot, "CAMPAIGN FAILED on day %d (%d gold).", g->day, g->money);
            }
            return;
        }
        if (day_advance(g) == R_OK) {
            bot->settlement_logged = false;
            log_action(bot, "Starting day %d.", g->day);
        } else {
            bot->finished = true;
            log_action(bot, "BOT stopped: cannot advance to next day.");
        }
        return;
    }
    bot->finished = true;
    log_action(bot, "BOT stopped: unknown game phase.");
}
