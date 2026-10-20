/* raylib を必要としない音声まわりのテスト: 設定ファイル、音量の対応付け、効果音 ID と「1 イベントにつき 1 音」の
 * ロジック、同梱のアセットファイル (WAV 形式、クリッピングなし、端にクリック音なし、BGM の存在、README の網羅性)。
 * `make test` / CTest でビルド・実行される。LOOTSMITH_ASSET_DIR は assets/ ディレクトリを指す。 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../src/audio/asset_path.h"
#include "../src/audio/settings.h"
#include "../src/audio/sfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef LOOTSMITH_ASSET_DIR
#define LOOTSMITH_ASSET_DIR "assets"
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

/* ------------------------------------------------------------------------------------------------ 設定 */
static void test_volume_math(void) {
    CHECK_EQ(volume_clamp(-5), 0);
    CHECK_EQ(volume_clamp(0), 0);
    CHECK_EQ(volume_clamp(55), 55);
    CHECK_EQ(volume_clamp(100), 100);
    CHECK_EQ(volume_clamp(101), 100);
    CHECK_EQ(volume_clamp(1000000000L), 100);
    CHECK(volume_gain(0) == 0.0f); /* 0 % はちょうど無音 */
    CHECK(volume_gain(100) == 1.0f);
    CHECK(fabsf(volume_gain(50) - 0.5f) < 1e-6f);
    CHECK(volume_gain(-10) == 0.0f && volume_gain(250) == 1.0f);
    Settings s;
    settings_defaults(&s);
    CHECK_EQ(s.bgm_volume, 35);
    CHECK_EQ(s.se_volume, 65);
}

static void test_settings_parse(void) {
    Settings s;
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 20\nse_volume 80\n", &s), SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 20);
    CHECK_EQ(s.se_volume, 80);
    /* 独立性: 一方を 0 にしても、もう一方には影響しない */
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 0\nse_volume 100\n", &s), SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 0);
    CHECK_EQ(s.se_volume, 100);
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 100\nse_volume 0\n", &s), SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 100);
    CHECK_EQ(s.se_volume, 0);
    /* 範囲外の値は丸められ、ゴミの値として拒否されることはない */
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 250\nse_volume -7\n", &s), SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 100);
    CHECK_EQ(s.se_volume, 0);
    CHECK_EQ(settings_parse(
                 "LOOTSMITH_SETTINGS 1\nbgm_volume 99999999999999999999999\nse_volume -99999999999999999999999\n", &s),
             SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 100);
    CHECK_EQ(s.se_volume, 0);
    /* CRLF、空行、コメント、余分な空白、未知のキー、順序の違い、重複は最後が優先 */
    CHECK_EQ(settings_parse("# my settings\r\n\r\nLOOTSMITH_SETTINGS 1\r\n  se_volume   42  \r\nfuture_key "
                            "7\r\nbgm_volume 9\r\nbgm_volume 11\r\n",
                            &s),
             SETTINGS_OK);
    CHECK_EQ(s.bgm_volume, 11);
    CHECK_EQ(s.se_volume, 42);
    /* 壊れたファイル: 必ず有効な値が出てくる */
    const char *bad[] = {"",
                         "\n\n",
                         "garbage",
                         "LOOTSMITH_SETTINGS 2\nbgm_volume 5\nse_volume 5\n",
                         "LOOTSMITH_SAVE 3\nbgm_volume 5\n",
                         "LOOTSMITH_SETTINGS\nbgm_volume 5\nse_volume 5\n",
                         "LOOTSMITH_SETTINGS 1 junk\nbgm_volume 5\nse_volume 5\n",
                         "bgm_volume 5\nse_volume 5\nLOOTSMITH_SETTINGS 1\n"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        s.bgm_volume = 77;
        s.se_volume = 88;
        CHECK_EQ(settings_parse(bad[i], &s), SETTINGS_ERR_FORMAT);
        CHECK_EQ(s.bgm_volume, 35);
        CHECK_EQ(s.se_volume, 65); /* 古い値や読みかけの値ではなく、既定値になる */
    }
    /* 壊れたキーは、そのキーの既定値にだけ戻る。正しい方は保持される */
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume loud\nse_volume 12\n", &s), SETTINGS_ERR_FORMAT);
    CHECK_EQ(s.bgm_volume, 35);
    CHECK_EQ(s.se_volume, 12);
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 12abc\nse_volume 3.5\n", &s), SETTINGS_ERR_FORMAT);
    CHECK_EQ(s.bgm_volume, 35);
    CHECK_EQ(s.se_volume, 65);
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume\nse_volume 4\n", &s), SETTINGS_ERR_FORMAT);
    CHECK_EQ(s.bgm_volume, 35);
    CHECK_EQ(s.se_volume, 4);
    CHECK_EQ(settings_parse("LOOTSMITH_SETTINGS 1\nbgm_volume 30\n", &s), SETTINGS_ERR_FORMAT); /* se が欠けている */
    CHECK_EQ(s.bgm_volume, 30);
    CHECK_EQ(s.se_volume, 65);
    CHECK_EQ(settings_parse(NULL, &s), SETTINGS_ERR_FORMAT);
    CHECK_EQ(s.bgm_volume, 35);
    /* 非常に長い行でも何もあふれないこと */
    char *big = malloc(5000);
    memset(big, 'x', 4999);
    big[4999] = 0;
    CHECK_EQ(settings_parse(big, &s), SETTINGS_ERR_FORMAT);
    CHECK(s.bgm_volume >= 0 && s.bgm_volume <= 100);
    free(big);
}

