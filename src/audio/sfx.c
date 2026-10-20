#include "sfx.h"

const SeDef SE_DEFS[SE_COUNT] = {
    [SE_UI_CLICK] = {"ui_click", "ui_click.wav", 1, 30},
    [SE_UI_NAV] = {"ui_nav", "ui_nav.wav", 1, 30},
    [SE_UI_BACK] = {"ui_back", "ui_back.wav", 1, 30},
    [SE_ERROR] = {"error", "error.wav", 3, 80},
    [SE_COIN] = {"coin", "coin.wav", 4, 60},
    [SE_UPGRADE] = {"upgrade", "upgrade.wav", 4, 60},
    [SE_CRAFT] = {"craft", "craft.wav", 4, 60},
    [SE_CRAFT_RARE] = {"craft_rare", "craft_rare.wav", 5, 60},
    [SE_SELL] = {"sell", "sell.wav", 4, 60},
    [SE_SHOP_OPEN] = {"shop_open", "shop_open.wav", 4, 200},
    [SE_DAY_START] = {"day_start", "day_start.wav", 4, 200},
    [SE_CONTRACT] = {"contract", "contract.wav", 6, 400},
    [SE_RANK_UP] = {"rank_up", "rank_up.wav", 7, 400},
    [SE_CLEAR] = {"clear", "clear.wav", 8, 800},
    [SE_GAMEOVER] = {"gameover", "gameover.wav", 8, 800},
};

void sfx_queue_init(SfxQueue *q) {
    q->pending = SE_NONE;
}
void sfx_request(SfxQueue *q, SeId id) {
    if (id < 0 || id >= SE_COUNT) {
        return;
    }
    if (q->pending == SE_NONE || SE_DEFS[id].priority > SE_DEFS[q->pending].priority) {
        q->pending = id;
    }
}
SeId sfx_take(SfxQueue *q) {
    SeId r = (SeId)q->pending;
    q->pending = SE_NONE;
    return r;
}

void sfx_gate_init(SfxGate *g) {
    for (int i = 0; i < SE_COUNT; i++) {
        g->last[i] = 0;
        g->used[i] = false;
    }
}
bool sfx_gate_allow(SfxGate *g, SeId id, double now) {
    if (id < 0 || id >= SE_COUNT) {
        return false;
    }
    if (g->used[id] && now >= g->last[id] && (now - g->last[id]) * 1000.0 < SE_DEFS[id].min_gap_ms) {
        return false;
    }
    g->used[id] = true;
    g->last[id] = now;
    return true;
}
