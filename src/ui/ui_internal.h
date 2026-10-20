/* LOOT SMITH - ui 内部専用ヘッダ。
 * ui ディレクトリ内の各 .c ファイルの間で共有する描画ヘルパ・ウィジェット・画面関数の宣言をここに集める。
 * 公開インターフェースは ui.h。ui / main / tests 以外からは include しない。 */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"
#include "text.h"
#include "core/icons.h"
#include "audio/audio.h"

#define RGB(r, g, b) ((Color){r, g, b, 255})

/* ui_draw.c: パレット (定義は ui_draw.c) */
extern const Color C_BG, C_PANEL, C_PANEL2, C_BORDER, C_BORDER_D, C_TEXT, C_DIM, C_GOLD, C_ORANGE, C_GOOD, C_BAD,
    C_MANA, C_BTN, C_BTN_H, C_BTN_OFF, C_BTN_SEL, C_STEEL, C_BLACK;

enum {
    BF_DIM = 1,
    BF_OFF = 2,
    BF_SEL = 4,
    BF_NAV = 8,
    BF_BACK = 16
}; /* NAV / BACK は、どのやわらかいチック音を鳴らすかを選ぶだけ */

/* ---------- 在庫画面と客画面で共有する在庫リスト ---------- */
static const int INV_ROWS = 11;

/* ---- ui_draw.c ---- */
Color icol(IconColor c);
void draw_mat_icon(int mat, int x, int y, int sc);
void draw_equip_icon(int type, int main, int sub, int x, int y, int sc);
void draw_silhouette(const char *icon_id, int x, int y, int sc);
void draw_cust_icon(int type, int x, int y, int sc);
void txt(const char *s, int x, int y, Color c);
void txt_big(const char *s, int x, int y, int size, Color c);
void txt_c(const char *s, int cx, int y, int size, Color c);
void txt_r(const char *s, int rx, int y, Color c);
void panel(int x, int y, int w, int h, const char *title);
void bar(int x, int y, int w, int h, int val, int max, Color c);
bool in_rect(Vector2 p, int x, int y, int w, int h);
void txt_fit(const char *s, int x, int y, int maxw, Color c);
int page_count(int items, int per);
int clamp_page(int page, int pages);
const char *equip_name(const Equipment *e);
Color quality_color(int q);
const char *quality_tag(int q);
const char *equip_full_name(const Equipment *e);
const char *trait_line(const Equipment *e);
const char *trait_label(const Equipment *e, int maxw); /* 狭い欄用: 長い特性名は先頭の単語に縮める */
void quality_frame(int q, int x, int y, int sc);
const char *trend_txt(int now, int prev);
Color trend_col(int now, int base);
int market_mat_count(void); /* 市場に並ぶ素材の数 (hunt_only を除く) */
int market_mat_at(int i);   /* i 番目の市場素材の ID。範囲外なら -1 */

/* ---- ui_widgets.c ---- */
bool btn(App *a, int x, int y, int w, int h, const char *label, int flags, int key);
bool key_hit(App *a, int key);
bool pager_btn(App *a, int rx, int y, int page, int pages, int key, const char *keyname);
void say(App *a, bool good, const char *fmt, ...);
void goto_screen(App *a, int s);
void spark(App *a, float x, float y, int n, Color c);
void floaty(App *a, float x, float y, Color c, const char *fmt, ...);
void fx_update_draw(App *a, float dt);

/* ---- ui_chrome.c ---- */
void options_button(App *a, int x, int y, int w, int h);
void draw_header(App *a);
void draw_footer(App *a);
void draw_anvil(const App *a, int x, int y);
void draw_furnace(App *a, int x, int y, int w, int h);
void draw_scene(App *a, int x, int y, int w, int h);

/* ---- ui_app.c ---- */
int screen_for_state(const GameState *g);
void new_game(App *a);
void do_save(App *a, bool quiet);
void do_load(App *a);
void follow_phase(App *a);
void try_open_shop(App *a);
void nav_to(App *a, int scr);

/* ---- screen_options.c ---- */
void save_settings_now(App *a);
void open_options(App *a);
void screen_options(App *a);

/* ---- screen_home.c ---- */
void screen_title(App *a);
void screen_main(App *a);
void screen_bot(App *a);

/* ---- screen_shop.c ---- */
void screen_market(App *a);
void screen_upgrade(App *a);

/* ---- screen_craft.c ---- */
void screen_craft(App *a);

/* ---- screen_inventory.c ---- */
void stock_list_headers(int x, int y, bool fit);
void stock_row(const Equipment *e, int x, int y, bool sel, bool dim, int fit, bool show_fit);
void stock_list_input(App *a, int x, int y0, bool keys);
void screen_inv(App *a);

/* ---- screen_customer.c ---- */
void draw_pips(int x, int y, int n, int max, Color c);
void screen_cust(App *a);

/* ---- screen_report.c ---- */
void screen_settle(App *a);
void screen_clear(App *a);
void screen_over(App *a);

/* ---- screen_guild.c ---- */
const char *upper_txt(const char *s);
void draw_banner(App *a);
void screen_guild(App *a);
void screen_blueprints(App *a);

/* ---- screen_help.c ---- */
void screen_help(App *a);

#endif
