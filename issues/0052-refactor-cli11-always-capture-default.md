# ヘルプテキストに出力するデフォルト値を always_capture_default() を使用したものに変更する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-cli11-always-capture-default
- Polished: 2026-09-30

## 目的

ヘルプテキストのデフォルト値をバインド済み変数の値から自動生成し、手書きによる記載漏れや食い違いをなくす。

## 現状

- `src/util.cpp` の `Util::ParseArgs` が `CLI::App` に全オプションを定義している。`app.option_defaults()->take_last();` は設定済みだが `always_capture_default()` は使っていない。
- デフォルト値は説明文字列に `(default: 1)` や `(default: VGA)` のように手書きで追記している (例: `--vcs`, `--resolution`, `--sora-data-channel-signaling-timeout`)。
- 実際のデフォルト値は次の 2 か所に定義されており、ヘルプの手書き文字列とは二重管理になっている。
  - `src/zakuro.h` の `ZakuroConfig` のメンバー初期化子 (`vcs = 1`, `resolution = "VGA"`, `sora_data_channel_signaling_timeout = 180` など)
  - `src/main.cpp` のローカル変数の初期化子 (`log_level = webrtc::LS_NONE`, `instance_hatch_rate = 1.0` など)
- `DEPS` の CLI11 は `v2.6.2` で、`always_capture_default()` が利用できる。
- CLI11 v2.6.2 で `always_capture_default()` を有効化したところ、単純に切り替えると表示が不適切になるオプションがある。
  - `--log-level`: `int` に `CLI::CheckedTransformer` を適用しているため、デフォルトが `[4]` (webrtc::LS_NONE) と数値で表示される。レベル名ではないため意味が伝わらない。
  - `bool` に `CLI::CheckedTransformer(bool_map)` を適用しているオプション
    (`--initial-mute-video`, `--initial-mute-audio`, `--sora-video`, `--sora-audio`,
    `--sora-simulcast`, `--sora-spotlight`) は `[0]` / `[1]` と表示される。`false` /
    `true` にはならない。なお `--sora-spotlight` の現行の説明文字列は
    `(default: none)` であるが、実際の初期値は `false` (`src/zakuro.h` の
    `ZakuroConfig::sora_spotlight`) である。
  - `--sora-signaling-url`: `std::vector<std::string>` の空デフォルトが `[{}]` と表示される。
  - `--sora-video-bit-rate` / `--sora-audio-bit-rate` / `--sora-spotlight-number`: 0 が「未指定 (Sora 側で決定)」を意味するが、`[0]` ではその意味が伝わらない。
  - デフォルトが空文字列や `std::nullopt` のオプションはキャプチャ結果が空になり、現在の `(default: none)` の記載が消える。
  - `--degradation-preference` (`std::optional<webrtc::DegradationPreference>`) とエンコーダー系 (`std::optional<sora::VideoCodecImplementation>`) は `operator<<` がないためキャプチャ結果が空になる (コンパイルは通る)。
  - フラグ (`add_flag`) はキャプチャ結果が空になり、現在の `(default: false)` / `(default: true)` の記載が消える。
- 少なくとも CLI11 v2.6.2 の `app.parse` では、未指定オプションの実行時の値がキャプチャしたデフォルト文字列で上書きされることはない。変わるのはヘルプ表示だけである。

## 設計方針

- `app.option_defaults()->take_last()->always_capture_default();` を設定し、説明文字列から手書きの `(default: ...)` を削除する。
- キャプチャ結果が実態と合わないオプションは `->default_str(...)` で表示だけを補正する。補正するかどうかと表記はオプションごとに決める。
  - `--log-level`: `->default_str("none")`
  - ただしこの補正は現行実装 (`int log_level`、初期値 `webrtc::LS_NONE` = 4) を
    前提にしている。open の issues/0072 は `log_level` を `std::optional<int>` に
    変更し「未指定 = `LS_INFO` 相当」とする計画であり、`--log-level` の既定の表示の
    扱いは issues/0072 側でも未定と明記している。issues/0072 が本 issue より先に
    入ると、キャプチャ結果は `[4]` ではなく空になり (`std::optional<int>` の空は
    `to_string` の結果が空になる)、実効の既定値も `LS_INFO` 相当になるため
    `default_str("none")` は実態と矛盾する表示になる。実装時は issues/0072 の
    反映状況を確認し、issues/0072 が先の場合は表示しないか実効の既定値
    (`LS_INFO` 相当) を表す表記に調整すること
  - `bool` + `CheckedTransformer` 系: `->default_str("false")` / `->default_str("true")`
  - `--sora-signaling-url`: `->default_str("")` などで `[{}]` を表示させない
  - `--sora-video-bit-rate` / `--sora-audio-bit-rate` / `--sora-spotlight-number`: `->default_str("none")`
  - 空文字列 / `nullopt` デフォルトのオプション: 表示不要なら補正しない。現行の `(default: none)` 相当を残すものは `->default_str("none")`
  - フラグ: デフォルト表示が必要なものだけ `->default_str("false")` / `->default_str("true")` を設定する (CLI11 ではフラグにも `default_str` を設定できる)
- README.md のヘルプ抜粋の再生成は issues/0071 が担当している (issues/0018 の後継。issues/0018 は UI オプション廃止に伴い対応不要として closed、issues/0071 は現行実装のみを対象として作成された)。そのため本 issue では README.md を変更しない。issues/0071 は本 issue が先に入ると `--help` の表記形式が変わることを前提にしている (issues/0071 の現状参照)。実装時は issues/0071 の反映状況を確認する。
- ヘルプ出力のスタイルが `(default: ...)` から CLI11 の `[値]` 形式に変わることは許容する。

## 完了条件

- `src/util.cpp` の説明文字列から手書きの `(default: ...)` が削除されていること
- `zakuro --help` に表示されるデフォルト値が `ZakuroConfig` と `src/main.cpp` の実際の初期値と一致していること
- 補正対象のオプションで `[4]` / `[0]` / `[1]` / `[{}]` ではなく意味の伝わる表記が表示されること
- ビルドとテストが通ること

## 解決方法

(実装時に記入)
