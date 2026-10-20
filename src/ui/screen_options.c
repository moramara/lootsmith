#include "ui_internal.h"
#include <stdio.h>
#include <math.h>

/* =====================================================================
 * オプション: 音量 (下の screen_options を参照)。設定ファイルは、変更が確定したとき
 * (マウスを離した、またはキーボード操作が しばらく止まった)、OPTIONS
 * を離れるとき、終了時に書き込む。ドラッグ中の毎フレームには決して書かない。
 * ===================================================================== */
enum { OPT_BGM, OPT_SE, OPT_BACK, OPT_ROWS };

void save_settings_now(App *a) {
    if (!a->settings_dirty) {
        return;
    }
    a->settings_dirty = false;
    a->settings_idle = 0;
    if (!a->settings_path) {
        return;
    }
    if (settings_save_file(&a->settings, a->settings_path) !=
        SETTINGS_OK) { /* ゲームは続く。プレイヤーに知らせるだけ (エラー音は鳴らさない) */
        snprintf(a->msg, sizeof a->msg, "Could not save the settings file.");
        a->msg_good = false;
        a->msg_t = 3.5f;
    }
}

/* 音量を、再生中の BGM と読み込み済みの SE へ即座に適用する。変化があれば true を返す */
static bool set_volume(App *a, int row, int v) {
    int *p = row == OPT_BGM ? &a->settings.bgm_volume : &a->settings.se_volume;
    v = volume_clamp(v);
    if (*p == v) {
        return false;
    }
    *p = v;
    audio_set_volumes(a->settings.bgm_volume, a->settings.se_volume);
    a->settings_dirty = true;
    a->settings_idle = 0;
    return true;
}

void open_options(App *a) {
    if (a->screen == SCR_OPTIONS) {
        return;
    }
    a->options_return = a->screen; /* タイトルまたはゲーム画面: 覚えておき、BACK でそこへ戻る */
    a->opt_sel = OPT_BGM;
    a->opt_drag = -1;
    goto_screen(a, SCR_OPTIONS);
}

static void close_options(App *a) {
    save_settings_now(a);
    int r = a->options_return;
    if (r < 0 || r >= SCR_COUNT || r == SCR_OPTIONS) {
        r = SCR_TITLE;
    }
    a->opt_drag = -1;
    goto_screen(a, r);
}

/* ---------- OPTIONS: 音量スライダー 2 本 (BGM / SE) + BACK ---------- */
#define OPT_SLX 140 /* スライダーのトラック: x、幅 (仮想ピクセル) */

#define OPT_SLW 280

static bool key_rep(App *a, int key) { /* 押下またはオートリピート。1 フレームにつき 1 つの操作 */
    if (a->consumed || !(IsKeyPressed(key) || IsKeyPressedRepeat(key))) {
        return false;
    }
    a->consumed = true;
    return true;
}

