# 開発ガイド

ソース構成、コンテンツの追加手順、設計上の不変条件、バランス検証、コードスタイルの正本です。ビルド手順は [BUILD.md](BUILD.md)、
ルールは [GAMEPLAY.md](GAMEPLAY.md)、セーブの形式と互換性は [SAVE_FORMAT.md](SAVE_FORMAT.md) にあります。

## ソース構成

依存の向きは `ui` → `core`、`ui` → `audio`、`ui` → `bot`、`bot` → `core` です。`core/`、`bot/`、`audio/` の純 C 部分は
raylib に依存しないので、`make test` で単体テストできます。`core` と `audio` の間に依存はありません。インクルードパスは `-Isrc` で、他のディレクトリのヘッダは
`#include "core/simulation.h"` のようにモジュール名付きで、同じディレクトリのヘッダは名前だけで include します。

### `src/core/` — ゲームロジック (raylib 不要)

公開 API は **`simulation.h` 1 つ**です (型、enum の ID、`BAL`、テーブル、全関数の宣言)。実装は責務ごとに分かれています。
`sim_internal.h` は core の内部専用で、ファイル間で共有する小さな補助関数 (`clampi`、`sat_add` など) と内部関数の宣言を持ち、
ui / audio / tests からは include しません。

| ファイル | 責務 |
|---|---|
| `content_tables.c` | バランス値 `BAL` とコンテンツのテーブル (素材、装備、客、アップグレード、品質、特性、ランク、設計図、ギルド難易度) |
| `content_validate.c` | `content_validate()`: すべてのテーブルの妥当性検査 |
| `rng.c` | xorshift の乱数、シードの導出 (`campaign_seed`、`guild_seed`) |
| `game_state.c` | `game_init()`、`game_validate()` (状態の不変条件)、`net_of()` |
| `market.c` | 市場価格の変動と購入 |
| `crafting.c` | 能力値の計算、品質/特性の抽選と適用、クラフト、旧セーブ用のクラフト乱数の導出 |
| `inventory.c` | 在庫、価格の編集、換金 |
| `customers.c` | 客の生成、装備の適合度 (FIT)、売却の判定、評判の変化 |
| `day_flow.c` | 開店、客への提示、精算、翌日への進行、サンドボックスの続行 |
| `upgrades.c` | アップグレードの費用、購入、効果の取得 |
| `guild_rank.c` | ランクと設計図の導出 (すべて `guild_done` から。不正な引数でも安全) |
| `guild.c` | 契約の掲示 (Dragon Hunt の抽選を含む)、受注、納品 (報酬素材の付与を含む)、失効、旧セーブ用のギルド状態の導出 |
| `save.c` | セーブ/ロード、検証、旧形式の移行、原子的な書き込み |
| `icons.c` / `icons.h` | 12x12 のスプライトの登録簿 (文字列 ID で引く。未知の ID は「?」) |

### `src/bot/` — 自動プレイ

| ファイル | 責務 | raylib |
|---|---|---|
| `bot.c` / `bot.h` | 市場での仕入れ、製作、ギルド契約、客対応を行う観戦用 BOT。`bot_step()` は 1 段階ずつ進める | 不要 |

BOT は `GameState` を独立して所有するため、通常のプレイ状態やセーブデータを変更しません。同じシードなら同じ操作と結果になります。BOT はゲームの正式なルール API を呼び出し、別の簡易ルールで結果を計算するものではありません。

### `src/audio/` — 音声と設定

| ファイル | 責務 | raylib |
|---|---|---|
| `audio.c` / `audio.h` | raylib の音声 API に触れる唯一の場所: デバイス、BGM ストリーム、効果音、音量、assets の探索 | 必要 |
| `settings.c` / `settings.h` | 音量設定の解析・丸め・原子的な保存 | 不要 |
| `sfx.c` / `sfx.h` | 効果音の ID・優先度、「1 イベントにつき 1 音」のキュー、再開始の防止 | 不要 |
| `asset_path.c` / `asset_path.h` | `assets/` の探索候補の組み立て (切り詰めなし、上限つき) | 不要 |

