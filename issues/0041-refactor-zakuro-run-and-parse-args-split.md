# Zakuro::Run と Util::ParseArgs の責務分割

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-zakuro-run-and-parse-args-split
- Polished: 2026-09-08

## 目的

`Zakuro::Run` (約 430 行。 `src/zakuro.cpp` 全体は 639 行) と `Util::ParseArgs` (400 行超・引数 11 個) が巨大化し、
テスト・レビュー・変更のいずれもコストが高くなっている。責務単位に分割してメンテナンス性を上げる。

## 現状

### Zakuro::Run

`src/zakuro.cpp` (639 行) に以下がすべて含まれている。`Zakuro::Run` 本体は約 430 行で、
同一ファイルに static 関数 `ParseDataChannels` (約 160 行) がある。

- `ParseDataChannels` (約 160 行)
- capturer 生成、audio 設定、ADM 設定 (`configure_dependencies` ラムダで 40 行超)
- コーデックプリファレンス生成
- signaling URL バリデーション
- Sora 設定への詰め替え (`sora_config.xxx = config_.xxx` が延々続く)
- シナリオ生成 (`add_reconnect_scenario` ラムダを含むシナリオ構築 約 90 行)
- ScenarioPlayer / トリガ / タイマーの起動
- ioc.run() と後片付け

400 行超の 1 メソッドで、テストも書けない。

### Util::ParseArgs

`src/util.cpp` の `Util::ParseArgs` は引数 11 個 (第 1 引数の `args` を含む) で 400 行超。
`args` に続けて `config_file` / `log_level` / `http_host` / `http_port` / `ui` / `ui_remote_url` /
`connection_id_stats_file` / `instance_hatch_rate` / `config` / `ignore_config` の順で受け取り、順序依存が強い。

## 設計方針

### Zakuro::Run の分割

- `ParseDataChannels` を別 TU (`src/data_channels_parser.cpp` など) に切り出す。
  `DataChannels` 構造体は `Zakuro::Run` と `ParseDataChannels` の両方から使われるため、共有ヘッダ
  (`src/data_channels_parser.h`) に移して公開する。 `MESSAGE_SIZE_MIN` / `MESSAGE_SIZE_MAX` 定数も同ヘッダへ移す
- `configure_dependencies` ラムダの内容を `Zakuro::CreateSoraContextConfig(...)` などのメンバー関数に切り出す
- シナリオ生成を `Zakuro::BuildScenario(...)` に切り出す
- Sora 設定への詰め替えを `Zakuro::BuildSoraSignalingConfig(...)` に切り出す
- 上記 4 つだけでは `Zakuro::Run` に約 260 行が残り、完了条件の 200 行以下を満たせないため、
  capturer 生成、audio 設定 (audio_type の決定と `vc_config` への設定)、SoraClientContext 構築
  (コーデックプリファレンス生成と `create_video_decoder` を含む) もメンバー関数に切り出すこと。

### Util::ParseArgs の分割

- 引数を構造体で受け渡す。`struct ParseArgsOutput` に `args` 以外の 10 引数
  (`config_file` / `log_level` / `http_host` / `http_port` / `ui` / `ui_remote_url` /
  `connection_id_stats_file` / `instance_hatch_rate` / `config` / `ignore_config`) をまとめ、
  `Util::ParseArgs(const std::vector<std::string>& args, ParseArgsOutput& output)` の 2 引数にする
- 非インスタンス系オプションを `ZakuroConfig` に統合する案は採用しない。`ZakuroConfig` はインスタンス毎に
  コピーされて各スレッドへ渡るため (`src/main.cpp` のスレッド生成)、プロセス全体の値 (`http_host` など) を
  混ぜるべきではない
- CLI 定義 (add_option / add_flag) をカテゴリ別のヘルパー関数に切り出す
  (アプリ全体オプション、インスタンス毎オプション、シグナリング、映像、音声、コーデック実装など)

## 完了条件

- `Zakuro::Run` が 200 行以下になっていること
- `Util::ParseArgs` の引数が構造体になり、関数の引数が 3 個以下になっていること
- CLI 定義 (add_option / add_flag) がカテゴリ別のヘルパー関数に分離されていること
- `ParseDataChannels` が別 TU にあり、`DataChannels` 構造体がヘッダから参照できて単体テストが書ける形になっていること
- 既存の CLI 挙動と設定ファイル挙動が変わらないこと (回帰テストで確認)

## 関連する issue

- issue 0013 (ParseDataChannels の到達不能コード) と issue 0014 (ParseDataChannels の std::cout 除去) が
  同じ `ParseDataChannels` の内部を編集する open issue として存在する。両 issue は 1 つのブランチで
  まとめて対応してよいとされている。本 issue は `ParseDataChannels` を別 TU へ移動するため、
  0013・0014 を先に完了させるか、3 件を 1 つのブランチで同時に実装すること
