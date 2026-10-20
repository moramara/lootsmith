/* LOOT SMITH - 音声再生 (raylib)。raylib の音声 API に触れる処理はすべて audio.c にある。
 * どの関数も、オーディオデバイスやファイルがなくても安全に呼べる: その場合、ゲームは単に無音で動く。 */
#ifndef AUDIO_H
#define AUDIO_H
#include <stdbool.h>
#include "settings.h"
#include "sfx.h"

/* オーディオデバイスを開き、アセットを探して読み込み、音量を適用し、BGM のループを (一度だけ) 開始する。
 * 音声出力が利用できれば true を返す。InitWindow() の後に呼ぶこと。 */
bool audio_init(const Settings *s);
void audio_update(void);                                 /* 毎フレーム: BGM のストリームに供給する */
void audio_set_volumes(int bgm_percent, int se_percent); /* 即時反映。0 % はそのグループだけを無音にする */
void audio_play(SeId id);                                /* 効果音を今すぐ 1 つ再生する */
void audio_shutdown(void);                               /* すべて停止し、解放し、デバイスを閉じる (冪等) */

bool audio_available(void); /* デバイスが開いている */
bool audio_bgm_playing(void);
int audio_bgm_loops(void);     /* BGM が何回ループしたか (診断用) */
void audio_set_trace(bool on); /* --audio-log: "[audio] ..." の行を標準出力に出す */

#endif
