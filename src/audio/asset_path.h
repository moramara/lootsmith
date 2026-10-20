/* LOOT SMITH - assets/ の探索場所 (raylib に依存しないので、探索順序を単体テストできる)。
 * audio.c が候補ディレクトリを問い合わせ、audio/<BGM_FILE> を含む最初のものを選ぶ。 */
#ifndef ASSET_PATH_H
#define ASSET_PATH_H

#define ASSET_PATH_LEN 512    /* 候補 1 件分のサイズ。収まらないパスは切り詰めずに捨てる */
#define ASSET_CANDIDATE_MAX 6 /* 環境変数 + 実行ファイル相対 3 件 + "./assets" + コンパイル時の既定値 */

/* コンパイル時の既定値 (ビルド定義が設定する。CMakeLists.txt を参照)。ビルドが定義していなければ NULL。 */
#ifdef LOOTSMITH_ASSET_DIR
#define ASSET_BUILTIN_DIR LOOTSMITH_ASSET_DIR
#else
#define ASSET_BUILTIN_DIR ((const char *)0)
#endif

/* `out` に assets のルート候補を優先順に書き込み、その個数 (<= max) を返す:
 *   1. env_dir                       明示的な上書き ($LOOTSMITH_ASSETS)。NULL または空なら無視する
 *   2. <exe_dir>assets               exe_dir は raylib の GetApplicationDirectory() と同様、区切り文字で終わる
 *   3. <exe_dir>../assets
 *   4. <exe_dir>../share/lootsmith/assets
 *   5. assets                        作業ディレクトリからの相対
 *   6. builtin_dir                   コンパイル時の既定値 (ASSET_BUILTIN_DIR)。NULL または空なら無視する
 * ASSET_PATH_LEN - 1 文字を超える候補は捨てる (切り詰めたパスは別の場所を指す可能性があるため)。 */
int asset_root_candidates(const char *exe_dir, const char *env_dir, const char *builtin_dir, char out[][ASSET_PATH_LEN],
                          int max);

#endif