### `src/ui/` — raylib フロントエンド (仮想 640x360 画面、最近傍補間で拡大)

`ui.h` が公開インターフェース (`App`、`ui_frame` など) で、`ui_internal.h` はこのディレクトリ内部の共有宣言です。
`text.h` はプレイヤーに表示する文字列 (ASCII のみ。raylib 組み込みフォントに日本語グリフがないため) で、翻訳対象ではありません。

| ファイル | 責務 |
|---|---|
| `ui_draw.c` | パレット、スプライト描画、テキスト・パネル・バー、装備名や品質色などの表示ヘルパ |
| `ui_widgets.c` | ボタン、キー入力、ページ送り、メッセージ、画面遷移、エフェクト |
| `ui_chrome.c` | 全画面共通のヘッダ・フッタ・鍛冶場の背景 |
| `ui_app.c` | アプリの状態、ゲーム開始・保存・読み込み、画面とフェーズの同期、`ui_frame`、デモ用シナリオ |
| `screen_home.c` | タイトル画面とメイン画面 |
| `screen_shop.c` | 市場とアップグレード |
| `screen_craft.c` | クラフト |
| `screen_inventory.c` | 在庫 (在庫リストは客画面と共有) |
| `screen_customer.c` | 客への販売 |
| `screen_report.c` | 日報、クリア、ゲームオーバー |
| `screen_guild.c` | ギルドと設計図の一覧 |
| `screen_options.c` | オプション (音量) |
| `screen_help.c` | ヘルプ (マーカー `@MATS` `@CUSTOMERS` `@UPGRADES` をテーブルから展開) |
| `screen_bot.c` | BOT の進行状況・行動ログ・速度と一時停止の操作 |

### その他

`src/main.c` (エントリーポイント、ウィンドウ、メインループ)、`tests/` (`test_sim.c`、`test_audio.c`、`test_bot.c`、`gui_e2e.py`)、
`tools/gen_audio.py` (音声の生成。ビルドにも実行にも不要)、`assets/audio/` (音声)、`CMakeLists.txt` と `Makefile` (同等の 2 つのビルド)。

## コンテンツの追加

ID は **追記のみ**です。並べ替え・削除・再利用は絶対にしないでください (セーブファイルに書き込まれます)。`*_COUNT` は最後の列挙子なので、
件数は自動的に追従します。

| 追加するもの | 1. `src/core/simulation.h` (`*_COUNT` の直前) | 2. `src/core/content_tables.c` | 3. 必要に応じて |
|---|---|---|---|
| 素材 | `MAT_` enum に `MAT_xxx` | `MATERIALS` に `[MAT_xxx] = {...}` の行 (名前、説明、アイコン、色、価格、能力値) | `icons.c` に新しいスプライト |
| 装備 | `EQ_xxx` | `EQUIPS` に行 (名前、説明、アイコン、基本能力値) | `icons.c` にスプライト |
| 客 | `CT_xxx` | `CUSTOMER_DEFS` に行 (名前、説明、`wants_eq`、重み、範囲、パレット) | `icons.c` にスプライト |
| アップグレード | `UP_xxx` | `UPGRADES` に行 (名前、説明、ホットキー、`effect_fmt`、費用、効果) | *効果そのもの* にはロジック側のフックが必要 |

UI の一覧、ヘルプ、セーブ/ロードはテーブルを読むので、ほかに編集する場所はありません。

- 名前は `NAME_MAX_LEN` (18) 文字まで。固定幅の UI では、それより長い文字列が `...` で切り詰められます。
- アイコンが未設定または未知の場合は、「?」のスプライトが描かれます。
- `make test` は `content_validate()` を実行し、名前の欠落や長すぎる名前、アイコン ID のタイプミス、範囲の誤り、ホットキーの重複などを失敗にします。
- セーブ: 新しい ID に移行は不要です (古いセーブは初期値で読み込まれます)。[SAVE_FORMAT.md](SAVE_FORMAT.md) を参照。
- コンテンツを足すと経済が変わります。「バランス検証」を参照してください。

