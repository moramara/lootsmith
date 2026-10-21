#include "ui_internal.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool phase_locked_to_settle(const App *a) {
    return a->gs.phase == PH_SETTLE;
}

static void reset_view(App *a) {
    a->sel_inv = 0;
    a->inv_scroll = 0;
    a->mk_row = 0;
    a->mk_scroll = 0;
    a->pg_main = a->pg_strip = a->pg_settle = a->pg_up = 0;
    a->gl_sel = 0;
    a->gl_item = -1;
    a->gl_scroll = 0;
    a->pg_bp = 0;
    a->banner_t = 0;
    for (int m = 0; m < MAT_COUNT; m++) {
        a->mk_qty[m] = 1;
    }
    a->cr_eq = EQ_SWORD;
    a->cr_main = MAT_IRON;
    a->cr_sub = MAT_IRON;
    a->has_last_craft = false;
    a->last_money = a->gs.money;
    a->react_pos = -1;
    a->react[0] = 0;
    a->msg_t = 0;
    memset(a->parts, 0, sizeof a->parts);
    memset(a->floats, 0, sizeof a->floats);
}

int screen_for_state(const GameState *g) {
    if (g->status == ST_CLEARED && g->phase == PH_SETTLE) {
        return SCR_SETTLE;
    }
    if (g->phase == PH_OPEN) {
        return SCR_CUST;
    }
    if (g->phase == PH_SETTLE) {
        return SCR_SETTLE;
    }
    return SCR_MAIN;
}

void new_game(App *a) {
    if (a->seed_fixed) {
        a->seed_fixed = false; /* --seed N が明示された: 最初のキャンペーンは N をそのまま使う (再現可能) */
    } else {
        a->seed = campaign_seed(
            a->seed, (uint32_t)time(NULL)); /* その後のキャンペーン (やり直し、新規ゲーム) は従来どおり異なる */
    }
    game_init(&a->gs, a->seed);
    reset_view(a);
    goto_screen(a, SCR_MAIN);
    say(a, true, "A new forge awaits. Buy materials at the MARKET!");
    sfx_request(&a->sfx, SE_DAY_START);
}

void do_save(App *a, bool quiet) {
    SaveResult r = game_save_file(&a->gs, a->save_path);
    a->has_save = (r == SAVE_OK) || a->has_save;
    if (!quiet || r != SAVE_OK) {
        say(a, r == SAVE_OK, r == SAVE_OK ? "Game saved." : "Save failed: %s", text_save(r));
    }
}

void do_load(App *a) {
    GameState t;
    SaveResult r = game_load_file(&t, a->save_path);
    if (r != SAVE_OK) {
        say(a, false, "Load failed: %s", text_save(r));
        return;
    }
    a->gs = t;
    reset_view(a);
    goto_screen(a, screen_for_state(&a->gs));
    say(a, true, "Game loaded. Day %d.", a->gs.day);
}

/* どの操作の後でも、表示中の画面がゲームのフェーズと一致するようにする。 */
void follow_phase(App *a) {
    GameState *g = &a->gs;
    if (g->phase == PH_SETTLE && a->screen != SCR_SETTLE) {
        goto_screen(a, SCR_SETTLE);
    }
}

void try_open_shop(App *a) {
    GameState *g = &a->gs;
    if (g->phase == PH_OPEN) {
        goto_screen(a, SCR_CUST);
        return;
    }
    Result r = day_open(g);
    if (r != R_OK) {
        say(a, false, "%s", text_result(r));
        return;
    }
    a->react_pos = -1;
    a->react[0] = 0;
    say(a, true, "The shop is open! %d customers are coming.", g->queue_count);
    sfx_request(&a->sfx, SE_SHOP_OPEN);
    goto_screen(a, g->phase == PH_SETTLE ? SCR_SETTLE : SCR_CUST);
}

static bool nav_allowed(App *a, int scr) {
    GameState *g = &a->gs;
    if (scr == SCR_HELP) {
        return true;
    }
    if ((scr == SCR_GUILD || scr == SCR_BLUEPRINT) && (g->status == ST_CLEARED || g->status == ST_FAILED)) {
        say(a, false, "The campaign is over.");
        return false;
    }
    if (g->phase == PH_OPEN && scr != SCR_MAIN && scr != SCR_CUST) {
        say(a, false, "The shop is open - finish with your customers first.");
        return false;
    }
    if (g->phase == PH_SETTLE) {
        say(a, false, "Check the day's results first.");
        return false;
    }
    return true;
}

void nav_to(App *a, int scr) {
    if (nav_allowed(a, scr)) {
        goto_screen(a, scr);
    }
}

