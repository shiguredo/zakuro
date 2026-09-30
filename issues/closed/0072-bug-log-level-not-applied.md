# `--log-level` がログの出力を絞り込めていない

- Created: 2026-09-29
- Completed: 2026-09-30
- Branch: feature/fix-log-level-not-applied
- Polished: 2026-09-30

## 目的

`--log-level` に `none` を指定しても stderr に `LS_INFO` / `LS_ERROR` のログが出続けており、
利用者が指定したログレベルでログの量を制御できない。負荷試験では 1 インスタンスあたり
大量のログが出るため、`--log-level none` が効かないことはログ量の見積もりとディスク消費に
直結する。

## 現状

`src/main.cpp` の `main` は `--log-level` の値 (`int log_level`、既定は `webrtc::LS_NONE`) を
`webrtc::LogMessage::LogToDebug` に渡している。しかし libwebrtc m150.7871.3.1 では
この API は stderr の出力を制御しない。

理由は `rtc_base/logging.cc` にある。stderr へ出すかどうかの判定は
`LogMessage::~LogMessage` の以下の条件で行われ、使うのは `LoggingConfig` の
`debug_severity_` である。

```cpp
const LoggingConfig& config = GetLoggingConfig();
if (log_line_.severity() >= config.debug_severity()) {
  OutputToDebug(log_line_);
}
```

一方 `LogMessage::LogToDebug` は `g_dbg_sev` を更新するだけで、`debug_severity_` には
触れない。`LogToDebug` は `UpdateMinLogSeverity` を呼ぶため、現行の出力は
「`LogToDebug` が `g_min_sev` を下げる」ことで成立している。

```cpp
void LogMessage::LogToDebug(LoggingSeverity min_sev) {
  g_dbg_sev.store(min_sev, std::memory_order_relaxed);
  MutexLock lock(&GetLoggingLock());
  UpdateMinLogSeverity();
}
```

`UpdateMinLogSeverity` は `g_dbg_sev` と `LoggingConfig` の `min_severity`、各シンクの
重大度の最小値を `g_min_sev` に入れる。`LogMessage::IsNoop` は `severity < g_min_sev`
で判定し、`RTC_LOG(sev)` は `!IsNoop<sev>()` が真でなければ本文ごと実行しない。
`g_min_sev` / `g_dbg_sev` の初期値は `kDefaultLoggingSeverity` であり、
`-DNDEBUG` のビルドでは `LS_NONE` である。そのため `UpdateMinLogSeverity` を
一度も呼ばないと全重大度で `IsNoop` が真になり、ログが 1 行も出ない。

`LoggingConfig` の既定値は `min_severity_ = LS_INFO` / `debug_severity_ = LS_INFO`
(`rtc_base/logging.h`) であり、`debug_severity_` が `LS_INFO` のままなので
`LS_INFO` 以上のログがすべて stderr に出る。

`LoggingSeverity` の enum は `LS_VERBOSE`(0) / `LS_INFO`(1) / `LS_WARNING`(2) /
`LS_ERROR`(3) / `LS_NONE`(4) であり `LS_NONE` は最大値である。`LogToDebug` に渡す値は
`UpdateMinLogSeverity` で `std::min` の引数になるため `LS_NONE` は「すべて通す」を
意味するが、`min_severity` / `debug_severity` は `severity >= しきい値` の比較に使う
しきい値であるため、同じ `LS_NONE` は逆に「すべて抑止する」を意味する。

ログファイルは `src/main.cpp` の `InstalledFileLogSink` が `FileRotatingLogSink` を
`LogMessage::AddLogToStream(sink, webrtc::LS_INFO)` で登録しており、シンクの重大度が
`LS_INFO` に固定されているため、`--log-level` の値はログファイルにも効かない。

### `--log-level` 未指定と `none` が区別できない問題

