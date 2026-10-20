#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

int volume_clamp(long v) {
    return v < VOLUME_MIN ? VOLUME_MIN : v > VOLUME_MAX ? VOLUME_MAX : (int)v;
}
float volume_gain(int percent) {
    return (float)volume_clamp(percent) / 100.0f;
}
void settings_defaults(Settings *s) {
    s->bgm_volume = VOLUME_DEFAULT_BGM;
    s->se_volume = VOLUME_DEFAULT_SE;
}
void settings_sanitize(Settings *s) {
    s->bgm_volume = volume_clamp(s->bgm_volume);
    s->se_volume = volume_clamp(s->se_volume);
}

/* "  42  " -> 42 (範囲内に丸める)。ちょうど 1 つの整数トークンでないものは拒否する。 */
static int parse_volume(const char *s, int *out) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s) {
        return 0;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r') {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (errno == ERANGE) {
        v = v < 0 ? VOLUME_MIN : VOLUME_MAX;
    }
    *out = volume_clamp(v);
    return 1;
}

SettingsResult settings_parse(const char *text, Settings *out) {
    Settings s;
    settings_defaults(&s);
    int ok = 1, header = 0, got_bgm = 0, got_se = 0;
    if (!text) {
        *out = s;
        return SETTINGS_ERR_FORMAT;
    }
    char line[96];
    const char *p = text;
    int first = 1;
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '\n') {
            n++;
        }
        size_t c = n < sizeof line - 1 ? n : sizeof line - 1; /* 長すぎる行は切り捨てる。どのみち有効ではない */
        memcpy(line, p, c);
        line[c] = 0;
        p += n;
        if (*p == '\n') {
            p++;
        }
        char *l = line;
        while (*l == ' ' || *l == '\t') {
            l++;
        }
        if (!*l || *l == '\r' || *l == '#') {
            continue;
        }
        if (first) { /* 最初の実質的な行はヘッダでなければならない */
            first = 0;
            int ver = 0;
            char extra;
            if (sscanf(l, "LOOTSMITH_SETTINGS %d %c", &ver, &extra) == 1 && ver == SETTINGS_VERSION) {
                header = 1;
                continue;
            }
            settings_defaults(out);
            return SETTINGS_ERR_FORMAT;
        }
        char key[32];
        int pos = 0;
        if (sscanf(l, "%31s %n", key, &pos) < 1) {
            continue;
        }
        if (!strcmp(key, "bgm_volume")) {
            if (parse_volume(l + pos, &s.bgm_volume)) {
                got_bgm = 1;
            } else {
                s.bgm_volume = VOLUME_DEFAULT_BGM;
                ok = 0;
            }
        } else if (!strcmp(key, "se_volume")) {
            if (parse_volume(l + pos, &s.se_volume)) {
                got_se = 1;
            } else {
                s.se_volume = VOLUME_DEFAULT_SE;
                ok = 0;
            }
        }
        /* 未知のキー: 無視する (より新しいビルドが書いた可能性がある) */
    }
    if (!header || !got_bgm || !got_se) {
        ok = 0;
    }
    settings_sanitize(&s);
    *out = s;
    return ok ? SETTINGS_OK : SETTINGS_ERR_FORMAT;
}

SettingsResult settings_load_file(const char *path, Settings *out) {
    settings_defaults(out);
    if (!path) {
        return SETTINGS_ERR_IO;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return SETTINGS_ERR_IO;
    }
    char buf[1025];
    size_t n = fread(buf, 1, sizeof buf - 1, f); /* 設定ファイルは極小: 1 KB より多くは読まない */
    int err = ferror(f);
    fclose(f);
    if (err) {
        return SETTINGS_ERR_IO;
    }
    buf[n] = 0;
    if (memchr(buf, 0, n)) {
        return SETTINGS_ERR_FORMAT; /* バイナリのゴミ */
    }
    return settings_parse(buf, out);
}

static int replace_file(const char *tmp, const char *path) {
#if defined(_WIN32)
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? 0 : -1;
#else
    return rename(tmp, path);
#endif
}

SettingsResult settings_save_file(const Settings *s, const char *path) {
    char tmp[512];
    if (!s || !path || snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
        return SETTINGS_ERR_IO;
    }
    Settings v = *s;
    settings_sanitize(&v); /* 範囲外の値は決して書き出さない */
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        return SETTINGS_ERR_IO;
    }
    int ok = fprintf(f, "LOOTSMITH_SETTINGS %d\nbgm_volume %d\nse_volume %d\n", SETTINGS_VERSION, v.bgm_volume,
                     v.se_volume) > 0;
    if (ok && fflush(f) != 0) {
        ok = 0;
    }
    if (fclose(f) != 0) {
        ok = 0;
    }
    if (!ok) {
        remove(tmp);
        return SETTINGS_ERR_IO;
    }
    if (replace_file(tmp, path) != 0) {
        remove(tmp);
        return SETTINGS_ERR_IO;
    }
    return SETTINGS_OK;
}
