/* プレイヤーに表示する文字列はすべてここにある (ASCII のみ: raylib 組み込みフォントには日本語グリフがない)。
 * 後でローカライズするときは、このファイルを差し替えるか、マクロを検索テーブルに置き換える。
 * 素材・装備・客・アップグレードの名前と説明は core/content_tables.c のデータテーブルから取得する。
 * T_HELP_LINES は、これらを列挙する代わりにマーカー @MATS、@CUSTOMERS、@UPGRADES を使う
 * (展開は ui/screen_help.c の help_build が行う)。
 */
#ifndef TEXT_H
#define TEXT_H
#include "core/simulation.h"

#define T_TITLE "LOOT SMITH"
#define T_SUBTITLE "A BLACKSMITH TYCOON"
#define T_NEW_GAME "NEW GAME [N]"
#define T_CONTINUE "CONTINUE [C]"
#define T_HELP "HELP [H]"
#define T_QUIT "QUIT [Q]"
#define T_BACK "BACK [Esc]"
#define T_OPEN_SHOP "OPEN SHOP [O]"
#define T_RESUME "CUSTOMERS [O]"
#define T_SAVE "SAVE [F5]"
#define T_TO_TITLE "TITLE"
#define T_NEXT_DAY "NEXT DAY [Enter]"
#define T_RESULT "SEE RESULT [Enter]"
#define T_SANDBOX "KEEP PLAYING (SANDBOX)"
#define T_RETRY "NEW CAMPAIGN"
#define T_OPTIONS "OPTIONS [F2]"
#define T_OPT_BGM "BGM VOLUME"
#define T_OPT_SE "SE VOLUME"
#define T_OPT_MUTE "MUTE"
#define T_OPT_HINT1 "Up/Down: select   Left/Right: +-5   Shift: +-1"
#define T_OPT_HINT2 "Home/End: 0% / 100%   Mouse wheel: +-5   F2/Esc: back"
#define T_OPT_HINT3 "0% mutes only that sound (BGM or SE)."
#define T_OPT_HINT4 "Changes apply at once and are saved automatically."
#define T_HUNT_ONLY "HUNT ONLY"
#define T_DRAGON_HUNT_TAG "DRAGON HUNT"
#define T_OPT_NODEVICE "No audio device found: sound stays off."

static const char *const T_NAV[8] = {"[1] MAIN",    "[2] MARKET", "[3] CRAFT", "[4] STOCK",
                                     "[5] UPGRADE", "[F1] HELP",  "[F5] SAVE", "[O] OPEN"};
static const char *const T_PHASE[3] = {"PREPARE", "OPEN", "SETTLED"};

static inline const char *text_result(Result r) {
    switch (r) {
        case R_OK:
            return "Done.";
        case R_ERR_PHASE:
            return "Not possible right now.";
        case R_ERR_RANGE:
            return "Invalid selection or amount.";
        case R_ERR_FUNDS:
            return "Not enough money!";
        case R_ERR_MATERIAL:
            return "Not enough materials (2 main + 1 sub).";
        case R_ERR_FULL:
            return "Storage is full!";
        case R_ERR_LOCKED:
            return "Locked! Earn the blueprint from the guild first.";
        case R_ERR_MAXLEVEL:
            return "Already at max level.";
        case R_ERR_NO_ITEM:
            return "Nothing selected.";
        case R_ERR_NO_CUSTOMER:
            return "No customer.";
        case R_ERR_PRICE:
            return "Invalid price.";
        case R_ERR_LIMIT:
            return "The calendar ends here: this is the last possible day.";
        case R_ERR_NOT_FOR_SALE:
            return "Not sold here! Win it from a Dragon Hunt contract.";
    }
    return "Error.";
}
static inline const char *text_sale(int r) {
    switch (r) {
        case SALE_OK:
            return "Sold! Thanks, smith!";
        case SALE_WRONG_TYPE:
            return "That's not what I came for.";
        case SALE_LOW_FIT:
            return "Not good enough for my needs.";
        case SALE_OVER_BUDGET:
            return "That's more than I can pay!";
        case SALE_TOO_EXPENSIVE:
            return "Too pricey for what it is.";
    }
    return "...";
}
static inline const char *text_guild(int r) {
    switch (r) {
        case GUILD_OK:
            return "Done.";
        case GUILD_ERR_PHASE:
            return "Contracts can only be handled while preparing.";
        case GUILD_ERR_RANGE:
            return "Invalid selection.";
        case GUILD_ERR_NO_CONTRACT:
            return "No such contract.";
        case GUILD_ERR_FULL:
            return "You already hold the maximum number of contracts.";
        case GUILD_ERR_NO_ITEM:
            return "Pick an item to deliver.";
        case GUILD_ERR_WRONG_TYPE:
            return "That is not the equipment they asked for.";
        case GUILD_ERR_LOW_FIT:
            return "The FIT of that item is too low for this contract.";
        case GUILD_ERR_REWARD_FULL:
            return "Cannot deliver: no room for the reward material! (stock is full)";
    }
    return "Error.";
}
static inline const char *text_save(SaveResult r) {
    switch (r) {
        case SAVE_OK:
            return "OK";
        case SAVE_ERR_IO:
            return "File error (no save file?)";
        case SAVE_ERR_FORMAT:
            return "Save file is damaged.";
        case SAVE_ERR_VERSION:
            return "Save is from another version.";
        case SAVE_ERR_VALUE:
            return "Save file has invalid data.";
    }
    return "Error";
}