`src/main.cpp` の `log_level` は `int` で初期値が `webrtc::LS_NONE` であり、
`src/util.cpp` の `log_level_map` も `none` を 4 に割り当てている。このため
「`--log-level` を指定しない」と「`--log-level none` を指定した」が同じ値になり、
区別できない。`LS_NONE` をそのまま `min_severity` / `debug_severity` に設定すると
既定で全抑止になってしまうため、実装では未指定を区別する必要がある。

確認したこと (実バイナリ `_build/macos_arm64/release/zakuro/zakuro` で実測):

- `--log-level` を `none` / `info` / `warning` / `error` / `verbose` の 5 通りで実行しても
  stderr はすべて 64 行で、正規化した内容も一致した (差分はポインタ値のみ)
- `--log-level none` を指定しても `src/main.cpp` の
  `file descriptor limit: required=... soft=... hard=...` の `LS_INFO` ログが stderr に出る
- `LogToDebug` の引数を `LS_ERROR` 固定に変えてビルドしても stderr は 64 行のままで、
  行数は 1 行も減らない (`LogToDebug` が制御点でないことの確認)
- `InitializeLogging` に `set_min_severity(LS_ERROR)` と `set_debug_severity(LS_ERROR)` を
  設定して `LogToDebug` を置き換えると、stderr は 4 行になり `LS_ERROR` 以上だけが出る
- `LogToDebug` を残したまま `InitializeLogging` を直後に足すと、戻り値が `false` になり
  stderr は 65 行のまま (`LogToDebug` が `GetOrInitConfig` 経由で設定の初期化を
  使い切るため)
- ログファイル (`./webrtc_logs_0`) は `--log-level none` と `error` のどちらでも 63 行で、
  正規化した内容も一致した
- `--log-level bogus` は CLI11 が 105 で弾くため、オプション自体は解析されている

`README.md` の `--log-level` の説明は `--help` の抜粋で、`verbose` / `info` / `warning` /
`error` / `none` を受け付ける旨のみであり、実際にはどの値でも同じ出力になることは
書かれていない。

`test/test_main_resource.py` の `test_valid_config_is_converted_to_arguments` は設定ファイルで
`"log-level": "error"` を指定しながら `LS_INFO` の
`HTTP_SERVER_STARTED_MARKER` (`test/test_helpers.py`) を同期点にしている。このテストは
現行実装で pass しており、`--log-level` が効いていないことの裏付けにもなっている。

## 設計方針

`--log-level` を「未指定」と「`none` などの指定値」に区別できるようにしたうえで、
`LogMessage::LogToDebug` を `webrtc::InitializeLogging` に置き換える。実測で stderr の
絞り込みが機能することを確認した唯一の経路である。

### 実効値の決め方

`InitializeLogging` に渡す重大度 (以下、実効値) を次のように決める。

- `--log-level` 未指定: `LS_INFO` (現行の既定の出力と同じ)
- `--log-level <値>` 指定: 指定された重大度 (`none` は `LS_NONE`)

**未指定でも `InitializeLogging` を必ず 1 回呼ぶ**。呼ばないと `UpdateMinLogSeverity` が
走らず `g_min_sev` が `LS_NONE` のままになり、`IsNoop` が全重大度で真になって
ログが 1 行も出なくなる (ログ機構そのものが止まる)。実測でも、何も設定せず
`RTC_LOG` を呼ぶと stderr は 0 行になり、`InitializeLogging` に `LS_INFO` を渡すと
`LS_INFO` 以上が出ることを確認した。

### `InitializeLogging` に渡す `LoggingConfig`

- `set_min_severity(実効値)` と `set_debug_severity(実効値)` の両方を設定する。
  `min_severity` は `g_min_sev` のゲート、`debug_severity` は stderr へ出す条件
  (`severity >= debug_severity`) に使われる
