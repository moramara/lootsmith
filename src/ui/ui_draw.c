#include "ui_internal.h"
#include <string.h>

const Color C_BG = RGB(20, 14, 11), C_PANEL = RGB(44, 31, 22), C_PANEL2 = RGB(60, 43, 30);

const Color C_BORDER = RGB(122, 86, 52), C_BORDER_D = RGB(78, 52, 32);

const Color C_TEXT = RGB(236, 222, 196), C_DIM = RGB(140, 124, 104), C_GOLD = RGB(255, 205, 70);

const Color C_ORANGE = RGB(255, 140, 30), C_GOOD = RGB(120, 210, 96), C_BAD = RGB(230, 78, 66);

const Color C_MANA = RGB(176, 110, 255), C_BTN = RGB(88, 62, 40), C_BTN_H = RGB(130, 92, 58);

const Color C_BTN_OFF = RGB(52, 44, 38), C_BTN_SEL = RGB(158, 104, 40), C_STEEL = RGB(170, 176, 188);

const Color C_BLACK = RGB(10, 8, 7);

/* =====================================================================
 * 12x12 ピクセルのスプライト。ピクセルデータは icons.c にあり (各コンテンツテーブルの行に保存された文字列 ID で引く)、
 * ここでは、スプライトの文字を色に対応づけて描画するだけ。
 * M/m = 主素材の明/暗、S = 副素材、H/A/T = 客の帽子/鎧/上着の色。
 * ===================================================================== */
typedef struct {
    Color M, m, S, H, A, T;
} Pal;

Color icol(IconColor c) {
    return (Color){c.r, c.g, c.b, c.a};
}

/* icons.c の ICON_PALETTE_CHARS と同期させておくこと */
static Color pal_color(char ch, const Pal *p) {
    switch (ch) {
        case 'M':
            return p->M;
        case 'm':
            return p->m;
        case 'S':
            return p->S;
        case 'H':
            return p->H;
        case 'A':
            return p->A;
        case 'T':
            return p->T;
        case 'h':
            return RGB(210, 214, 224);
        case 'w':
            return RGB(240, 240, 250);
        case 'k':
            return RGB(20, 14, 12);
        case 'b':
            return RGB(120, 78, 40);
        case 'B':
            return RGB(78, 48, 24);
        case 'n':
            return RGB(214, 170, 110);
        case 's':
            return RGB(238, 190, 150);
        case 'g':
            return RGB(150, 154, 166);
        case 'p':
            return RGB(150, 84, 235);
        case 'P':
            return RGB(214, 170, 255);
        case 'q':
            return RGB(92, 48, 170);
        default:
            return BLANK;
    }
}

static void draw_sprite(const IconSprite *spr, int x, int y, int sc, const Pal *p) {
    for (int r = 0; r < ICON_SIZE; r++) {
        for (int c = 0; c < ICON_SIZE && spr->rows[r][c]; c++) {
            if (spr->rows[r][c] == '.') {
                continue;
            }
            DrawRectangle(x + c * sc, y + r * sc, sc, sc, pal_color(spr->rows[r][c], p));
        }
    }
}

/* 3 つのアイコン補助関数はいずれも、不正な ID や未設定のアイコンを許容する (「?」スプライトにフォールバックする)。 */
static Pal neutral_pal(void) {
    return (Pal){C_STEEL, C_DIM, WHITE, WHITE, WHITE, WHITE};
}

void draw_mat_icon(int mat, int x, int y, int sc) {
    if (mat < 0 || mat >= MAT_COUNT) {
        Pal n = neutral_pal();
        draw_sprite(icon_find(NULL), x, y, sc, &n);
        return;
    }
    const MaterialDef *d = &MATERIALS[mat];
    Pal p = {icol(d->color), icol(d->color_dark), WHITE, WHITE, WHITE, WHITE};
    draw_sprite(icon_find(d->icon), x, y, sc, &p);
}