void app_start_bot(App *a) {
    bot_init(&a->bot, a->seed);
    a->bot_paused = false;
    a->bot_speed = 1;
    a->bot_accum = 0;
    goto_screen(a, SCR_BOT);
}

void app_init(App *a, uint32_t seed, const char *save_path) {
    memset(a, 0, sizeof *a);
    a->seed = seed ? seed : 1;
    a->save_path = save_path;
    a->has_save = save_file_exists(save_path);
    a->screen = SCR_TITLE;
    a->next_screen = -1;
    sfx_queue_init(&a->sfx); /* memset で pending = 0 (有効な ID) になっているので、SE_NONE にしなければならない */
    settings_defaults(&a->settings);
    a->opt_drag = -1;
    a->options_return = SCR_TITLE;
    game_init(&a->gs, a->seed);
    reset_view(a);
}

void app_set_settings(App *a, const Settings *s, const char *path) {
    a->settings = *s;
    settings_sanitize(&a->settings);
    a->settings_path = path;
    a->settings_dirty = false;
    a->settings_idle = 0;
}

void app_shutdown(App *a) {
    save_settings_now(a);
}

void ui_frame(App *a, Vector2 mouse, bool click, float dt) {
    a->mouse = mouse;
    a->click = click;
    a->consumed = false;
    a->t += dt;
    if (a->msg_t > 0) {
        a->msg_t -= dt;
    }
    if (a->anvil_t > 0) {
        a->anvil_t -= dt;
    }
    if (a->opt_preview_t > 0) {
        a->opt_preview_t -= dt;
    }
    ClearBackground(C_BG);

    switch (a->screen) {
        case SCR_TITLE:
            screen_title(a);
            break;
        case SCR_MAIN:
            screen_main(a);
            draw_footer(a);
            break;
        case SCR_MARKET:
            screen_market(a);
            break;
        case SCR_CRAFT:
            screen_craft(a);
            break;
        case SCR_INV:
            screen_inv(a);
            break;
        case SCR_CUST:
            screen_cust(a);
            break;
        case SCR_UPG:
            screen_upgrade(a);
            break;
        case SCR_SETTLE:
            screen_settle(a);
            break;
        case SCR_CLEAR:
            screen_clear(a);
            break;
        case SCR_OVER:
            screen_over(a);
            break;
        case SCR_HELP:
            screen_help(a);
            break;
        case SCR_GUILD:
            screen_guild(a);
            break;
        case SCR_BLUEPRINT:
            screen_blueprints(a);
            break;
        case SCR_OPTIONS:
            screen_options(a);
            break;
        case SCR_BOT:
            screen_bot(a);
            break;
        default:
            break;
    }
    /* F2 はどの画面からでも OPTIONS を開く (OPTIONS ボタンのある画面は、上でこのキーを自分で処理している) */
    if (a->screen != SCR_OPTIONS && a->screen != SCR_BOT && key_hit(a, KEY_F2)) {
        open_options(a);
    }
    /* Esc = サブ画面からメイン画面へ戻る */
    if (((a->screen >= SCR_MARKET && a->screen <= SCR_UPG) || a->screen == SCR_GUILD) && key_hit(a, KEY_ESCAPE)) {
        sfx_request(&a->sfx, SE_UI_BACK);
        goto_screen(a, SCR_MAIN);
    }
    if (a->screen == SCR_BLUEPRINT && key_hit(a, KEY_ESCAPE)) {
        sfx_request(&a->sfx, SE_UI_BACK);
        goto_screen(a, SCR_GUILD);
    }

    /* ゴールドの増減のフィードバック */
    if (a->screen != SCR_TITLE && a->gs.money != a->last_money) {
        int dm = a->gs.money - a->last_money;
        floaty(a, 232, 26, dm > 0 ? C_GOOD : C_BAD, "%+d", dm);
        a->last_money = a->gs.money;
    }
    if (a->screen == SCR_TITLE) {
        a->last_money = a->gs.money;
    }
    fx_update_draw(a, dt);
    draw_banner(a); /* どの画面よりも手前に表示する。時間が経てば消えるだけ */
    if (a->banner_t > 0) {
        a->banner_t -= dt;
    }

    if (a->settings_dirty && a->opt_drag < 0) { /* キーボードでの変更: プレイヤーが調整をやめたら一度だけ保存する */
        a->settings_idle += dt;
        if (a->settings_idle >= 0.6f) {
            save_settings_now(a);
        }
    }
    if (a->next_screen >= 0) {
        if (a->next_screen != a->screen) { /* 終了画面に入るとき: 到着ごとにファンファーレを 1 回 */
            if (a->next_screen == SCR_CLEAR) {
                sfx_request(&a->sfx, SE_CLEAR);
            } else if (a->next_screen == SCR_OVER) {
                sfx_request(&a->sfx, SE_GAMEOVER);
            }
        }
        a->screen = a->next_screen;
        a->next_screen = -1;
    }
    SeId se = sfx_take(&a->sfx); /* このフレームの唯一の音 */
    if (se != SE_NONE) {
        audio_play(se);
    }
    (void)phase_locked_to_settle;
}