- `set_log_timestamp(true)` と `set_log_thread(true)` を設定する。現行の `main` は
  `LogTimestamps()` と `LogThreads()` を呼んでおり、`LogThreads()` が有効にするのは
  `LoggingConfig::log_thread` である。`InitializeLogging` に渡した `LoggingConfig` は
  `GetOrInitConfig` がムーブしてそのままグローバルな設定になるため、`set_log_thread(true)`
  で現行と同じ `[000:001][259] (main.cpp:121): ...` の形式 (スレッド ID 付き) を保てる
  (`InitializeLogging` が明示的に設定へ書き戻すのは静的変数に置かれている
  `log_timestamp` / `log_queue_name` / `log_to_stderr` と `g_dbg_sev` の分だけだが、
  `log_thread` は設定そのものに入るため `LogMessage` から読まれる)。`set_log_queue_name`
  は使わない (`LogThreads` は有効にしておらず、有効にすると
  `[スレッド ID:キュー名]` 形式になって現行と変わる)
- `InitializeLogging` を呼んだら `LogToDebug` / `LogTimestamps` / `LogThreads` は
  呼ばない

### `InitializeLogging` を使うときの制約

- 設定が反映されるのは `LoggingConfig` が最初に初期化される 1 回だけである。
  `GetOrInitConfig` は静的初期化ラムダで `config_applied` を立てるため、
  `InitializeLogging` だけでなく `LogToDebug` / `LogThreads` / `AddLogToStream` /
  最初の `RTC_LOG` もこの 1 回を使い切る。`LogToDebug` を残したまま
  `InitializeLogging` を足すと戻り値 `false` になり無視されるため、
  **`LogToDebug` は必ず置き換える (併用しない)**
- `InitializeLogging` は `src/main.cpp` の現在の `LogToDebug` の位置 (引数解析の後、
  ログファイルのシンクを作る前) に置く。この位置より前に `RTC_LOG` は無く、
  `Util::ParseArgs` の出力は `std::cerr` である。ログファイルのシンクを作る前に
  置くことで、`log_sink->Init()` が失敗したときの `LS_ERROR` も現行どおり出力される
- ログファイルは `LoggingConfig::AddSink` に移さない。`AddSink` には重大度を指定する
  手段が無く、シンクの既定が `LS_INFO` 固定になるため、`--log-level` をログファイルへ
  適用できなくなる。`AddLogToStream` を維持し、実効値を渡す
- `--log-level none` のときは実効値が `LS_NONE` になり、`g_min_sev` が `LS_NONE` に
  なって `RTC_LOG` が呼び出し側で短絡するため、stderr とログファイルのどちらにも
  1 行も出ない (シンクの重大度に `LS_NONE` を渡すと `severity >= min_severity_` は
  常に偽になるため二重に抑止される)。空の `webrtc_logs_0` は作られる

### 未指定と指定値の区別

- `src/main.cpp` の `log_level` を `std::optional<int>` にする。`Util::ParseArgs` の
  `int& log_level` も `std::optional<int>&` に合わせる (`src/util.h` の宣言と
  `src/util.cpp` の定義)。CLI11 は `std::optional<int>` を扱え、未指定なら
  `std::nullopt` のままになることを実測で確認した (`--log-level none` は 4、
  `--log-level bogus` は 105 で CLI11 が弾く)
- 設定ファイル (JSONC) のトップレベル `log-level` は `src/main.cpp` で
  `--log-level <値>` に変換されて CLI と同じ経路に入るため、設定ファイルでの指定も
  「指定」として扱う。CLI と設定ファイルは区別しない

### 既定値を変更しない理由

既定値を変えると、リリース済みの 2026.1.0-canary.8 と比べて既定のログ出力が減り、
利用者が原因を確認できなくなる。既存のテストも既定のログ出力を前提にしている。
既定値の見直しは破壊的な変更にあたるため本 issue では扱わず、別途判断する。
なお、`--log-level` の既定の表示は未指定が `LS_INFO` 相当になることで `none` ではなく
なるため、その扱いも別途判断する。

## 完了条件

- `--log-level none` を指定した場合、stderr とログファイルのどちらにもログが
  出力されないこと (空の `webrtc_logs_0` が作られることは許容する)
- `--log-level error` を指定した場合、`LS_ERROR` 以上 (すなわち `LS_ERROR` のみ) のログが
  stderr とログファイルに出力されること