void draw_equip_icon(int type, int main, int sub, int x, int y, int sc) {
    if (type < 0 || type >= EQ_COUNT) {
        Pal n = neutral_pal();
        draw_sprite(icon_find(NULL), x, y, sc, &n);
        return;
    }
    Pal p = neutral_pal();
    if (main >= 0 && main < MAT_COUNT) {
        p.M = icol(MATERIALS[main].color);
        p.m = icol(MATERIALS[main].color_dark);
    }
    if (sub >= 0 && sub < MAT_COUNT) {
        p.S = icol(MATERIALS[sub].color);
    }
    draw_sprite(icon_find(EQUIPS[type].icon), x, y, sc, &p);
}

/* 未解放のもの: スプライトを平坦な灰色のシルエットにする (未知の ID はこれまでどおり「?」スプライトになる) */
void draw_silhouette(const char *icon_id, int x, int y, int sc) {
    Color body = RGB(70, 60, 54), shine = RGB(96, 84, 74);
    const IconSprite *spr = icon_find(icon_id);
    for (int r = 0; r < ICON_SIZE; r++) {
        for (int c = 0; c < ICON_SIZE && spr->rows[r][c]; c++) {
            char ch = spr->rows[r][c];
            if (ch == '.') {
                continue;
            }
            DrawRectangle(x + c * sc, y + r * sc, sc, sc, (ch == 'h' || ch == 'w') ? shine : body);
        }
    }
}

void draw_cust_icon(int type, int x, int y, int sc) {
    Pal p = neutral_pal();
    const char *id = NULL;
    if (type >= 0 && type < CUST_TYPE_COUNT) {
        const CustomerDef *d = &CUSTOMER_DEFS[type];
        p.H = icol(d->hat);
        p.A = icol(d->armor);
        p.T = icol(d->top);
        id = d->icon;
    }
    draw_sprite(icon_find(id), x, y, sc, &p);
}

void txt(const char *s, int x, int y, Color c) {
    DrawText(s, x, y, 10, c);
}

void txt_big(const char *s, int x, int y, int size, Color c) {
    DrawText(s, x, y, size, c);
}

void txt_c(const char *s, int cx, int y, int size, Color c) {
    DrawText(s, cx - MeasureText(s, size) / 2, y, size, c);
}

void txt_r(const char *s, int rx, int y, Color c) {
    DrawText(s, rx - MeasureText(s, 10), y, 10, c);
}

void panel(int x, int y, int w, int h, const char *title) {
    DrawRectangle(x, y, w, h, C_PANEL);
    DrawRectangleLines(x, y, w, h, C_BORDER);
    DrawRectangle(x + 1, y + 1, w - 2, 1, C_PANEL2);
    if (title) {
        DrawRectangle(x + 1, y + 1, w - 2, 12, C_BORDER_D);
        txt_fit(title, x + 5, y + 3, w - 10, C_GOLD);
    }
}

void bar(int x, int y, int w, int h, int val, int max, Color c) {
    DrawRectangle(x, y, w, h, C_BLACK);
    if (max > 0 && val > 0) {
        DrawRectangle(x + 1, y + 1, (w - 2) * (val > max ? max : val) / max, h - 2, c);
    }
    DrawRectangleLines(x, y, w, h, C_BORDER_D);
}

bool in_rect(Vector2 p, int x, int y, int w, int h) {
    return p.x >= x && p.y >= y && p.x < x + w && p.y < y + h;
}

/* `s` を maxw ピクセルで切り詰めて描画し、収まらない場合は末尾を "..." にする (長いコンテンツ名/テキスト用)。 */
void txt_fit(const char *s, int x, int y, int maxw, Color c) {
    if (MeasureText(s, 10) <= maxw) {
        txt(s, x, y, c);
        return;
    }
    char buf[104];
    size_t n = strlen(s);
    if (n > sizeof buf - 4) {
        n = sizeof buf - 4;
    }
    memcpy(buf, s, n);
    while (n > 0) {
        memcpy(buf + n, "...", 4);
        if (MeasureText(buf, 10) <= maxw) {
            break;
        }
        n--;
    }
    if (n == 0) {
        return;
    }
    txt(buf, x, y, c);
}

