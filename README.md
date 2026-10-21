# LOOT SMITH

C11 + raylib で作った 8bit 風の鍛冶屋タイクーンゲームです。素材を仕入れ、装備をクラフトし、価格を決めて客に売り、鍛冶場を強化します。
目標は **30 日以内に 1500 ゴールドと評判 80 に到達すること** (所持金が -45 ゴールドを下回ると破産で終了)。

## 特徴

- 市場価格の変動、客ごとの好み (優先する能力値・予算・許容価格)、価格設定による駆け引き
- ギルド契約 (受注 → クラフト → 納品) とギルドランクによる設計図の解放
- **The Ascendant Era (大型アップデート)**: 最高ランク Ascendant Smith (75 件)、新設計図と装備 3 種 (Astral Blade / Aegis of Dawn / Void Codex) と客 3 人、
  新素材 (Mithril / Moonstone / Emberglass、特別契約でのみ得られる Astral Shard)、特別契約 Relic Expedition、7 日周期の日替わりイベント
  (Market Caravan / Arcane Convergence / Guild Assembly)。セーブ形式は変わらず、既存のセーブはそのまま遊べる
- 装備ごとの品質 (Common〜Legendary) と特殊特性
- オリジナル合成の BGM と効果音、音量オプション
- 同じシードと同じ操作列なら完全に再現できる決定的なシミュレーション
- raylib に依存しないロジック層。ASan/UBSan 付きのユニットテストとボット対戦による検証つき
- タイトル画面から BOT の自動プレイを観戦可能 (一時停止・1 手送り・速度変更に対応)

## 依存関係

| 用途 | 必須 | 内容 |
|---|---|---|
| ゲーム本体 | はい | C11 コンパイラ、make または CMake、raylib 5.5 (`make deps` で取得可能。X11/GL の開発ヘッダが必要) |
| テスト (`make test`) | はい | C11 コンパイラのみ (raylib 不要) |
| GUI テスト | 任意 | Xvfb、python3 + python-xlib |
| 音声の再生成 | 任意 | python3 + numpy + scipy + ffmpeg (ビルド・実行には不要) |

## 最短のビルドと実行

```sh
make deps      # 任意: raylib 5.5 を third_party/raylib に取得してビルド
make run       # ゲームをビルドして起動
make test      # ロジックと音声設定のテスト (raylib 不要)
```

CMake でのビルド、オプション、インストール、デバッグ用スイッチは [docs/BUILD.md](docs/BUILD.md) を参照してください。

## ドキュメント

| 文書 | 内容 |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | ビルド、実行、インストール、コマンドライン引数、テストの実行、`assets/` の探索順 |
| [docs/GAMEPLAY.md](docs/GAMEPLAY.md) | ゲームのルール、ギルド契約、BOT 観戦の操作 |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | ソース構成、コンテンツの追加手順、乱数ストリーム、数値の上限、バランス検証、コードスタイル |
| [docs/SAVE_FORMAT.md](docs/SAVE_FORMAT.md) | セーブ/設定ファイルの形式、バージョン履歴、互換性ポリシー、検証 |
| [assets/audio/README.md](assets/audio/README.md) | 音声ファイルの由来・生成手順・ライセンス・測定値 |

## ディレクトリ構成

```
src/main.c    エントリーポイント
src/core/     ゲームロジック・コンテンツ定義・セーブ/ロード・アイコン (raylib 不要)
src/bot/      BOT 自動プレイの判断と段階実行 (raylib 不要)
src/audio/    raylib の音声制御、音量設定、効果音 ID、assets の探索
src/ui/       画面制御、描画、各画面 (raylib フロントエンド)
tests/        ユニットテスト (test_sim.c、test_audio.c、test_bot.c) と GUI テスト (gui_e2e.py)
tools/        音声の生成スクリプト (gen_audio.py)
assets/       BGM と効果音
docs/         ドキュメント
```

各ファイルの責務は [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) にあります。