- `--log-level verbose` を指定した場合、`LS_VERBOSE` 以上のログが出力されること
- `--log-level` を指定しない場合は、現行と同じ `LS_INFO` 以上のログが stderr と
  ログファイルに出力されること (リリース済みの挙動を変えない)
- ログの出力形式が現行と同じであること。`[タイムスタンプ][スレッド ID] (ファイル:行):`
  の形式が保たれ、`LogToDebug` を `InitializeLogging` に置き換えた結果として
  スレッド ID が消えたり `キュー名` が増えたりしないこと
- `test/test_main_resource.py` の `test_valid_config_is_converted_to_arguments` が
  pass すること。このテストは設定ファイルで `"log-level": "error"` を指定しながら
  `LS_INFO` のログ (`test/test_helpers.py` の `HTTP_SERVER_STARTED_MARKER`) を同期点に
  しており、`--log-level` が実際に効くようになると同期点が現れなくなる。同期点を
  `LS_ERROR` 以上のログに変えるか、ログレベルを `info` に変えるか、同期点に依存しない
  待ち方に変える。ログレベルを変える場合は、同テストの期待引数の
  `--log-level error` も合わせて更新する
- 既定のログ出力に依存している他のテスト (`test/test_helpers.py` の
  `HTTP_SERVER_STARTED_MARKER` / `DATA_CHANNELS_SENDING_MARKER` を使い、`--log-level` を
  指定していないもの) が、既定の出力を変えないため引き続き pass すること
- `python3 run.py build macos_arm64` などのビルドが通ること
- `CHANGES.md` の `## develop` に `[FIX]` のエントリを追加すること

## 解決方法

`src/main.cpp` の `LogMessage::LogToDebug` / `LogTimestamps` / `LogThreads` を
`webrtc::InitializeLogging` に置き換え、`LoggingConfig` に `set_min_severity` /
`set_debug_severity` / `set_log_timestamp(true)` / `set_log_thread(true)` を設定した。
設定が反映されるのはログ機構の初期化 1 回だけのため、引数解析の直後 (ログファイルの
シンクを作る前) で一度だけ呼ぶ。

`log_level` を `std::optional<int>` にし、「`--log-level` 未指定」と
「`none` (= `LS_NONE`)」を区別できるようにした。未指定の実効値は `LS_INFO` にする。
`Util::ParseArgs` の `log_level` も `std::optional<int>&` に合わせた。

ログファイルは `LoggingConfig::AddSink` に移さず `LogMessage::AddLogToStream` を
維持し、`InstalledFileLogSink` に実効値を渡すようにした。`AddSink` には重大度を
指定する手段が無く、シンクの既定が `LS_INFO` 固定になるためである。

`test/test_log_level.py` を追加し、`none` / `error` / 未指定 / `verbose` の絞り込みと
ログの形式を検証するようにした。`test/test_main_resource.py` の
`test_valid_config_is_converted_to_arguments` は設定ファイルの `log-level` を `info` に
変更し、`LS_INFO` の同期点と合わせた。

検証したこと (実バイナリ `_build/macos_arm64/release/zakuro/zakuro` で実測):

- `--log-level none`: stderr はログ 0 行 (`failed to tcgetattr` などログ機構を経由しない
  出力のみ)、`webrtc_logs_0` は 0 バイトになる
- `--log-level error`: stderr とログファイルに `LS_ERROR` のみが出る
- `--log-level verbose`: stderr は 76 行になり、`LS_VERBOSE` のログ (AEC3 の設定) が出る
- `--log-level` 未指定: stderr は 64 行で、`--log-level info` と一致する (差分は
  タイムスタンプとスレッド ID のみ)
- ログの形式は `[タイムスタンプ][スレッド ID] (ファイル:行):` のまま変わらない
- `python3 run.py build macos_arm64` が成功する
- `uv run pytest -q` が 119 passed / 1 skipped (実 Sora 接続テストのみ skip) で通る
