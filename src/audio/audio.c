#include "audio.h"
#include "asset_path.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RAYLIB_VERSION_MAJOR) && (RAYLIB_VERSION_MAJOR * 100 + RAYLIB_VERSION_MINOR) < 505
#define IsSoundValid IsSoundReady /* raylib 5.5 未満 */
#define IsMusicValid IsMusicReady
#endif

typedef struct {
    bool device, trace;
    bool bgm_ok, bgm_started;
    Music bgm;
    Sound se[SE_COUNT];
    bool se_ok[SE_COUNT];
    int bgm_pct, se_pct;
    float last_time;
    int loops;
    SfxGate gate;
    char root[ASSET_PATH_LEN]; /* audio/ を含むディレクトリ (asset_root_candidates() の 1 件がちょうど収まる) */
} Audio;
static Audio A;

#define TRACE(...)                                                                                                     \
    do {                                                                                                               \
        if (A.trace) {                                                                                                 \
            printf("[audio] " __VA_ARGS__);                                                                            \
            printf("\n");                                                                                              \
            fflush(stdout);                                                                                            \
        }                                                                                                              \
    } while (0)

void audio_set_trace(bool on) {
    A.trace = on;
}
bool audio_available(void) {
    return A.device;
}
int audio_bgm_loops(void) {
    return A.loops;
}
bool audio_bgm_playing(void) {
    return A.device && A.bgm_ok && IsMusicStreamPlaying(A.bgm);
}

/* ---- assets はどこにあるか。単純に「作業ディレクトリ」とはしない: audio/<BGM_FILE> を持つ最初の
 *      ディレクトリを採用する。順序は次のとおり:
 *        1. $LOOTSMITH_ASSETS                      明示的な上書き
 *        2. <exe dir>/assets                       プロジェクトディレクトリでの make
 * ビルド、またはバイナリの隣に展開した zip
 *        3. <exe dir>/../assets                    ソースツリー内の CMake ビルドディレクトリ (build/lootsmith)
 *        4. <exe dir>/../share/lootsmith/assets    `make install` / `cmake --install` 後 (<prefix>/bin/lootsmith)
 *        5. ./assets                               プロジェクトディレクトリから起動した場合
 *        6. LOOTSMITH_ASSET_DIR                    コンパイル時の既定値 (ビルド定義が設定する) */
static bool has_bgm(const char *root) {
    char p[640];
    if (snprintf(p, sizeof p, "%s/audio/%s", root, BGM_FILE) >= (int)sizeof p) {
        return false;
    }
    return FileExists(p);
}
static bool find_assets(void) {
    char cand[ASSET_CANDIDATE_MAX][ASSET_PATH_LEN];
    int n = asset_root_candidates(GetApplicationDirectory(), getenv("LOOTSMITH_ASSETS"), ASSET_BUILTIN_DIR, cand,
                                  ASSET_CANDIDATE_MAX);
    for (int i = 0; i < n; i++) {
        if (has_bgm(cand[i])) {
            memcpy(A.root, cand[i], strlen(cand[i]) + 1); /* どの候補も ASSET_PATH_LEN より短い */
            return true;
        }
    }
    return false;
}

static void apply_volumes(void) {
    if (!A.device) {
        return;
    }
    if (A.bgm_ok) {
        SetMusicVolume(A.bgm, volume_gain(A.bgm_pct));
    }
    for (int i = 0; i < SE_COUNT; i++) {
        if (A.se_ok[i]) {
            SetSoundVolume(A.se[i], volume_gain(A.se_pct));
        }
    }
}