int page_count(int items, int per) {
    return per > 0 && items > 0 ? (items + per - 1) / per : 1;
}

int clamp_page(int page, int pages) {
    return page < 0 ? 0 : page >= pages ? pages - 1 : page;
}

/* ---------- 書式 ---------- */
const char *equip_name(const Equipment *e) {
    return TextFormat("%s %s", MATERIALS[e->main].name, EQUIPS[e->type].name);
}

Color quality_color(int q) {
    return q >= 0 && q < QUALITY_COUNT ? icol(QUALITY_DEFS[q].color) : C_TEXT;
}

const char *quality_tag(int q) {
    return q >= 0 && q < QUALITY_COUNT ? QUALITY_DEFS[q].tag : "?";
}

/* "Masterwork Iron Sword": 余裕のある場所 (詳細パネル、メッセージ) では品質名を常に綴る ... */
const char *equip_full_name(const Equipment *e) {
    return TextFormat("%s %s %s", quality_name(e->quality), MATERIALS[e->main].name, EQUIPS[e->type].name);
}

/* ... 特性は "TRAIT: KEEN (+2 ATK)"。アイテムに特性がなければ空文字列。 */
const char *trait_line(const Equipment *e) {
    char fx[24];
    trait_effect_text(e->trait, e->quality, fx, sizeof fx);
    if (!fx[0]) {
        return "";
    }
    return TextFormat("TRAIT: %s (%s)", upper_txt(trait_name(e->trait)), fx);
}

/* 一覧の狭い欄用の特性表示 "KEEN +2 ATK"。特性名が長くて maxw に収まらないときは、名前を先頭の単語に縮める
 * ("DRACONIC EDGE +3 ATK" -> "DRACONIC +3 ATK")。効果の値は必ず残す。特性がなければ空文字列 */
const char *trait_label(const Equipment *e, int maxw) {
    char fx[24];
    trait_effect_text(e->trait, e->quality, fx, sizeof fx);
    if (!fx[0]) {
        return "";
    }
    const char *full = TextFormat("%s %s", upper_txt(trait_name(e->trait)), fx);
    if (MeasureText(full, 10) <= maxw) {
        return full;
    }
    char word[32];
    snprintf(word, sizeof word, "%s", upper_txt(trait_name(e->trait)));
    char *sp = strchr(word, ' ');
    if (sp) {
        *sp = 0;
    }
    return TextFormat("%s %s", word, fx);
}

/* 品質の色の枠を、(x, y) に倍率 sc で描いた 12x12 アイコンの周りに付ける (Common: 枠なし) */
void quality_frame(int q, int x, int y, int sc) {
    if (q > QUALITY_COMMON) {
        DrawRectangleLines(x - 1, y - 1, ICON_SIZE * sc + 2, ICON_SIZE * sc + 2, quality_color(q));
    }
}

const char *trend_txt(int now, int prev) {
    return now > prev ? TextFormat("+%d", now - prev) : now < prev ? TextFormat("-%d", prev - now) : "=";
}

Color trend_col(int now, int base) {
    return now * 100 <= base * 93 ? C_GOOD : now * 100 >= base * 107 ? C_BAD : C_TEXT;
}

/* 市場に並ぶ素材 (hunt_only を除く) の一覧: 価格の帯 (ホーム、日報) はこの一覧だけを並べる */
int market_mat_count(void) {
    int n = 0;
    for (int m = 0; m < MAT_COUNT; m++) {
        n += !MATERIALS[m].hunt_only;
    }
    return n;
}

int market_mat_at(int i) {
    for (int m = 0; m < MAT_COUNT; m++) {
        if (!MATERIALS[m].hunt_only && i-- == 0) {
            return m;
        }
    }
    return -1;
}
