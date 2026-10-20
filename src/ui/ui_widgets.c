#include "ui_internal.h"
#include <stdio.h>
#include <stdarg.h>

/* クリック / キー押下ごとに 1 回だけ true を返す。BF_DIM はボタンを灰色にするだけ (クリックは失敗した *理由* を
 * 報告する)。BF_OFF はボタンを無効にする。`consumed` により、1 回の入力につき 1 フレームで 1 つの操作しか起こらない。
 * 音: 押された有効なボタンはやわらかいチック音 (click / nav / back) を要求する。ホバーや、無効/灰色 (BF_DIM / BF_OFF)
 * の ボタンは鳴らさない。操作自体が結果の音 (コイン、エラー、ファンファーレなど) を報告する場合は、フレームごとの
 * SfxQueue でそちらがチック音より優先されるので、1 回の入力で鳴る音はちょうど 1 つになる。 */
bool btn(App *a, int x, int y, int w, int h, const char *label, int flags, int key) {
    bool off = flags & BF_OFF;
    bool hover = !off && in_rect(a->mouse, x, y, w, h);
    Color bg = C_BTN;
    if (flags & BF_SEL) {
        bg = C_BTN_SEL;
    }
    if (flags & (BF_DIM | BF_OFF)) {
        bg = C_BTN_OFF;
    }
    if (hover) {
        bg = (flags & BF_DIM) ? RGB(70, 58, 48) : C_BTN_H;
    }
    DrawRectangle(x, y + 2, w, h, C_BLACK);
    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLines(x, y, w, h, hover ? C_GOLD : C_BORDER);
    Color tc = (flags & (BF_DIM | BF_OFF)) ? C_DIM : C_TEXT;
    txt_c(label, x + w / 2, y + (h - 10) / 2, 10, tc);
    if (off || a->consumed) {
        return false;
    }
    bool fired = (hover && a->click) || (key && IsKeyPressed(key));
    if (fired) {
        a->consumed = true;
        if (!(flags & BF_DIM)) {
            sfx_request(&a->sfx, (flags & BF_BACK) ? SE_UI_BACK : (flags & BF_NAV) ? SE_UI_NAV : SE_UI_CLICK);
        }
    }
    return fired;
}

bool key_hit(App *a, int key) {
    if (a->consumed || !IsKeyPressed(key)) {
        return false;
    }
    a->consumed = true;
    return true;
}

/* 右端が rx にある小さな "PG 1/2 [R]" ページ切り替え。クリック / キー押下で true を返し、
 * 呼び出し側が次のページへ移る。 */
bool pager_btn(App *a, int rx, int y, int page, int pages, int key, const char *keyname) {
    const char *l =
        keyname ? TextFormat("PG %d/%d [%s]", page + 1, pages, keyname) : TextFormat("PG %d/%d >", page + 1, pages);
    int w = MeasureText(l, 10) + 10;
    return btn(a, rx - w, y, w, 12, l, 0, key);
}

void say(App *a, bool good, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(a->msg, sizeof a->msg, fmt, ap);
    va_end(ap);
    a->msg_good = good;
    a->msg_t = 3.5f;
    if (!good) {
        sfx_request(&a->sfx, SE_ERROR); /* 拒否/失敗のメッセージはすべて、やわらかいエラー音 1 つにする */
    }
}

void goto_screen(App *a, int s) {
    a->next_screen = s;
}

/* ---------- エフェクト ---------- */
void spark(App *a, float x, float y, int n, Color c) {
    for (int i = 0, made = 0; i < MAX_PARTS && made < n; i++) {
        if (a->parts[i].life > 0) {
            continue;
        }
        float ang = GetRandomValue(0, 628) / 100.0f - 3.14f, sp = GetRandomValue(20, 110);
        a->parts[i] = (Particle){x,
                                 y,
                                 (float)(sp * (GetRandomValue(-100, 100) / 100.0)),
                                 -sp * (GetRandomValue(30, 100) / 100.0f),
                                 0.5f + GetRandomValue(0, 60) / 100.0f,
                                 1.1f,
                                 c,
                                 GetRandomValue(1, 2)};
        (void)ang;
        made++;
    }
}

void floaty(App *a, float x, float y, Color c, const char *fmt, ...) {
    for (int i = 0; i < MAX_FLOATS; i++) {
        if (a->floats[i].life > 0) {
            continue;
        }
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(a->floats[i].text, sizeof a->floats[i].text, fmt, ap);
        va_end(ap);
        a->floats[i].x = x;
        a->floats[i].y = y;
        a->floats[i].life = 1.4f;
        a->floats[i].c = c;
        return;
    }
}

void fx_update_draw(App *a, float dt) {
    for (int i = 0; i < MAX_PARTS; i++) {
        Particle *p = &a->parts[i];
        if (p->life <= 0) {
            continue;
        }
        p->life -= dt;
        p->vy += 220 * dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        float k = p->life / p->max;
        if (k > 1) {
            k = 1;
        }
        DrawRectangle((int)p->x, (int)p->y, p->size, p->size, k > 0.4f ? p->c : Fade(p->c, k * 2.5f));
    }
    for (int i = 0; i < MAX_FLOATS; i++) {
        FloatText *f = &a->floats[i];
        if (f->life <= 0) {
            continue;
        }
        f->life -= dt;
        f->y -= 14 * dt;
        float al = f->life > 0.5f ? 1.0f : f->life * 2;
        DrawText(f->text, (int)f->x + 1, (int)f->y + 1, 10, Fade(C_BLACK, al));
        DrawText(f->text, (int)f->x, (int)f->y, 10, Fade(f->c, al));
    }
}