### ギルドの契約と設計図

- 報酬・FIT・期間は `GUILD_DIFFS`、日数ごとの難易度の出現率は `GUILD_SPAWN` (どちらも `content_tables.c`、`content_validate()` が検査する)。
- 新しい *種類* の契約 (たとえば素材の依頼) は、`GuildContract` の新しいフィールド、セーブのレコード (`gb` / `ga`)、`game_validate()`、
  `guild_deliver()` の納品ルールが必要で、`SAVE_VERSION` を上げます。Dragon Hunt がその実例です (下の「契約の種類と追加報酬」)。
- **設計図の装備を増やす** (または解放条件を動かす): `simulation.h` の `EQ_COUNT` / `CUST_TYPE_COUNT` / `BP_COUNT` の直前に ID を追加し、
  `EQUIPS`、`CUSTOMER_DEFS` (客はその装備だけを求めなければならない)、`BLUEPRINTS` (ランク + 2 つの ID) に行を足し、`icons.c` にスプライトを足して
  `make test` を実行する (`content_validate()` が装備・客・設計図・アイコン・名前を相互に検査します)。新しいランクは `GUILD_RANKS` に 1 行足す
  (+ `RANK_*`)。UI・ヘルプ・セーブはテーブルを読みます。`tests/test_sim.c` の件数の期待値も更新してください。
- 新しいランクを足したら `QUALITY_RATE_PCT` にも同じ ID の行が必要です (合計 100。`content_validate()` が検査)。範囲外のランクを渡す
  `quality_rates()` は最高ランクに丸められるので、最高ランクを足すとその挙動も新しいランクに移ります。
  ランクを足すと、`counts` 行の装備数・客数も変わります (現行ビルドの出力を検査するテストの期待値)。旧ビルドのセーブの実物は書き換えないこと。
- テストで 30 件を超えて契約を納品し続けるときは、補助関数 `give_item` が毎回素材を足すため、`stock_cap` (99) を超えないよう
  納品のたびに素材を空にしてください。Mythic Smith (50 件) の検証が該当します。
- 設計図の装備を足してもセーブ形式は変わりません。ランクと解放は `guild_done` から導かれ、新しい ID は `counts` を延ばすだけです。
- 設計図の名前は `<装備名> Plans` なので、`BLUEPRINT_NAME_MAX_LEN` (20) まで許される (通常のコンテンツ名は `NAME_MAX_LEN` = 18。`Aegis of Dawn Plans` は 19 文字)。
  1 つのランクに設計図が複数あってもよい (Ascendant Smith は 3 つ)。UI の「次の設計図」は `+N PLANS` と表示する。

### 契約の種類と追加報酬 / 市場で買えない素材 (Dragon Hunt の実装規則)

新しい種類の契約や追加報酬、市場で買えない素材を足すときの規則です。

1. **定義は 1 つのテーブルに集める。** Dragon Hunt の数値 (依頼主・最低 FIT・報酬・日数・報酬素材・出現率・解放条件) は `GUILD_DRAGON_HUNT`
   (`content_tables.c`) だけにあり、ロジックはそれを読む。`content_validate_dragon_hunt()` が妥当性 (報酬素材が `hunt_only`、
   数が 1..`stock_cap`、依頼主が `min_done` で解放済みなど) を検査する。通常契約の `GUILD_DIFFS` / `GUILD_SPAWN` は変えない。
2. **契約種別は `GuildContract.kind` (`GUILD_CONTRACT_*`、追記のみ)。** 0 は必ず「通常契約・報酬なし」にして、ゼロ初期化された契約が
   有効な通常契約になるようにする。通常契約の報酬欄は正規形 (`reward_mat == 0 && reward_qty == 0`)。種別と報酬の組み合わせは
   `guild.c` の `contract_valid()` が検証する (不正な素材 ID、負や過大な数、種別と合わない報酬、掲示板の Dragon Hunt は最大 1 件)。
