/* LOOT SMITH - ユーザー設定 (音量)。純粋な C で raylib は使わず、`make test` で単体テストされる。
 *
 * ファイル形式 (テキスト。セーブデータとは別。既定のファイル名は lootsmith.cfg):
 *     LOOTSMITH_SETTINGS 1
 *     bgm_volume 35
 *     se_volume 65
 * 音量は 0..100 の整数パーセント。読み込みが致命的に失敗することはない: ファイルがどう壊れていても、
 * 呼び出し側は必ず有効な値を受け取る (欠けている/読めないものは既定値)。settings_load_file() を参照。 */
#ifndef SETTINGS_H
#define SETTINGS_H

#define VOLUME_MIN 0
#define VOLUME_MAX 100
#define VOLUME_DEFAULT_BGM 35
#define VOLUME_DEFAULT_SE 65
#define SETTINGS_VERSION 1

typedef struct {
    int bgm_volume, se_volume;
} Settings;

typedef enum { SETTINGS_OK = 0, SETTINGS_ERR_IO, SETTINGS_ERR_FORMAT } SettingsResult;

int volume_clamp(long v); /* -> 0..100 */
float volume_gain(
    int percent); /* パーセント (範囲内に丸める) -> 線形ゲイン 0.0 .. 1.0。0 % はちょうど 0.0 (ミュート) */
void settings_defaults(Settings *s);
void settings_sanitize(Settings *s); /* 両方の音量を範囲内に丸める */

/* ファイルのテキストを解析する。`out` には常に有効な値が入る: 欠けている/解析できないキーは既定値のまま、
 * 0..100 の範囲外の数値は丸め、未知のキーは無視し、ヘッダが違えばすべて既定値になる。
 * ヘッダと 2 つのキーがすべて存在し、形式も正しかった場合にだけ SETTINGS_OK を返す。 */
SettingsResult settings_parse(const char *text, Settings *out);
SettingsResult settings_load_file(const char *path,
                                  Settings *out); /* IO エラー (例: 初回起動でファイルがない) -> 既定値 */
SettingsResult settings_save_file(const Settings *s,
                                  const char *path); /* <path>.tmp に書き込み、その後 <path> を置き換える */

#endif