static char *read_all(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    char *b = malloc(4096);
    size_t n = fread(b, 1, 4095, f);
    fclose(f);
    b[n] = 0;
    if (len) {
        *len = n;
    }
    return b;
}
static void test_settings_files(void) {
    const char *path = "test_settings.tmpcfg";
    remove(path);
    Settings s, r;
    /* 初回起動: ファイルなし -> 既定値 + IO ステータス (ゲームはこれを正常として扱う) */
    s.bgm_volume = 1;
    s.se_volume = 2;
    CHECK_EQ(settings_load_file(path, &s), SETTINGS_ERR_IO);
    CHECK_EQ(s.bgm_volume, 35);
    CHECK_EQ(s.se_volume, 65);
    /* さまざまな値 (両方のミュートを含む) での 保存 -> 読み込み の往復 */
    int vals[][2] = {{35, 65}, {0, 0}, {0, 100}, {100, 0}, {100, 100}, {1, 99}, {50, 50}};
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
        s.bgm_volume = vals[i][0];
        s.se_volume = vals[i][1];
        CHECK_EQ(settings_save_file(&s, path), SETTINGS_OK);
        CHECK_EQ(settings_load_file(path, &r), SETTINGS_OK);
        CHECK_EQ(r.bgm_volume, vals[i][0]);
        CHECK_EQ(r.se_volume, vals[i][1]);
    }
    /* ファイルの内容は文書化された形式であり、一時ファイルが残らない */
    s.bgm_volume = 12;
    s.se_volume = 34;
    CHECK_EQ(settings_save_file(&s, path), SETTINGS_OK);
    char *t = read_all(path, NULL);
    CHECK(t && !strcmp(t, "LOOTSMITH_SETTINGS 1\nbgm_volume 12\nse_volume 34\n"));
    free(t);
    FILE *f = fopen("test_settings.tmpcfg.tmp", "rb");
    CHECK(f == NULL);
    if (f) {
        fclose(f);
    }
    /* メモリ上の範囲外の値は、書き込み時に丸められる */
    s.bgm_volume = 400;
    s.se_volume = -3;
    CHECK_EQ(settings_save_file(&s, path), SETTINGS_OK);
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_OK);
    CHECK_EQ(r.bgm_volume, 100);
    CHECK_EQ(r.se_volume, 0);
    /* ディスク上の壊れたファイル -> 既定値、クラッシュしない */
    f = fopen(path, "wb");
    fputs("\x01\x02\x03 not a settings file \xff\xfe", f);
    fclose(f);
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_ERR_FORMAT);
    CHECK_EQ(r.bgm_volume, 35);
    CHECK_EQ(r.se_volume, 65);
    {
        static const char nul[] = "LOOTSMITH_SETTINGS 1\nbgm_volume 5\n\0\0se_volume 5\n";
        f = fopen(path, "wb");
        fwrite(nul, 1, sizeof nul - 1, f);
        fclose(f);
    }
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_ERR_FORMAT);
    CHECK_EQ(r.bgm_volume, 35);
    CHECK_EQ(r.se_volume, 65);
    f = fopen(path, "wb");
    fclose(f); /* 空のファイル */
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_ERR_FORMAT);
    CHECK_EQ(r.bgm_volume, 35);
    /* 巨大なファイルは 1 KB までしか読まない */
    f = fopen(path, "wb");
    fputs("LOOTSMITH_SETTINGS 1\nbgm_volume 10\nse_volume 20\n", f);
    for (int i = 0; i < 4000; i++) {
        fputs("# padding padding padding\n", f);
    }
    fclose(f);
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_OK);
    CHECK_EQ(r.bgm_volume, 10);
    CHECK_EQ(r.se_volume, 20);
    /* 書き込みの失敗は報告されるが致命的ではなく、既存のファイルには触れない */
    s.bgm_volume = 7;
    s.se_volume = 8;
    CHECK_EQ(settings_save_file(&s, "no_such_dir_xyz/lootsmith.cfg"), SETTINGS_ERR_IO);
    CHECK_EQ(settings_save_file(&s, NULL), SETTINGS_ERR_IO);
    CHECK_EQ(settings_load_file(NULL, &r), SETTINGS_ERR_IO);
    CHECK_EQ(r.bgm_volume, 35);
    char longpath[700];
    memset(longpath, 'a', sizeof longpath - 1);
    longpath[sizeof longpath - 1] = 0;
    CHECK_EQ(settings_save_file(&s, longpath), SETTINGS_ERR_IO);
    CHECK_EQ(settings_load_file(path, &r), SETTINGS_OK);
    CHECK_EQ(r.bgm_volume, 10); /* 変更されていない */