3. **納品は原子的。** `guild_deliver()` は、種類・FIT・報酬を受け取れるか (在庫上限) をすべて何かに触れる前に判断し、失敗なら
   `GuildResult` を返して状態を一切変えない。報酬素材は納品の成功時にだけ付与し、失効 (`guild_expire`)・通常契約・失敗した納品では付けない。
4. **乱数はギルドのストリームだけ。** 出現の抽選 (`guild_dragon_hunt_roll_hits()` は純粋関数、ロール 0..99 が `< spawn_pct`) は
   `guild_rng` から、対象 (`guild_dragon_hunt_eligible()`) のときにだけ 1 回引く。対象外では何も引かず、従来の掲示板の乱数列を保つ。
5. **市場で買えない素材は `MaterialDef.hunt_only = 1`。** 素材テーブルに行を足し (`MATERIALS`、`icons.c` のスプライト)、
   `market_buy()` は `R_ERR_NOT_FOR_SALE` を返し、`market_quote()` は -1、`market_update()` はその素材の価格を動かさず **乱数も引かない**
   (引くと客の列など `rng` の後続が変わる)。価格は見積もり (`estimate_craft_cost()`) の目安にだけ使う。クラフト・混合・Forge・品質・特性・価値の計算は
   通常の素材と同じで、専用の例外を作らない (希少さは `MaterialDef.rarity_value` で価値に反映する。素材 1 個あたりの加算点で、
   `equipment_price()` が主 2 + 副 1 の 3 個分を足す。0 が既定で、変えても保存済みの装備の価値は再計算されない)。報酬の素材は `mat_cost` を増やさない (無料 = 原価 0。docs/GAMEPLAY.md に明記)。
   `hunt_only` の素材は、どこかの契約の報酬でなければならない (`content_validate()` が検査)。
6. **UI は表を読む。** 市場の行は `hunt_only` なら `HUNT ONLY` を出し、購入操作は core の `market_buy()` に通す。価格の帯 (ホーム・日報) は
   `market_mat_count()` / `market_mat_at()` で買える素材だけを並べる。契約の札と報酬は `kind` / `reward_*` から描く。
   BOT・戦略の自作コードも `hunt_only` の素材を仕入れ候補に入れない (仕入れに失敗して契約を見送ってしまう)。
7. **セーブ。** 契約の `kind` / `reward_mat` / `reward_qty` は `gb` / `ga` の末尾に保存する (v4)。旧バージョンの契約は 8 フィールドで、
   通常契約として復元する。素材 ID の追加自体は `counts` で表され、バージョンは上げない。[SAVE_FORMAT.md](SAVE_FORMAT.md) を参照。

### 複数の特別契約が共存する場合の抽選規約 (Dragon Hunt + Relic Expedition)

特別契約 (`GuildContract.kind != GUILD_CONTRACT_STANDARD`) を足すときの規約です。定義は `SpecialContractDef` (= `DragonHuntDef`) の表 1 行
(`GUILD_DRAGON_HUNT` / `GUILD_RELIC_EXPEDITION`) で、`guild_special_def(kind)` が引く。

1. **ロールは掲示板の更新につき 1 回。** `guild_special_eligible(done)` (= 最初の特別契約の解放条件) のときだけ、`guild_rng` から 0..99 を **1 回** 引き、
   `guild_special_from_roll(done, roll)` (純粋関数) が分類する。特別契約ごとに別々のロールを引いてはいけない (引くと、後から解放される契約のために
   それ以前の乱数列がずれ、既存の進行が変わる)。対象外 (50 件未満) では何も引かない。
2. **帯は積み上げる。** 後から解放された契約が先頭の帯 `[0, spawn_pct)` を取り、既存の契約の帯はその直後に続ける。これで既存の契約の出現率
   (Dragon Hunt の 25%) は、新しい契約が解放されても変わらない。まだ解放されていない契約の帯は存在しない (75 件未満では従来の `roll < 25` と同一)。
   帯の合計が 100 を超えない・解放順の整合は `content_validate_special_contracts()` が検査する。