static const char *const T_HELP_LINES[] = {
    "GOAL",
    "  Reach 1500 gold AND 80 reputation within 30 days.",
    "  Running out of money (debt below -45) = bankruptcy.",
    "",
    "A DAY",
    "  1 MARKET   buy @MATS (prices change daily; buy when cheap!)",
    "  2 CRAFT    pick equipment + main material + sub material.",
    "             Costs 2 main + 1 sub.  Materials change stats & price.",
    "  3 STOCK    set selling prices.  Higher = more profit, but may not sell.",
    "  4 UPGRADE  permanent improvements for your shop:",
    "@UPGRADES",
    "  G GUILD    take contracts from the notice board (hold up to 2 at once).",
    "             Craft the wanted equipment with enough FIT and DELIVER it by the",
    "             DUE DAY for gold + reputation. Deliver only while preparing.",
    "@GUILDDIFFS",
    "             No fees, no penalties. A contract not delivered by its due day",
    "             lapses at that day's settlement. The board is renewed every morning.",
    "@DRAGONHUNT",
    "  RANK       your guild rank follows the contracts you DELIVERED (lapsed ones",
    "             do not count). Each rank hands you a blueprint:",
    "@BLUEPRINTS",
    "             Locked equipment cannot be crafted and its customers do not show up",
    "             (also not on the board). Plans: BLUEPRINTS [B] on the guild screen.",
    "  O OPEN SHOP  customers come one by one. Pick an item and SELL, or",
    "             send them away. Wrong item, low quality or a high price",
    "             makes them refuse (they give up after 2 refusals).",
    "  End of day: upkeep is paid, prices update, next day starts.",
    "",
    "CUSTOMERS",
    "@CUSTOMERS",
    "  Watch their budget. FIT % shows how well an item suits them.",
    "",
    "QUALITY AND TRAITS",
    "  Every item gets a quality WHEN IT IS CRAFTED (you cannot see it beforehand).",
    "  Quality multiplies ATK / DEF / MAG / DUR after the Forge bonus; WT is not scaled:",
    "@QUALITIES",
    "  Masterwork and Legendary items also get exactly ONE special trait, picked at random:",
    "@TRAITS",
    "  Weight never drops below 1. Common and Fine items never have a trait.",
    "  Base odds by GUILD RANK (Common / Fine / Masterwork / Legendary, in %):",
    "@QUALITYODDS",
    "  Each FORGE level moves 5 points from Common to Fine (e.g. Apprentice + Forge",
    "  Lv 2 = 60 / 35 / 5 / 0). No Legendary items before the Journeyman rank.",
    "  The boosted stats are the real stats: FIT, value, recommended price, customers",
    "  and guild contracts all use them. Quality stays with each single item.",
    "",
    "DRAGONFORGING",
    "  Dragon Scale (a Dragon Hunt reward) is a craft material like any other: 2 as the",
    "  main material, 1 as the sub material. On a Masterwork or Legendary item it may",
    "  grant a DRAGON TRAIT in place of a normal one (never on Common or Fine items):",
    "@DRAGONTRAITS",
    "  Chance of a dragon trait by Dragon Scale used (main = 2, sub = 1, both = 3):",
    "@DRAGONODDS",
    "  The two traits are equally likely. If the roll fails you get a normal trait as",
    "  usual. Items made without Dragon Scale never get a dragon trait.",
    "",
    "SAVES AND RANDOMNESS",
    "  Save format version 4 stores every item's quality and trait, the state of",
    "  the craft random generator (craft_rng), and each contract's kind and reward.",
    "  Saves of version 1 to 3 still load (old contracts become plain contracts):",
    "  their items become Common / no trait, with saved stats, value and prices kept.",
    "  craft_rng is separate from the market / customer generator and from the guild",
    "  board generator, so rolling quality never changes prices, customers or contracts.",
    "",
    "KEYS",
    "  Mouse: click buttons.   1-5 screens   O open shop   F5 save",
    "  F1 help   F2 options (volumes)   Esc back   Arrows move/adjust   Enter confirm",
    "  Market: Up/Down row (list scrolls), Left/Right amount, Enter buy",
    "  Upgrade: press the key on its button; Left/Right turns the page",
    "  Guild: G on the main screen; Q/W/E accept, Up/Down pick an item, Enter deliver",
    "  Craft: Q/W/E equipment, A/S/D main, Z/X/C sub, Enter craft",
    "         R/F/V: next page of equipment / main / sub (when there are more than 3)",
    "  Stock: Up/Down item, Left/Right price (Shift = x10), R recommended, L liquidate",
    "  Customer: Up/Down item, Enter sell, D send away, E close shop early",
    "  Broke? Liquidate stock for 50% of cost in the STOCK screen.",
};
#define T_HELP_LINE_COUNT ((int)(sizeof T_HELP_LINES / sizeof *T_HELP_LINES))

#endif
