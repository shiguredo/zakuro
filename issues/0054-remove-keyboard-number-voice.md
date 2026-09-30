# キーボード操作と数字音声機能を削除する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/remove-keyboard-number-voice
- Polished: {YYYY-MM-DD}

## 目的

接続ごとの数字音声再生と、それをキーボードから操作する機能を削除する。この機能は対話端末での確認を前提としており、サーバーや CI などの非対話環境では起動時にキーボード監視スレッドが `tcgetattr` に失敗してエラーログを出すだけで機能しない。また、数字音声の再生のためだけに埋め込み WAV リソース (44 ファイル) と専用のオーディオ経路を維持しており、利用場面に対して保守コストが見合わない。

## 現状

### キーボード操作

- `src/game/game_key_core.h` の `GameKeyCore` が `stdin` を raw モードにしてキー入力監視スレッドを起動する。`src/main.cpp` の `main` が起動時に `key_core->Init()` を呼び、`ZakuroConfig::key_core` として全インスタンスで共有する。
- `src/game/game_key.h` の `GameKey` がキー入力をキューに溜め、`src/fake_audio_key_trigger.h` の `FakeAudioKeyTrigger` が 100 ミリ秒ごとに取り出して次を実行する。
  - 数字キー: 対応するクライアントの番号を数字音声で再生する
  - `q` + 数字: 指定クライアントを切断する、`Q` + 数字: 再接続する
  - `s`: 全シナリオを一時停止する、`S`: 再開する
- 非対話環境では起動時に `failed to tcgetattr: 19` が標準エラー出力に出る (実行して確認済み)。

### 数字音声

- `src/voice_number_reader.h` の `VoiceNumberReader` が `resource/num*.wav` (44 ファイル) を `EmbeddedBinary` 経由で読み込み、0〜99 の数字音声を組み立てる。
- `src/game/game_audio.h` の `GameAudio` / `GameAudioManager` がクライアントごとの音声バッファを管理し、`ZakuroAudioDeviceModuleConfig::Type::External` 経由で送信音声に載せる。
- `src/zakuro.cpp` の `Zakuro::Run` は `config_.fake_audio_capture` が空のとき `fake_audio_key_trigger` を true にして `GameAudioManager` と `FakeAudioKeyTrigger` を生成し、`VirtualClientConfig::AudioType::External` を選ぶ。`--fake-audio-capture` 未指定時のデフォルト音声は数字音声を除いて無音になる。
- 同じ分岐でシナリオに `PlayVoiceNumberClient` を挟む。`src/scenario_player.h` の `OP_PLAY_VOICE_NUMBER_CLIENT` が `VoiceNumberReader` と `GameAudioManager` で数字音声を再生する。
- `--scenario` は `fake_audio_key_trigger` が true のときだけ参照され、`--fake-audio-capture` 指定時は無視される。
- `CMakeLists.txt` の `RESOURCE_FILES` に `num*.wav` が 44 件登録されている。
- `--fake-audio-capture` 未指定時に選ばれる想定の `AudioType::AutoGenerateFakeAudio` は、`fake_audio_key_trigger` の条件で必ず外れるため到達しないコードになっている。

### 関連 issue

- issues/0005 (FakeAudioKeyTrigger の寿命) と issues/0006 (GameKeyCore のデータレース) は削除対象コードのバグ修正である。
- issues/0025 (ScenarioPlayer の寿命と状態) は `FakeAudioKeyTrigger` が `ScenarioPlayer*` を保持する点に言及している。
- issues/0038 (未使用シンボルの削除) は `game_audio.h` の `PlayAny` を、issues/0040 (make_unique 化) は削除対象ファイルを対象に含む。
- issues/pending/0050 (スポットライトの連続音声) は `GameAudioManager` を前提とした設計になっている。
- issues/0018 (README のヘルプ抜粋) と issues/0041 (Zakuro::Run と Util::ParseArgs の分割) は、README のヘルプ抜粋と `--scenario` の定義に影響する。

## 設計方針

- キーボード操作: `src/fake_audio_key_trigger.h` / `src/game/game_key.h` / `src/game/game_key_core.h` を削除する。`ZakuroConfig::key_core` と `src/main.cpp` の `GameKeyCore` 生成・各 config への代入も削除する。
- 数字音声: `src/voice_number_reader.h` / `src/game/game_audio.h` を削除する。`resource/num*.wav` (44 ファイル) を削除し、`CMakeLists.txt` の `RESOURCE_FILES` から `num*.wav` の登録を削除する (`Kosugi-Regular.ttf` は残す)。
- オーディオ経路: `VirtualClientConfig::AudioType::External` と `render_audio` / `sample_rate` / `channels`、`ZakuroAudioDeviceModuleConfig::Type::External` と `render` / `sample_rate` / `channels`、`src/zakuro.cpp` の `GameAudioManager` 生成を削除する。
- デフォルト音声: `--fake-audio-capture` 未指定時は、`External` を削除することで既存の到達不能分岐 `AudioType::AutoGenerateFakeAudio` (Safari) が選ばれるようにする。新しい実装は不要。現状は無音のためデフォルトの音声が変わることは許容する。
- シナリオ: `src/scenario_player.h` から `OpPlayVoiceNumberClient` / `voice_reader_` / `ScenarioPlayerConfig::gam` を削除する。`src/zakuro.cpp` の `fake_audio_key_trigger` 条件分岐を削除して、シナリオを Reconnect → DataChannel サブシナリオ → `add_reconnect_scenario` の一本にまとめる。
- `--scenario`: 参照箇所が無くなるため、CLI オプションと `ZakuroConfig::scenario`、JSONC の `scenario` を削除する。
- 後始末: 0005 / 0006 は実装時に closed にする。0025 / 0038 / 0040 / 0018 / 0041 は実装時に反映状況を確認する。pending/0050 は `GameAudioManager` 前提の設計を見直す。

## 完了条件

- `GameKeyCore` / `GameKey` / `FakeAudioKeyTrigger` / `VoiceNumberReader` / `GameAudio` / `GameAudioManager` が削除されていること
- `External` オーディオタイプ関連の配線 (`VirtualClientConfig` / `ZakuroAudioDeviceModuleConfig` / `Zakuro::Run`) が削除されていること
- `resource/num*.wav` と `CMakeLists.txt` の `RESOURCE_FILES` の登録が削除されていること
- `--fake-audio-capture` 未指定時に Safari (`AutoGenerateFakeAudio`) の音声が送信されること
- `--scenario` が削除されていること
- 非対話環境で `failed to tcgetattr` が出力されないこと
- ビルドとテストが通ること

## 解決方法

(実装時に記入)