3. **掲示板の不変条件**: 特別契約は 1 回の更新で最大 1 件、出ると通常契約が 1 件減って総数は常に `GUILD_OFFER_COUNT`、通常契約の依頼主は特別契約の依頼主と
   重ならない。`game_validate()` は掲示板の特別契約を合計 1 件までとして検査する。
4. **契約の保存は既存の `kind` / `reward_mat` / `reward_qty`** (v4 の `gb` / `ga` 末尾)。新しい種別 ID を追記するだけで、保存フィールドもバージョンも増えない。
   `contract_valid()` は種別ごとに `guild_special_def()` から依頼主・解放条件・報酬素材 (`hunt_only`)・数を検査する。
5. **報酬素材の出所は表から引く** (`guild_reward_source(mat)`): UI の説明 (市場の `HUNT ONLY` 行、クラフトの不足表示) に契約名をハードコードしない。

### 日替わりイベントの設計 (`src/core/events.c`)

- イベントは **日付だけから決まる純粋関数** `day_event(day)` (`phase = (day - 1) % EVENT_CYCLE_DAYS`)。**保存しない・乱数を使わない・`GameState` に何も足さない**:
  ロード後も同じ日付は同じイベントになり、セーブ形式 (v4) は変わらない。効果の数値はすべて `DAY_EVENTS` の行 (`content_validate_events()` が検査:
  phase の重複なし、割引・加算の範囲、Arcane の「移す量 = 加える量」で合計 100 を保つ、Common を超えて移さない)。
- **判定の呼び出し口は 1 つ。** 市場 (`market_unit_price` / `market_quote`)、クラフト (`quality_rates_on_day` / `quality_from_roll_on_day`)、ギルド
  (`event_guild_bonus`)、UI (`day_event()` / `event_headline()`) はすべて `day_event()` を通る。UI や BOT に日付の判定 (`% 7` など) を書かない。
- **既存 API の挙動は不変**: `quality_rates()` / `quality_from_roll()` はイベントなし (日付 0) の確率を返す。日付つきの `*_on_day` が新しい API。
  確率の組み立て順は **基本確率 → イベント補正 → Forge 補正** で、各段階は Common から他へ移すだけなので合計は常に 100・負にならない。
- **乱数の消費はイベントで変わらない。** Arcane の日もクラフトの品質ロールは 1 回 (確率だけが変わる)。イベントを理由に `rng` / `guild_rng` / `craft_rng` を
  追加で引かない。テストでクラフトの品質を固定するときは、実際の判定と同じ `quality_from_roll_on_day()` で `craft_rng` を選ぶこと (Arcane の日は
  日付なしの関数だと結果がずれる)。
- Market Caravan の価格は `(価格 × (100 − Supplier%) × (100 − 15) + 5000) / 10000` を 1 回だけ四捨五入し (最低 1)、表示・見積もり・購入が同じ式を通る。
  `hunt_only` は対象外。Guild Assembly の上乗せ額は `(gold × 10 + 50) / 100` (0.5 切り上げ) で、判定は **納品した日** (`g->day`)。

### 乱数ストリームの保持 (新しい素材を足すとき)

- 市場更新が `rng` を引くのは、導入済みの買える素材 (`MARKET_RNG_MATS` = Dragon Scale の ID + 1 未満) だけ。**それ以降に追加した買える素材
  (Mithril / Moonstone / Emberglass) は `rng` を消費せず**、変動幅をその時点の `rng` の *値* と日付・素材 ID から作るローカルな乱数 (`guild_seed`) で決める。
  ストリームは 1 つも進まないので、素材を足しても既存のキャンペーンの客の列・価格の推移・同じシードでの結果は変わらない (`rng_streams` の固定値テストが
  そのまま通る)。素材を足す側が `rng` の引き数を増やす実装 (追加の `rng_range`) は、既存の進行を全部ずらすので避ける。
