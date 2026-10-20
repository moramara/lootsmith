#include "asset_path.h"
#include <stdio.h>

static int add_candidate(char out[][ASSET_PATH_LEN], int n, int max, const char *fmt, const char *a) {
    if (n >= max) {
        return n;
    }
    int len = snprintf(out[n], ASSET_PATH_LEN, fmt, a);
    return (len >= 0 && len < ASSET_PATH_LEN) ? n + 1 : n; /* 切り詰められた: 捨てる */
}

int asset_root_candidates(const char *exe_dir, const char *env_dir, const char *builtin_dir, char out[][ASSET_PATH_LEN],
                          int max) {
    int n = 0;
    if (!out || max <= 0) {
        return 0;
    }
    if (env_dir && *env_dir) {
        n = add_candidate(out, n, max, "%s", env_dir);
    }
    if (exe_dir) {
        n = add_candidate(out, n, max, "%sassets", exe_dir);
        n = add_candidate(out, n, max, "%s../assets", exe_dir);
        n = add_candidate(out, n, max, "%s../share/lootsmith/assets", exe_dir);
    }
    n = add_candidate(out, n, max, "%s", "assets");
    if (builtin_dir && *builtin_dir) {
        n = add_candidate(out, n, max, "%s", builtin_dir);
    }
    return n;
}
