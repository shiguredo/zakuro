# Zakuro::Run と Util::ParseArgs の責務分割

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-zakuro-run-and-parse-args-split
- Polished: {YYYY-MM-DD}

## 目的

`Zakuro::Run` (639 行) と `Util::ParseArgs` (400 行超・引数 11 個) が巨大化し、
テスト・レビュー・変更のいずれもコストが高くなっている。責務単位に分割してメンテナンス性を上げる。

## 現状

### Zakuro::Run

`src/zakuro.cpp` の `Zakuro::Run` に以下がすべて詰め込まれている。

- `ParseDataChannels` (150 行)
- capturer 生成、audio 設定、ADM 設定 (`configure_dependencies` ラムダで 40 行超)
- コーデックプリファレンス生成
- signaling URL バリデーション
- Sora 設定への詰め替え (`sora_config.xxx = config_.xxx` が延々続く)
- シナリオ生成 (`add_reconnect_scenario` ラムダ、シナリオ設定 100 行超)
- ScenarioPlayer / トリガ / タイマーの起動
- ioc.run() と後片付け

400 行超の 1 メソッドで、テストも書けない。

### Util::ParseArgs

`src/util.cpp` の `Util::ParseArgs` は引数 11 個で 400 行超。
`config_file` / `log_level` / `http_host` / `http_port` / `ui` / `ui_remote_url` / `connection_id_stats_file` /
`instance_hatch_rate` / `config` / `ignore_config` の順で受け取り、順序依存が強い。

## 設計方針

### Zakuro::Run の分割

- `ParseDataChannels` を別 TU (`src/data_channels_parser.cpp` など) に切り出す
- `configure_dependencies` ラムダの内容を `Zakuro::CreateSoraContextConfig(...)` などのメンバー関数に切り出す
- シナリオ生成を `Zakuro::BuildScenario(...)` に切り出す
- Sora 設定への詰め替えを `Zakuro::BuildSoraSignalingConfig(...)` に切り出す

### Util::ParseArgs の分割

- 引数を構造体で受け渡す。`struct ParseArgsOutput { std::string config_file; int log_level; ...; }`
- あるいは `ZakuroConfig` に非インスタンス系オプションを統合し、引数を 2〜3 個に減らす
- CLI 定義 (add_option / add_flag) をカテゴリ別のヘルパー関数に切り出す (アプリ全体オプション、シグナリング、映像、音声、コーデック実装など)

## 完了条件

- `Zakuro::Run` が 200 行以下になっていること
- `Util::ParseArgs` の引数が構造体または 3 個以下に減っていること
- `ParseDataChannels` が別 TU にあり、単体テストが書ける形になっていること
- 既存の CLI 挙動と設定ファイル挙動が変わらないこと (回帰テストで確認)