- 新しい `hunt_only` 素材 (Astral Shard) は従来どおり乱数を引かず、価格も動かない。
- 旧ビルドのセーブを読むと、追加素材の価格・変動は既定値 (基準価格・0) になる (次の市場更新で通常どおり動き出す)。

## 設計上の不変条件

- **アクション関数は失敗したら状態を変えない。** 判断は何かに触れる前に行い、失敗は `Result` で返します。
- **乱数ストリームは 3 本で、互いに消費しない。** (特別契約の抽選は `guild_rng` に 1 ロール、市場更新は導入済みの買える素材 7 つだけ `rng` を引く。日替わりイベントは乱数を使わない) `rng` (市場・客)、`guild_rng` (契約)、`craft_rng` (品質と特性)。`craft_rng` と `guild_rng`
  は初期シードから専用のソルトで導出し (`game_init()`)、他のストリームの値を変えません (テストが値を固定しています)。クラフトは
  すべての検証 (`can_craft()`) を終えた後で、成功したときにだけ `craft_rng` を引きます (品質に 1 回、品質が特性を持つなら特性にもう 1 回。
  Dragon Scale を使うレシピだけ、特性のある品質で判定の 1 回が加わる)。
  したがって、品質が市場価格、客の行列、契約の掲示板をずらすことはありません。
- **特性 ID は追記のみ。通常特性の抽選範囲は `EQ_TRAIT_STANDARD_COUNT` で固定する。** `TraitDef` の行を足すときは、`simulation.h` の
  `EQ_TRAIT_*` の *末尾* (`EQ_TRAIT_COUNT` の直前) に ID を追記し、既存の ID の値・順序・再利用を変えない (セーブは ID をそのまま保存する)。
  通常クラフトの特性抽選は `trait_from_roll(rng_range(0, EQ_TRAIT_STANDARD_COUNT - 2))` で、`EQ_TRAIT_COUNT` を使ってはいけない:
  `EQ_TRAIT_COUNT` を使うと、ID を足しただけで既存の抽選範囲・分布・同一シードでの結果が変わる。`EQ_TRAIT_STANDARD_COUNT` を変えるのは
  通常の抽選に新しい特性を加えるときだけで、既存の結果がずれることを承知の上で、テストの固定値と一緒に更新する。
  Dragonforging の特性は `TraitDef.dragon = 1` の行で、`EQ_TRAIT_STANDARD_COUNT` 以降の ID に限る (`content_validate_traits()` が検査)。
  Dragon Scale を使っていない装備の `dragon` 特性は不正で、`equipment_apply_quality()` が拒否し `game_validate()` / セーブの読み込みが弾く。
  確率は `DRAGON_TRAIT_PCT[使用数]` (使用数は `dragon_scale_uses()`: 主 2 + 副 1)、候補は `DRAGON_TRAIT_IDS` に集約する。
  抽選の判定は純粋関数 (`chance_roll_hits()` / `dragon_trait_hits()` / `dragon_trait_from_roll()`) で、`craft_roll_trait()` がそれを組み立てる。
  **`craft_rng` の消費は、Dragon Scale を使わないクラフトでは従来と同一** (品質 1 回 + 特性のある品質で通常特性 1 回)。Dragon Scale を使うときだけ、
  特性のある品質で判定 1 回 (+ 当たりなら候補の選択 1 回、外れなら通常特性 1 回) を足す。市場・客の `rng` と `guild_rng` は引かない。
- **能力値の補正はちょうど 1 回。** `equipment_compute_stats()` は基本能力 + Forge (品質なし) を返し、`equipment_build()` が
  基本 → 品質 → 特性を毎回ゼロから組み立てます。`equipment_apply_quality()` は、すでに品質や特性を持つアイテムを拒否するので、重ねがけになりません。
- **ランクと解放は保存しない。** `guild_done` からの純粋関数です。
- **未解放の客は現れない。** `customer_generate()` は、解放済みの客タイプだけを id 順に 1 回で抽選します。何も解放されていなければ、
  設計図導入前と同じ抽選になります (回帰テストで固定)。

### 数値の上限

