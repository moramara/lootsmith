#include "simulation.h"
#include "sim_internal.h"

int guild_rank(int done) {
    int r = RANK_APPRENTICE;
    for (int i = 0; i < RANK_COUNT; i++) {
        if (done >= GUILD_RANKS[i].need_done) {
            r = i;
        }
    }
    return r;
}

const char *guild_rank_name(int rank) {
    return rank >= 0 && rank < RANK_COUNT ? GUILD_RANKS[rank].name : "?";
}

int guild_rank_need(int rank) {
    return rank >= 0 && rank < RANK_COUNT ? GUILD_RANKS[rank].need_done : -1;
}

int guild_next_rank_remaining(int done) {
    int r = guild_rank(done);
    if (r >= RANK_COUNT - 1) {
        return -1;
    }
    return GUILD_RANKS[r + 1].need_done - (done < 0 ? 0 : done);
}

int guild_next_blueprint(int done) {
    int r = guild_rank(done);
    if (r >= RANK_COUNT - 1) {
        return -1;
    }
    for (int b = 0; b < BP_COUNT; b++) {
        if (BLUEPRINTS[b].rank == r + 1) {
            return b; /* 次のランクがもたらす設計図 */
        }
    }
    return -1;
}

int blueprint_unlocked(int done, int bp) {
    return bp >= 0 && bp < BP_COUNT && guild_rank(done) >= BLUEPRINTS[bp].rank;
}

int blueprint_of_equip(int eq) {
    for (int b = 0; b < BP_COUNT; b++) {
        if (BLUEPRINTS[b].equip == eq) {
            return b;
        }
    }
    return -1;
}

int equip_unlocked(int done, int eq) {
    if (eq < 0 || eq >= EQ_COUNT) {
        return 0;
    }
    int b = blueprint_of_equip(eq);
    return b < 0 ? 1 : blueprint_unlocked(done, b);
}

int customer_unlocked(int done, int ct) {
    if (ct < 0 || ct >= CUST_TYPE_COUNT) {
        return 0;
    }
    return equip_unlocked(done, CUSTOMER_DEFS[ct].wants_eq);
}