/* デモ専用: クラフト済みのアイテムを最初から作り直して、すべての品質/特性を見せる (抽選結果の上に重ねない) */
static void demo_quality(GameState *g, int idx, int q, int trait) {
    Equipment *e = &g->inv[idx];
    equipment_build(e, g->up_level[UP_FORGE], q, trait);
    equipment_price(e);
    e->price = e->rec_price;
}

/* demo = 画面 + 100 * シナリオ。シナリオ 1..4 は Ascendant Smith (納品 75 件) の状態で、日付がイベントの日になる:
 * 1 = Market Caravan (2 日目)、2 = Arcane Convergence (4 日目)、3 = Guild Assembly (6 日目)、4 = イベントなし (1 日目)
 */
void app_demo(App *a, int screen) {
    GameState *g = &a->gs;
    int scen = screen / 100;
    screen %= 100;
    static const int scen_day[5] = {7, 2, 4, 6, 1};
    if (scen < 0 || scen > 4) {
        scen = 0;
    }
    game_init(g, 4242);
    g->money = scen ? 3000 : 800;
    g->rep = 45;
    g->day = scen_day[scen];
    g->guild_done =
        scen ? 75 : 20; /* 熟練の職人 (Master Craftsman): Warhammer と Rune Bow は解放済み、Crystal Orb はまだ未解放 */
    guild_refresh_board(g); /* game_init() は 1 日目 / 見習い (Apprentice) の掲示板を出した: 7
                             * 日目とこのランクで出し直す (guild_rng のみ使用。GameState.rng には触れない) */
    if (scen) { /* 掲示板に Relic Expedition を 1 件だけ載せる (特別契約は最大 1 件、依頼主は重ならない) */
        for (uint32_t k = 1; k < 200; k++) {
            g->guild_rng = k;
            guild_refresh_board(g);
            int specials = 0, clash = 0;
            for (int i = 0; i < g->guild_board_count; i++) {
                specials += g->guild_board[i].kind != GUILD_CONTRACT_STANDARD;
                clash += i > 0 && g->guild_board[i].customer_type == GUILD_RELIC_EXPEDITION.customer;
            }
            if (specials == 0 && !clash) {
                break;
            }
        }
        const SpecialContractDef *rd = &GUILD_RELIC_EXPEDITION;
        g->guild_board[0] = (GuildContract){g->guild_board[0].id,
                                            rd->customer,
                                            rd->min_fit,
                                            rd->difficulty,
                                            rd->gold,
                                            rd->rep,
                                            rd->days,
                                            0,
                                            GUILD_CONTRACT_RELIC_EXPEDITION,
                                            rd->reward_mat,
                                            rd->reward_qty};
    }
    market_buy(g, MAT_IRON, 12);
    market_buy(g, MAT_WOOD, 6);
    market_buy(g, MAT_MANA, 6);
    craft_equipment(g, EQ_SWORD, MAT_IRON, MAT_IRON, NULL);
    craft_equipment(g, EQ_SWORD, MAT_WOOD, MAT_WOOD, NULL);
    craft_equipment(g, EQ_SHIELD, MAT_IRON, MAT_WOOD, NULL);
    craft_equipment(g, EQ_STAFF, MAT_MANA, MAT_WOOD, NULL);
    for (int m = MAT_COPPER; m < MAT_COUNT; m++) {
        market_buy(g, m, 6); /* 後半のコンテンツも見せる */
    }
    g->mat_stock[MAT_DRAGON_SCALE] = 2; /* Dragon Hunt の報酬 (無料: 原価 0) を持っている状態を見せる */
    if (scen) {
        g->mat_stock[MAT_ASTRAL_SHARD] = 4; /* Relic Expedition の報酬を持っている状態 */
        market_buy(g, MAT_MOONSTONE, 3);
        market_buy(g, MAT_MITHRIL, 3);
    }
    craft_equipment(g, EQ_DAGGER, MAT_COPPER, MAT_WOOD, NULL);
    craft_equipment(g, EQ_SPEAR, MAT_STEEL, MAT_IRON, NULL);
    craft_equipment(g, EQ_GRIMOIRE, MAT_SILVER, MAT_OBSIDIAN, NULL);
    craft_equipment(g, EQ_WARHAMMER, MAT_IRON, MAT_STEEL, NULL);
    craft_equipment(g, EQ_RUNE_BOW, MAT_OBSIDIAN, MAT_COPPER, NULL);
    if (scen) { /* 新装備: 末尾 (添字 9, 10) に置く。下の品質の上書きは先頭 9 つだけ */
        craft_equipment(g, EQ_VOID_CODEX, MAT_ASTRAL_SHARD, MAT_MOONSTONE, NULL);
        craft_equipment(g, EQ_ASTRAL_BLADE, MAT_MITHRIL, MAT_STEEL, NULL);
    }
    { /* さまざまな品質と特性 (添字は上のクラフト順に対応する) */
        static const int q[9] = {QUALITY_COMMON,    QUALITY_FINE,       QUALITY_MASTERWORK,
                                 QUALITY_LEGENDARY, QUALITY_MASTERWORK, QUALITY_LEGENDARY,
                                 QUALITY_FINE,      QUALITY_MASTERWORK, QUALITY_LEGENDARY};
        static const int t[9] = {EQ_TRAIT_NONE,   EQ_TRAIT_NONE,        EQ_TRAIT_WARDING,
                                 EQ_TRAIT_ARCANE, EQ_TRAIT_LIGHTWEIGHT, EQ_TRAIT_KEEN,
                                 EQ_TRAIT_NONE,   EQ_TRAIT_STURDY,      EQ_TRAIT_LIGHTWEIGHT};
        for (int i = 0; i < 9 && i < g->inv_count; i++) {
            demo_quality(g, i, q[i], t[i]);
        }
    }
    g->up_level[UP_FORGE] = 1;
    g->price[MAT_WOOD] = 5;
    g->prev_price[MAT_WOOD] = 6;
    reset_view(a);
    if (scen) {
        a->mk_scroll = scen == 3 ? 9 : 8; /* 新素材の行を見せる (reset_view の後) */
        a->mk_row = a->mk_scroll;
        a->help_scroll = scen * 28; /* ヘルプの後半 (新コンテンツの節) を見せる: 28 行ずつ */
        a->pg_bp = 1;               /* 設計図の 2 ページ目 (Ascendant Smith の 3 つ) */
    }
    a->cr_eq = EQ_GRIMOIRE;
    a->cr_main = MAT_OBSIDIAN;
    a->cr_sub = MAT_SILVER;
    if (scen) {
        a->cr_eq = EQ_VOID_CODEX;
        a->cr_main = MAT_ASTRAL_SHARD;
        a->cr_sub = MAT_MOONSTONE;
        g->mat_stock[MAT_ASTRAL_SHARD] += 2;
    }
    if (g->inv_count > 8) {
        a->last_craft = g->inv[8];
        a->has_last_craft = true;
    }
    a->screen = screen;
    if (screen == SCR_CUST || screen == SCR_SETTLE || screen == SCR_CLEAR || screen == SCR_OVER) {
        if (screen == SCR_SETTLE && guild_accept(g, 0) == GUILD_OK) { /* レポートにギルドの納品を表示する */
            g->guild_active[0].min_fit = 0;
            for (int i = 0; i < g->inv_count; i++) {
                GuildOutcome o;
                if (guild_deliver(g, 0, i, &o) == GUILD_OK) {
                    break;
                }
            }
            if (guild_accept(g, 0) == GUILD_OK) {
                g->guild_active[0].deadline_day = g->day;
                g->guild_active[0].days = 1;
            } /* 本日で失効するもの */
        }
        day_open(g);
        if (screen == SCR_CUST) {
            SaleOutcome o;
            g->inv[1].price = 900;
            customer_offer(g, 1, &o);
            a->react_pos = g->queue_pos;
            snprintf(a->react, sizeof a->react, "%s", text_sale(o.result));
        }
        if (screen != SCR_CUST) {
            day_close_early(g);
        }
    }
    if (screen == SCR_CLEAR) {
        g->status = ST_CLEARED;
    }
    if (screen == SCR_OVER) {
        g->status = ST_FAILED;
        g->fail_reason = FAIL_TIMEOUT;
    }
    if (screen == SCR_GUILD) { /* 契約を 2 件受注し、最初の契約に納品できるアイテムを 1 つ持たせる */
        guild_accept(g, 0);
        guild_accept(g, 0);
        if (!scen) {
            g->guild_active[0].min_fit = 20;
        }
        a->gl_sel = 0;
    }
    a->last_money = g->money;
    if (screen == SCR_HELP) {
        a->help_return = SCR_MAIN;
    }
    if (screen == SCR_OPTIONS) {
        a->options_return = SCR_MAIN;
    }
}
