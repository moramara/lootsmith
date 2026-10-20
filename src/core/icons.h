/* LOOT SMITH - 12x12 ピクセルアートのスプライト登録簿 (raylib に依存しないので、テストから検査できる)。
 *
 * コンテンツテーブル (core/content_tables.c) は、スプライトを文字列 ID ("ingot"、"sword" など) で参照する。
 * icon_find() は失敗しない: NULL、""、未知の ID は「?」のフォールバックスプライトになるので、絵のないコンテンツも
 * 描画できる。スプライトを追加するには icons.c の ICONS[] に行を足す。
 *
 * スプライトの文字 ('.' = 透明): M/m = 主素材の明/暗、S = 副素材、H/A/T = 客の帽子/鎧/上着、
 * そのほか ICON_PALETTE_CHARS に列挙された固定色
 * (RGB への対応は ui/ui_draw.c の pal_color() が行う。両者は同期させておくこと)。
 */
#ifndef ICONS_H
#define ICONS_H

#define ICON_SIZE 12
#define ICON_FALLBACK_ID "unknown"

typedef struct {
    const char *id;
    const char *rows[ICON_SIZE]; /* ICON_SIZE 個の文字列。各文字列は ICON_SIZE 文字 */
} IconSprite;

extern const char ICON_PALETTE_CHARS[];

const IconSprite *icon_find(const char *id); /* NULL にはならない */
int icon_exists(const char *id);             /* 実際に登録された ID (フォールバックではない) のときだけ 1 */
int icon_count(void);
const IconSprite *icon_at(int index);

#endif