void screen_options(App *a) {
    DrawRectangle(0, 0, VW, VH, C_BG);
    panel(120, 24, 400, 312, "OPTIONS");
    txt("SOUND", 140, 46, C_DIM);
    if (key_hit(a, KEY_F2)) {
        close_options(a);
        return;
    } /* F2 でトグルする: 開いたキーが閉じるキーでもある */
    if (key_hit(a, KEY_DOWN)) {
        a->opt_sel = (a->opt_sel + 1) % OPT_ROWS;
    }
    if (key_hit(a, KEY_UP)) {
        a->opt_sel = (a->opt_sel + OPT_ROWS - 1) % OPT_ROWS;
    }

    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    float wheel = GetMouseWheelMove();
    bool mouse_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    static const char *const names[2] = {T_OPT_BGM, T_OPT_SE};
    for (int row = 0; row < 2; row++) {
        int y = 62 + row * 70, v = row == OPT_BGM ? a->settings.bgm_volume : a->settings.se_volume;
        bool sel = a->opt_sel == row, changed = false;
        /* キーボード (選択行のみ) */
        if (sel) {
            int step = shift ? 1 : 5;
            if (key_rep(a, KEY_LEFT)) {
                changed |= set_volume(a, row, v - step);
            }
            if (key_rep(a, KEY_RIGHT)) {
                changed |= set_volume(a, row, v + step);
            }
            if (key_hit(a, KEY_HOME)) {
                changed |= set_volume(a, row, VOLUME_MIN);
            }
            if (key_hit(a, KEY_END)) {
                changed |= set_volume(a, row, VOLUME_MAX);
            }
        }
        /* マウス: 行の上でのホイール、スライダー上の押下でドラッグ開始 (ボタンを離すと終わる) */
        if (wheel != 0 && in_rect(a->mouse, 130, y, 380, 60)) {
            changed |= set_volume(a, row, v + (wheel > 0 ? 5 : -5));
            a->opt_sel = row;
            sel = true;
        }
        if (a->click && !a->consumed && in_rect(a->mouse, 130, y, 380, 60)) {
            a->opt_sel = row;
            sel = true;
            a->consumed = true;
            if (in_rect(a->mouse, OPT_SLX - 8, y + 18, OPT_SLW + 16, 32)) {
                a->opt_drag = row;
            }
        }
        if (a->opt_drag == row) {
            if (mouse_down) {
                changed |= set_volume(a, row, (int)floorf((a->mouse.x - OPT_SLX) * 100.0f / OPT_SLW + 0.5f));
                a->settings_idle = 0;
            } else { /* 離した: 確定 (1 回保存) + プレイヤーに SE の音量を聞かせる */
                a->opt_drag = -1;
                save_settings_now(a);
                if (row == OPT_SE) {
                    sfx_request(&a->sfx, SE_UI_CLICK);
                }
            }
        }
        if (changed && row == OPT_SE && a->opt_drag < 0 && a->opt_preview_t <= 0) {
            sfx_request(&a->sfx, SE_UI_CLICK);
            a->opt_preview_t = 0.12f;
        }
        v = row == OPT_BGM ? a->settings.bgm_volume : a->settings.se_volume;

        DrawRectangle(130, y, 380, 60, sel ? C_PANEL2 : C_PANEL);
        DrawRectangleLines(130, y, 380, 60, sel ? C_GOLD : C_BORDER_D);
        txt(names[row], 140, y + 8, sel ? C_GOLD : C_TEXT);
        DrawRectangle(OPT_SLX, y + 28, OPT_SLW, 12, C_BLACK);
        DrawRectangle(OPT_SLX + 1, y + 29, (OPT_SLW - 2) * v / 100, 10,
                      v == 0           ? C_BTN_OFF
                      : row == OPT_BGM ? C_GOLD
                                       : C_ORANGE);
        DrawRectangleLines(OPT_SLX, y + 28, OPT_SLW, 12, C_BORDER);
        for (int t = 0; t <= 4; t++) {
            DrawRectangle(OPT_SLX + (OPT_SLW - 1) * t / 4, y + 42, 1, 3, C_DIM); /* 0 / 25 / 50 / 75 / 100 % */
        }
        int hx = OPT_SLX + (OPT_SLW - 6) * v / 100; /* つまみ */
        DrawRectangle(hx, y + 24, 6, 20, a->opt_drag == row ? C_GOLD : C_TEXT);
        DrawRectangleLines(hx, y + 24, 6, 20, C_BLACK);
        txt_big(TextFormat("%d%%", v), 436, y + 24, 20, v == 0 ? C_DIM : C_TEXT);
        if (v == 0) {
            txt(T_OPT_MUTE, 436, y + 46, C_BAD);
        }
    }

    txt_fit(T_OPT_HINT1, 140, 206, 360, C_DIM);
    txt_fit(T_OPT_HINT2, 140, 218, 360, C_DIM);
    txt_fit(T_OPT_HINT3, 140, 238, 360, C_DIM);
    txt_fit(T_OPT_HINT4, 140, 250, 360, C_DIM);
    if (!audio_available()) {
        txt_fit(T_OPT_NODEVICE, 140, 270, 360, C_BAD);
    }
    if (a->msg_t > 0 && a->msg[0]) {
        txt_fit(a->msg, 140, 282, 360, a->msg_good ? C_GOOD : C_BAD);
    }
    if (btn(a, 140, 300, 140, 24, T_BACK, BF_BACK | (a->opt_sel == OPT_BACK ? BF_SEL : 0), KEY_ESCAPE)) {
        close_options(a);
        return;
    }
    if (a->opt_sel == OPT_BACK && key_hit(a, KEY_ENTER)) {
        sfx_request(&a->sfx, SE_UI_BACK);
        close_options(a);
    }
}
