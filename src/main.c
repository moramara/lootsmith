/* LOOT SMITH - エントリーポイント: ウィンドウ、仮想 640x360 画面、メインループ。 */
#include "ui/ui.h"
#include "audio/audio.h"
#include "audio/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

int main(int argc, char **argv) {
    int demo = -1, max_frames = 0;
    const char *shot = NULL, *save_path = "lootsmith.sav", *settings_path = "lootsmith.cfg";
    bool audio_log = false;
    uint32_t seed = (uint32_t)time(NULL);
    bool seed_given = false;
    for (int i = 1; i < argc; i++) { /* デバッグ/テスト用スイッチ */
        if (!strcmp(argv[i], "--demo") && i + 1 < argc) {
            demo = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            shot = argv[++i];
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
            seed_given = true;
        } else if (!strcmp(argv[i], "--save") && i + 1 < argc) {
            save_path = argv[++i];
        } else if (!strcmp(argv[i], "--settings") && i + 1 < argc) {
            settings_path = argv[++i];
        } else if (!strcmp(argv[i], "--audio-log")) {
            audio_log = true; /* すべての音声イベントを出力する ("[audio] SE coin") */
        } else {
            printf("usage: %s [--seed N] [--save FILE] [--settings FILE] [--audio-log]\n", argv[0]);
            return strcmp(argv[i], "--help") ? 1 : 0;
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(VW * 2, VH * 2, "LOOT SMITH");
    SetWindowMinSize(VW, VH);
    SetExitKey(KEY_NULL); /* Esc は「戻る」であり、「終了」ではない */
    SetTargetFPS(60);
    SetRandomSeed(seed);

    RenderTexture2D target = LoadRenderTexture(VW, VH);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT); /* 最近傍補間 = くっきりしたピクセル */

    Settings settings; /* 音量: ファイルがない/壊れている場合は既定値になるだけ */
    settings_load_file(settings_path, &settings);
    audio_set_trace(audio_log);
    audio_init(&settings); /* デバイスもファイルもない: ゲームは単に無音で動く */

    App app;
    app_init(&app, seed, save_path);
    app_set_settings(&app, &settings, settings_path);
    app.seed_fixed = seed_given; /* NEW GAME はちょうどこのシードから始まる */
    if (demo >= 0) {
        app_demo(&app, demo);
    }

    bool integer_scale = false;
    int frame = 0;
    while (!WindowShouldClose() && !app.quit) {
        if (IsKeyPressed(KEY_F11)) {
            ToggleBorderlessWindowed();
        }
        if (IsKeyPressed(KEY_F10)) {
            integer_scale = !integer_scale;
        }

        /* レターボックスで拡大する。マウスは仮想座標に戻して扱う */
        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = fminf(sw / VW, sh / VH);
        if (integer_scale && scale >= 1.0f) {
            scale = floorf(scale);
        }
        float ox = (sw - VW * scale) * 0.5f, oy = (sh - VH * scale) * 0.5f;
        Vector2 m = GetMousePosition();
        Vector2 vm = {(m.x - ox) / scale, (m.y - oy) / scale};
        bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        float dt = GetFrameTime();
        if (dt > 0.1f) {
            dt = 0.1f;
        }

        audio_update(); /* 毎フレーム BGM のストリームに供給する */
        BeginTextureMode(target);
        ui_frame(&app, vm, click, dt);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, (Rectangle){0, 0, VW, -VH}, (Rectangle){ox, oy, VW * scale, VH * scale},
                       (Vector2){0, 0}, 0, WHITE);
        EndDrawing();

        frame++;
        if (max_frames && frame >= max_frames) {
            if (shot) {
                Image img = LoadImageFromTexture(target.texture);
                ImageFlipVertical(&img);
                ExportImage(img, shot);
                UnloadImage(img);
            }
            break;
        }
    }
    app_shutdown(&app); /* 保存されていない音量変更を保存する */
    audio_shutdown();   /* 停止し、解放し、デバイスを閉じる */
    UnloadRenderTexture(target);
    CloseWindow();
    return 0;
}