bool audio_init(const Settings *s) {
    if (A.device) {
        return true;
    }
    Settings v;
    if (s) {
        v = *s;
    } else {
        settings_defaults(&v);
    }
    settings_sanitize(&v);
    A.bgm_pct = v.bgm_volume;
    A.se_pct = v.se_volume;
    sfx_gate_init(&A.gate);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        TRACE("init: no audio device - running silent");
        return false;
    }
    A.device = true;
    int loaded = 0;
    if (!find_assets()) {
        TraceLog(LOG_WARNING, "AUDIO: assets/audio not found (set LOOTSMITH_ASSETS) - running silent");
        TRACE("init: assets not found");
    } else {
        TraceLog(LOG_INFO, "AUDIO: assets at %s", A.root);
        char p[640];
        for (int i = 0; i < SE_COUNT; i++) {
            snprintf(p, sizeof p, "%s/audio/se/%s", A.root, SE_DEFS[i].file);
            A.se[i] = LoadSound(p);
            A.se_ok[i] = IsSoundValid(A.se[i]);
            if (A.se_ok[i]) {
                loaded++;
            } else {
                TraceLog(LOG_WARNING, "AUDIO: could not load %s", p);
            }
        }
        snprintf(p, sizeof p, "%s/audio/%s", A.root, BGM_FILE);
        A.bgm = LoadMusicStream(p);
        A.bgm_ok = IsMusicValid(A.bgm);
        if (!A.bgm_ok) {
            TraceLog(LOG_WARNING, "AUDIO: could not load %s", p);
        }
    }
    apply_volumes();
    if (A.bgm_ok) {           /* ちょうど 1 回だけ開始する。画面遷移では触れない */
        A.bgm.looping = true; /* raylib は Stop/Play なしでバッファを先頭から埋め直す: 途切れも音量のリセットもない */
        PlayMusicStream(A.bgm);
        A.bgm_started = true;
        A.last_time = 0;
        A.loops = 0;
    }
    TRACE("init: device=ok bgm=%s se=%d/%d volumes bgm=%d se=%d", A.bgm_ok ? "ok" : "missing", loaded, SE_COUNT,
          A.bgm_pct, A.se_pct);
    if (A.bgm_ok) {
        TRACE("bgm start");
    }
    return true;
}

void audio_update(void) {
    if (!A.device || !A.bgm_ok) {
        return;
    }
    UpdateMusicStream(A.bgm);
    float t = GetMusicTimePlayed(A.bgm);
    if (t + 1.0f < A.last_time) {
        A.loops++;
        TRACE("bgm loop %d", A.loops);
    }
    A.last_time = t;
    if (A.bgm_started && !IsMusicStreamPlaying(A.bgm)) {
        PlayMusicStream(A.bgm);
        TRACE("bgm resumed (stream had stopped)");
    }
}

void audio_set_volumes(int bgm_percent, int se_percent) {
    A.bgm_pct = volume_clamp(bgm_percent);
    A.se_pct = volume_clamp(se_percent);
    apply_volumes();
    TRACE("volumes bgm=%d se=%d", A.bgm_pct, A.se_pct);
}

void audio_play(SeId id) {
    if (id < 0 || id >= SE_COUNT) {
        return;
    }
    if (!A.device || !A.se_ok[id]) {
        TRACE("SE %s (not played: %s)", SE_DEFS[id].name, A.device ? "not loaded" : "no device");
        return;
    }
    if (A.se_pct <= 0) {
        TRACE("SE %s (muted)", SE_DEFS[id].name);
        return;
    }
    if (!sfx_gate_allow(&A.gate, id, GetTime())) {
        TRACE("SE %s (suppressed: too soon)", SE_DEFS[id].name);
        return;
    }
    PlaySound(A.se[id]);
    TRACE("SE %s", SE_DEFS[id].name);
}

void audio_shutdown(void) {
    if (!A.device) {
        memset(&A, 0, sizeof A);
        return;
    }
    if (A.bgm_ok) {
        StopMusicStream(A.bgm);
        UnloadMusicStream(A.bgm);
    }
    for (int i = 0; i < SE_COUNT; i++) {
        if (A.se_ok[i]) {
            StopSound(A.se[i]);
            UnloadSound(A.se[i]);
        }
    }
    CloseAudioDevice();
    TRACE("shutdown");
    bool t = A.trace;
    memset(&A, 0, sizeof A);
    A.trace = t;
}