#ifndef _WIN32
    /* 読み取り専用ディレクトリ: 保存はきれいに失敗する (どこにでも書き込める root で実行している場合はスキップ) */
    if (geteuid() != 0 && mkdir("ro_test_dir", 0755) == 0) {
        chmod("ro_test_dir", 0555);
        CHECK_EQ(settings_save_file(&s, "ro_test_dir/lootsmith.cfg"), SETTINGS_ERR_IO);
        chmod("ro_test_dir", 0755);
        rmdir("ro_test_dir");
    }
#endif
    remove(path);
}

/* ------------------------------------------------------------------------------------------------ 効果音 ID */
static void test_sfx_tables(void) {
    for (int i = 0; i < SE_COUNT; i++) {
        CHECK(SE_DEFS[i].name && SE_DEFS[i].name[0]);
        CHECK(SE_DEFS[i].file && strstr(SE_DEFS[i].file, ".wav"));
        CHECK(SE_DEFS[i].priority > 0);
        CHECK(SE_DEFS[i].min_gap_ms > 0);
        for (int j = i + 1; j < SE_COUNT; j++) {
            CHECK(strcmp(SE_DEFS[i].name, SE_DEFS[j].name));
            CHECK(strcmp(SE_DEFS[i].file, SE_DEFS[j].file));
        }
    }
    /* UI が前提とする順序: チック < エラー < 通常のイベント音 < ファンファーレ */
    CHECK(SE_DEFS[SE_UI_CLICK].priority < SE_DEFS[SE_ERROR].priority);
    CHECK(SE_DEFS[SE_ERROR].priority < SE_DEFS[SE_COIN].priority);
    CHECK(SE_DEFS[SE_COIN].priority < SE_DEFS[SE_CRAFT_RARE].priority);
    CHECK(SE_DEFS[SE_CRAFT].priority < SE_DEFS[SE_CRAFT_RARE].priority);
    CHECK(SE_DEFS[SE_CRAFT_RARE].priority < SE_DEFS[SE_CONTRACT].priority);
    CHECK(SE_DEFS[SE_CONTRACT].priority < SE_DEFS[SE_RANK_UP].priority);
    CHECK(SE_DEFS[SE_RANK_UP].priority < SE_DEFS[SE_CLEAR].priority);
}
static void test_sfx_queue(void) {
    SfxQueue q;
    sfx_queue_init(&q);
    CHECK_EQ(sfx_take(&q), SE_NONE); /* 何も要求されていない: 無音 */
    sfx_request(&q, SE_UI_CLICK);
    CHECK_EQ(sfx_take(&q), SE_UI_CLICK);
    CHECK_EQ(sfx_take(&q), SE_NONE); /* 取り出すと空になる: 次のフレームで繰り返さない */
    /* 成功する BUY のクリック: チック音の代わりにコイン音が鳴る */
    sfx_request(&q, SE_UI_CLICK);
    sfx_request(&q, SE_COIN);
    CHECK_EQ(sfx_take(&q), SE_COIN);
    /* 要求の順序は関係しない */
    sfx_request(&q, SE_COIN);
    sfx_request(&q, SE_UI_CLICK);
    CHECK_EQ(sfx_take(&q), SE_COIN);
    /* 失敗した画面遷移: チック + エラー -> エラーだけ */
    sfx_request(&q, SE_UI_NAV);
    sfx_request(&q, SE_ERROR);
    CHECK_EQ(sfx_take(&q), SE_ERROR);
    /* ランクアップする納品: 契約のファンファーレ + ランクのファンファーレ -> 大きい方だけ */
    sfx_request(&q, SE_UI_CLICK);
    sfx_request(&q, SE_CONTRACT);
    sfx_request(&q, SE_RANK_UP);
    CHECK_EQ(sfx_take(&q), SE_RANK_UP);
    sfx_request(&q, SE_RANK_UP);
    sfx_request(&q, SE_CONTRACT);
    CHECK_EQ(sfx_take(&q), SE_RANK_UP);
    /* 同じ優先度: 最初の要求が勝つ。同じ音を 2 回要求しても 1 回だけ鳴る */
    sfx_request(&q, SE_SELL);
    sfx_request(&q, SE_COIN);
    CHECK_EQ(sfx_take(&q), SE_SELL);
    sfx_request(&q, SE_SELL);
    sfx_request(&q, SE_SELL);
    CHECK_EQ(sfx_take(&q), SE_SELL);
    CHECK_EQ(sfx_take(&q), SE_NONE);
    /* 不正な ID は無視される */
    sfx_request(&q, (SeId)-5);
    sfx_request(&q, (SeId)SE_COUNT);
    sfx_request(&q, (SeId)999);
    CHECK_EQ(sfx_take(&q), SE_NONE);
    /* すべての ID は要求でき、そのまま返ってくる */
    for (int i = 0; i < SE_COUNT; i++) {
        sfx_request(&q, (SeId)i);
        CHECK_EQ(sfx_take(&q), i);
    }
}
static void test_sfx_gate(void) {
    SfxGate g;
    sfx_gate_init(&g);
    CHECK(sfx_gate_allow(&g, SE_COIN, 10.0));
    CHECK(!sfx_gate_allow(&g, SE_COIN, 10.01)); /* min_gap 以内の再開始: 抑制される */
    CHECK(sfx_gate_allow(&g, SE_SELL, 10.01));  /* 別の音は独立している */
    CHECK(sfx_gate_allow(&g, SE_COIN, 10.0 + SE_DEFS[SE_COIN].min_gap_ms / 1000.0 + 0.001));
    CHECK(sfx_gate_allow(&g, SE_UI_CLICK, 0.0)); /* 時刻 0 は最初の再生として有効 */
    CHECK(sfx_gate_allow(&g, SE_UI_CLICK, 5.0));
    CHECK(!sfx_gate_allow(&g, (SeId)-1, 1.0));
    CHECK(!sfx_gate_allow(&g, (SeId)SE_COUNT, 1.0));
}

