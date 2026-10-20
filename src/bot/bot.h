/* LOOT SMITH - 観戦用 BOT。シミュレーション API だけを使い、raylib には依存しない。 */
#ifndef LOOTSMITH_BOT_H
#define LOOTSMITH_BOT_H

#include "core/simulation.h"
#include <stdbool.h>
#include <stdint.h>

#define BOT_LOG_COUNT 8
#define BOT_LOG_LEN 80

typedef struct {
    GameState game;
    uint32_t seed;
    int recipe_rotation;
    int skip_mask;
    int customer_pos;
    int log_count;
    char log[BOT_LOG_COUNT][BOT_LOG_LEN]; /* log[0] が最新 */
    bool settlement_logged;
    bool finished;
} BotRunner;

void bot_init(BotRunner *bot, uint32_t seed);
/* 1 回につき、準備・客への応対・日次精算のいずれか 1 段階を進める。 */
void bot_step(BotRunner *bot);

#endif