到達可能なあらゆる状態を保存でき、かつ読み込めるよう、上限は `simulation.h` に集約され、`game_validate()` が同じ数値で検証します。
`MAX_DAY` 9999、`STAT_CAP` 1e9 (キャンペーン累計)、`DAY_CAP` 1e8 (所持金と日次レポートの各額)、`MONEY_FLOOR` -1e6、`GUILD_ID_MAX` 1e9。

- カウンタはオーバーフローせず **飽和**します (`sat_add`、`money_add`)。長いサンドボックスでも、ローダが拒否する状態は生まれません。
- `day_advance()` は `MAX_DAY` の日に `R_ERR_LIMIT` を返します (状態は変わらない)。`campaign_continue_sandbox()` は何かを変える前に `MAX_DAY` を検査します。
- `swing_pm` は `abs` を使わない単純な比較で範囲を検査します (`abs(INT_MIN)` は未定義動作)。
- 契約 ID は `GUILD_ID_MAX` 未満で発行されます。カウンタが近づいたら、`guild_refresh_board()` が受注中の契約を 1..n に振り直す (古い掲示板は
  どのみち入れ替わる) ので、有効な状態が日付を変えただけで無効になることはありません。
- `--seed N` は最初の新規キャンペーンをちょうど N から始めます。以後のキャンペーンは、従来どおり時計を使う `campaign_seed()` から来ます。

## テスト

- `tests/test_sim.c`: ロジックのユニットテストとボット対戦。セーブ互換 (旧形式の実物、ファジング、破損データ)、極端な値、回帰を含みます。
- `tests/test_ascendant.c`: The Ascendant Era (新ランク・設計図・客・素材・Relic Expedition・Astral Shard・日替わりイベント・保存の検証・乱数ストリームの独立)。
  抽選は長期シミュレーションだけに頼らず、`guild_special_from_roll()` などの境界値を直接検査する。
- `tests/test_bot.c`: 観戦用 BOT の状態不変条件、終端までの進行、同じシードでの決定性。Mythic Smith (50 件) と Ascendant Smith (70 / 75 件) 開始の
  サンドボックスで、Dragon Hunt / Relic Expedition の受注・納品・在庫満杯時の回避を検証する。
- `tests/test_audio.c`: 設定ファイル、音量の対応、効果音 ID と「1 イベントにつき 1 音」、assets の探索 (回帰を含む)、同梱音声ファイルの検査
  (WAV 形式、クリッピング、端のクリック、BGM の存在、`assets/audio/README.md` の網羅性)。
- `tests/gui_e2e.py`: 実際のバイナリを Xvfb で動かし、X11 の入力で OPTIONS 画面と音声イベントを検証する任意のテスト。

テストを削除したり、失敗を隠すためにアサーションを緩めたりしないでください。期待値を変える場合は、その理由 (仕様の変更) をコミットに残します。

## バランス検証

客の種類は一様に抽選されるので、ある装備を作らない戦略は、その客に対応できません。コンテンツやギルドの数値を変えたときは、
`make test` が走らせるボット戦略 (出荷時の装備のみ、全装備 + 出荷時の素材、Steel/Silver、Copper、Obsidian、ギルドあり/なし) のクリア率を読んでください。
`test_bots()` のしきい値を確認し、*新しい* 行の数値を調整します。ギルドなしの戦略は元のキャンペーンの回帰基準で、ギルドの影響を受けません。

ギルドは強力な追加収入です。仕様どおりの報酬では、同じボットが契約 **あり** でほぼすべてのキャンペーンをクリアし、ギルドなしより 8〜10 日早く
終わります (9 種の装備すべてに対応するボットは約 15 日)。これはアサーションで隠さず、テストが出力として報告するものです。強すぎる場合は
`GUILD_DIFFS` / `GUILD_ACTIVE_MAX` / `GUILD_OFFER_COUNT` を調整して `make test` を再実行してください。

**品質の影響** (各戦略 200 キャンペーン、品質の導入前 → 後。`bots` テストは同じ表を 40 キャンペーンで出力し、通常の売上とギルドのゴールドも示す):