/* ------------------------------------------------------------------------------------------------ アセットの探索 */
/* 回帰: find_assets() には、かつて空の `#ifdef LOOTSMITH_ASSET_DIR` があり、コンパイル時のパスを一度も探索しなかった。
 */
static void test_asset_candidates(void) {
    char c[ASSET_CANDIDATE_MAX][ASSET_PATH_LEN];
    int n = asset_root_candidates("/opt/ls/bin/", "/custom", "/src/assets", c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 6);
    CHECK_EQ(n, ASSET_CANDIDATE_MAX); /* 配列は、すべての候補を入れるのにちょうどの大きさ */
    CHECK(!strcmp(c[0], "/custom"));
    CHECK(!strcmp(c[1], "/opt/ls/bin/assets"));
    CHECK(!strcmp(c[2], "/opt/ls/bin/../assets"));
    CHECK(!strcmp(c[3], "/opt/ls/bin/../share/lootsmith/assets"));
    CHECK(!strcmp(c[4], "assets"));
    CHECK(!strcmp(c[5], "/src/assets")); /* コンパイル時の既定値は最後 */

    /* 未設定 / 空 / 欠けている要素は除かれ、残りは順序を保つ */
    n = asset_root_candidates("/opt/ls/bin/", NULL, NULL, c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 4);
    CHECK(!strcmp(c[0], "/opt/ls/bin/assets"));
    CHECK(!strcmp(c[3], "assets"));
    n = asset_root_candidates("/opt/ls/bin/", "", "", c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 4);
    n = asset_root_candidates(NULL, NULL, "/src/assets", c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 2);
    CHECK(!strcmp(c[0], "assets"));
    CHECK(!strcmp(c[1], "/src/assets"));

    /* ビルド定義はテスト用に LOOTSMITH_ASSET_DIR を定義する。audio.c も同じマクロ経由でそれを読む */
    n = asset_root_candidates(NULL, NULL, ASSET_BUILTIN_DIR, c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 2);
    CHECK(!strcmp(c[n - 1], LOOTSMITH_ASSET_DIR));

    /* `max` を超えて書き込むことはなく、不正な引数でも安全 */
    char small[2][ASSET_PATH_LEN];
    CHECK_EQ(asset_root_candidates("/opt/ls/bin/", "/custom", "/src/assets", small, 2), 2);
    CHECK(!strcmp(small[1], "/opt/ls/bin/assets"));
    CHECK_EQ(asset_root_candidates("/x/", "/custom", "/src/assets", c, 0), 0);
    CHECK_EQ(asset_root_candidates("/x/", "/custom", "/src/assets", NULL, 6), 0);
}

