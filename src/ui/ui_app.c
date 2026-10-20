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

void app_demo(App *a, int screen) {
    GameState *g = &a->gs;
    game_init(g, 4242);
    g->money = 800;
    g->rep = 45;
    g->day = 7;
    g->guild_done = 20; /* 熟練の職人 (Master Craftsman): Warhammer と Rune Bow は解放済み、Crystal Orb はまだ未解放 */
    guild_refresh_board(g); /* game_init() は 1 日目 / 見習い (Apprentice) の掲示板を出した: 7
                             * 日目とこのランクで出し直す (guild_rng のみ使用。GameState.rng には触れない) */
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
    craft_equipment(g, EQ_DAGGER, MAT_COPPER, MAT_WOOD, NULL);
    craft_equipment(g, EQ_SPEAR, MAT_STEEL, MAT_IRON, NULL);
    craft_equipment(g, EQ_GRIMOIRE, MAT_SILVER, MAT_OBSIDIAN, NULL);
    craft_equipment(g, EQ_WARHAMMER, MAT_IRON, MAT_STEEL, NULL);
    craft_equipment(g, EQ_RUNE_BOW, MAT_OBSIDIAN, MAT_COPPER, NULL);
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
    a->cr_eq = EQ_GRIMOIRE;
    a->cr_main = MAT_OBSIDIAN;
    a->cr_sub = MAT_SILVER;
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
        g->guild_active[0].min_fit = 20;
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
