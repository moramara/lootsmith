# ビルドと実行

この文書は、ビルド、実行、テストの手順の正本です。ゲームのルールは [GAMEPLAY.md](GAMEPLAY.md)、ソース構成は
[DEVELOPMENT.md](DEVELOPMENT.md) を参照してください。

## 必要なもの

- C11 コンパイラ (GCC / Clang)、make または CMake 3.16 以上
- ゲーム本体のみ raylib 5.5 (ロジックとテストのビルドには不要)。X11 と OpenGL の開発ヘッダが必要
- GUI テストのみ Xvfb と python3 + python-xlib

## make

```sh
make deps      # 任意: raylib 5.5 を third_party/raylib に取得してビルド (git と X11/GL 開発ヘッダが必要)
make           # ./lootsmith をビルド
make run       # ビルドして起動
make test      # ロジック・音声設定・BOT のテスト。raylib 不要、ASan/UBSan 付き
make install   # 任意: PREFIX=/usr/local の bin/lootsmith と share/lootsmith/assets へインストール
make clean
```

raylib は `RAYLIB_PATH=<raylib のチェックアウト>`、`third_party/raylib`、`pkg-config raylib` の順に探します。
ソース一覧とインクルードパス (`-Isrc`) は `Makefile` の先頭にまとめてあり、ヘッダは `src/*/*.h` のどれを変えても再ビルドされます。

## CMake

Makefile と並行して使える、同等のビルドです。

```sh
cmake -S . -B build [-DRAYLIB_PATH=<raylib のチェックアウト> | -DLOOTSMITH_FETCH_RAYLIB=ON]
cmake --build build
ctest --test-dir build --output-on-failure
```

| オプション | 既定 | 意味 |
|---|---|---|
| `LOOTSMITH_BUILD_GAME` | ON | raylib 製のゲーム本体をビルドする |
| `LOOTSMITH_BUILD_TESTS` | ON | ロジックのテストをビルドして登録する (raylib 不要) |
| `LOOTSMITH_SANITIZE` | ON | テストを AddressSanitizer + UBSan でビルドする (`make test` と同様) |
| `LOOTSMITH_FETCH_RAYLIB` | OFF | raylib が見つからないとき 5.5 をダウンロードしてビルドする (ネットワークと git が必要) |
| `RAYLIB_PATH` | 空 | `src/libraylib.a` をビルド済みの raylib のチェックアウト (Makefile と同じ意味) |

raylib が見つからない場合は、ロジックのライブラリとテストだけがビルドされます。ビルド後、`assets/` は
`build/lootsmith` の隣へコピーされるので、どの作業ディレクトリからでも起動できます。

## 実行

タイトル画面の `WATCH BOT [B]` から BOT 観戦を開始できます。操作方法は [GAMEPLAY.md](GAMEPLAY.md) を参照してください。

セーブファイルは作業ディレクトリの `lootsmith.sav`、設定ファイルは `lootsmith.cfg` です (形式は
[SAVE_FORMAT.md](SAVE_FORMAT.md))。セーブは新しい日の始まりごとに自動で行われます。

デバッグ/テスト用のスイッチ:

| スイッチ | 意味 |
|---|---|
| `--seed N` | 最初の新規キャンペーンを、ちょうど N のシードから始める |
| `--save FILE` | セーブファイルの場所を変える |
| `--settings FILE` | 設定ファイルの場所を変える |
| `--audio-log` | すべての音声イベントを `[audio] SE coin`、`[audio] bgm loop 2` のように標準出力へ出す |
| `--demo SCREEN_ID --frames N --shot out.png` | 状態を用意した画面を N フレーム描画してスクリーンショットを保存する |

## assets/ の探索順

`src/audio/audio.c` は、`audio/bgm_hearth.ogg` を含む最初のディレクトリを `assets/` として採用します
(候補の組み立ては `src/audio/asset_path.c`)。

1. `$LOOTSMITH_ASSETS` (明示的な上書き)
2. `<実行ファイルのディレクトリ>/assets`
3. `<実行ファイルのディレクトリ>/../assets`
4. `<実行ファイルのディレクトリ>/../share/lootsmith/assets` (`make install` / `cmake --install` の後)
5. `./assets`
6. コンパイル時の既定値 `LOOTSMITH_ASSET_DIR` (CMake ビルドではソースツリーの `assets/`。Makefile は定義しない)

512 バイトに収まらない候補は、切り詰めずに捨てられます。見つからない場合やオーディオデバイスを開けない場合は、ゲームは
無音で動き続けます (OPTIONS 画面にその旨が表示されます)。

## テスト

| 実行方法 | 内容 |
|---|---|
| `make test` | `tests/test_sim` (ロジックのユニットテスト + ボット対戦) と `tests/test_audio` (設定、効果音 ID、アセットの探索、同梱音声ファイルの検査) |
| `ctest --test-dir build --output-on-failure` | 上と同じテストを CMake から実行する |
| `python3 tests/gui_e2e.py ./lootsmith` | 任意の GUI エンドツーエンドテスト。Xvfb と python-xlib が必要で、`make test` には含まれない |

GUI テストは自分で Xvfb (`:77`) を起動します。オーディオデバイスのない環境で音声の項目も検査するには、ALSA の `null` デバイスを
既定にします (`~/.asoundrc` に `pcm.!default { type null }`)。`LIBGL_ALWAYS_SOFTWARE=1` でソフトウェア描画にできます。

テストの方針、バランス検証の読み方は [DEVELOPMENT.md](DEVELOPMENT.md) を参照してください。