static void test_asset_candidates_long_paths(void) {
    char c[ASSET_CANDIDATE_MAX][ASSET_PATH_LEN];
    static char longdir[2 * ASSET_PATH_LEN];
    memset(longdir, 'd', sizeof longdir - 1);
    longdir[sizeof longdir - 1] = 0;
    longdir[0] = '/';
    /* 長すぎるディレクトリは、決して切り詰めて返してはならない (切り取られたパスは別のディレクトリを指しうる) */
    int n = asset_root_candidates(longdir, longdir, longdir, c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 1);
    CHECK(!strcmp(c[0], "assets"));
    for (int i = 0; i < n; i++) {
        CHECK(strlen(c[i]) < ASSET_PATH_LEN);
    }

    /* ちょうど境界: "<dir>assets" は 511 文字 (収まる)、"<dir>../assets" は 514 文字 (捨てられる) */
    static char edge[ASSET_PATH_LEN + 8];
    size_t want = (ASSET_PATH_LEN - 1) - strlen("assets");
    memset(edge, 'e', want);
    edge[0] = '/';
    edge[want - 1] = '/';
    edge[want] = 0;
    n = asset_root_candidates(edge, NULL, NULL, c, ASSET_CANDIDATE_MAX);
    CHECK_EQ(n, 2);
    CHECK_EQ(strlen(c[0]), ASSET_PATH_LEN - 1);
    CHECK(!strncmp(c[0], edge, want) && !strcmp(c[0] + want, "assets"));
    CHECK(!strcmp(c[1], "assets"));
}

/* ------------------------------------------------------------------------------------------------ アセットファイル */
static uint32_t rd32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned rd16(const unsigned char *p) {
    return p[0] | p[1] << 8;
}