| 戦略 | クリア数 | 平均クリア日 | 通常の売上 | ギルドのゴールド |
|---|---|---|---|---|
| broad、ギルドなし | 158 → 179 | 26.4 → 25.7 | 4712 → 4686 | - |
| cheap (copper)、ギルドなし | 164 → 179 | 26.5 → 25.9 | 4785 → 4756 | - |
| steel/silver、ギルドなし | 137 → 138 | 27.0 → 26.9 | 4892 → 4818 | - |
| obsidian、ギルドなし | 136 → 132 | 27.3 → 27.0 | 4902 → 4839 | - |
| broad + ギルド | 200 → 200 | 18.2 → 17.8 | 2830 → 2802 | 2074 → 2022 |
| 9 種すべて + ギルド | 200 → 200 | 15.7 → 15.3 | 3214 → 3180 | 1733 → 1681 |

品質は収入を膨らませません。客が支払う額には予算の上限があるので、キャンペーンあたりの通常の売上とギルドのゴールドは、以前の数値の数パーセント以内に
収まります。主に安価な素材の戦略が、客の最低 FIT を満たしやすくなって有利になります (200 中 +15〜+21 クリア)。steel と obsidian のボットは誤差の範囲
(+1 / -4) で、キャンペーンは 0.1〜0.7 日短くなります。品質が能力値を下げることはないので、レシピを *基本* FIT で選ぶギルドのボットも、
要求される FIT に常に届きます。何も調整していません。調整が必要になったら、`QUALITY_RATE_PCT`、`QUALITY_DEFS[].mult_pm`、
`BAL.quality_forge_shift_pct` だけを変えてください。

**The Ascendant Era の影響** (BOT 400 キャンペーン、導入前 → 後。`bots` テストの 40 キャンペーンの表も同じ傾向):

| 指標 | 導入前 → 後 |
|---|---|
| 観戦 BOT (ギルドあり、通常キャンペーン) のクリア率 | 400/400 → 400/400 |
| 同・平均クリア日 | 14.48 → 14.19 |
| broad、ギルドなし (40 キャンペーン) | 37 → 39 クリア、25.2 → 24.8 日 |
| steel/silver、ギルドなし | 29 → 34、26.7 → 26.2 日 |
| cheap (copper)、ギルドなし | 36 → 38、25.2 → 25.3 日 |
| obsidian、ギルドなし | 27 → 32、26.8 → 26.8 日 |
| broad + ギルド / cheap + ギルド | 40/40 → 40/40、17.7 → 17.1 日 / 17.8 → 16.9 日 |

回帰はない (すべて同等以上)。通常キャンペーンの客・市場の乱数列は変わらない (`rng_streams` の固定値が不変) ので、差は日替わりイベント
(Market Caravan の割引と Arcane Convergence の高品質化が 7 日に 1 回ずつ) によるもので、数日分の短縮と、品質に依存する戦略のクリア数の増加として現れる。
新しい客・設計図・特別契約は 75 件以上でしか現れないので、通常キャンペーン (平均 28 件、最大 34 件) には影響しない。
エンドゲームの BOT (70 件開始、50 日) は 12/12 シードで Ascendant Smith に到達し、Relic Expedition を平均 8 回納品する。
Astral Shard は BOT が使わないため在庫が増えるだけ (60 日で最大 30)。上限は報酬の受け取り可否の判定で守られる。

## コードスタイル

C ソース・ヘッダは、リポジトリ直下の `.clang-format` (LLVM ベース、インデント 4、桁幅 120、タブなし) で整形します。

```sh
clang-format -i src/*.c src/*/*.c src/*/*.h tests/*.c
```

コメントと API の説明は日本語で書きます。ただし、識別子、ログ文字列、セーブのキー、書式指定子、ゲーム画面に表示する文字列 (`src/ui/text.h` と
コンテンツのテーブル) は翻訳しません。整形だけの変更とロジックの変更は、別のコミットに分けてください。