static unsigned char *slurp(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) {
        free(b);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return b;
}
static void test_se_files(void) {
    for (int i = 0; i < SE_COUNT; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/audio/se/%s", LOOTSMITH_ASSET_DIR, SE_DEFS[i].file);
        long n = 0;
        unsigned char *b = slurp(path, &n);
        if (!b) {
            g_checks++;
            g_fails++;
            printf("  FAIL missing sound file %s\n", path);
            continue;
        }
        CHECK(n > 44 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WAVE", 4));
        long pos = 12;
        int fmt_ok = 0;
        unsigned ch = 0;
        uint32_t rate = 0;
        const unsigned char *data = NULL;
        uint32_t dlen = 0;
        while (n >= 8 && pos + 8 <= n) {
            uint32_t len = rd32(b + pos + 4);
            if (!memcmp(b + pos, "fmt ", 4) && len >= 16) {
                fmt_ok = rd16(b + pos + 8) == 1 && rd16(b + pos + 22) == 16;
                ch = rd16(b + pos + 10);
                rate = rd32(b + pos + 12);
            }
            if (!memcmp(b + pos, "data", 4)) {
                data = b + pos + 8;
                dlen = len;
                if (pos + 8 + (long)dlen > n) {
                    dlen = (uint32_t)(n - pos - 8);
                }
                break;
            }
            pos += 8 + len + (len & 1);
        }
        CHECK(fmt_ok); /* 16 ビット PCM */
        CHECK(ch == 1 || ch == 2);
        CHECK_EQ(rate, 44100);
        CHECK(data != NULL && dlen >= 2);
        if (data && fmt_ok && ch) {
            long frames = dlen / (2 * ch);
            double secs = (double)frames / rate;
            CHECK(secs >= 0.03 && secs <= 8.0);
            int peak = 0;
            for (long k = 0; k < frames * ch; k++) {
                int v = abs((int)(int16_t)rd16(data + 2 * k));
                if (v > peak) {
                    peak = v;
                }
            }
            CHECK(peak > 3000);  /* 無音ではない */
            CHECK(peak < 30500); /* ヘッドルーム: クリッピングなし、-0.6 dBFS 未満 */
            int e0 = abs((int)(int16_t)rd16(data)), e1 = abs((int)(int16_t)rd16(data + 2 * (frames * ch - 1)));
            CHECK(e0 <= 40 && e1 <= 40); /* 始まりと終わりがほぼ 0: クリック音が出ない */
            int ui = (i == SE_UI_CLICK || i == SE_UI_NAV || i == SE_UI_BACK);
            if (ui) {
                CHECK(peak < 12000); /* UI のチック音は控えめなまま (< -8.7 dBFS) */
            }
            if (i == SE_ERROR) {
                CHECK(peak < 14000);
            }
        }
        free(b);
    }
}
static void test_bgm_and_docs(void) {
    char path[512];
    long n = 0;
    snprintf(path, sizeof path, "%s/audio/%s", LOOTSMITH_ASSET_DIR, BGM_FILE);
    unsigned char *b = slurp(path, &n);
    CHECK(b != NULL);
    if (b) {
        CHECK(n > 200000 && n < 8000000); /* スタブではなく本物のストリーム。リポジトリに入れられる程度に小さい */
        CHECK(!memcmp(b, "OggS", 4));
        CHECK(memcmp(b + 28, "\x01vorbis", 7) == 0); /* Ogg Vorbis (raylib / stb_vorbis がストリーミングできる) */
        free(b);
    }
    /* 由来: assets/audio/README.md は、同梱のすべてのファイルに言及しなければならない */
    snprintf(path, sizeof path, "%s/audio/README.md", LOOTSMITH_ASSET_DIR);
    long dn = 0;
    unsigned char *d = slurp(path, &dn);
    CHECK(d != NULL);
    if (d) {
        d[dn] = 0;
        CHECK(strstr((char *)d, BGM_FILE) != NULL);
        for (int i = 0; i < SE_COUNT; i++) {
            CHECK(strstr((char *)d, SE_DEFS[i].file) != NULL);
        }
        CHECK(strstr((char *)d, "CC0") != NULL || strstr((char *)d, "License") != NULL ||
              strstr((char *)d, "licen") != NULL);
        free(d);
    }
}

int main(void) {
    test_volume_math();
    test_settings_parse();
    test_settings_files();
    test_sfx_tables();
    test_sfx_queue();
    test_sfx_gate();
    test_asset_candidates();
    test_asset_candidates_long_paths();
    test_se_files();
    test_bgm_and_docs();
    printf("audio / settings: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
