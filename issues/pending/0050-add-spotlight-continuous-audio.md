# スポットライトで音を出し続けるモードを追加する

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-spotlight-continuous-audio
- Polished: {YYYY-MM-DD}
- Reporter: @shino

## 目的

スポットライト利用時に、仮想クライアントが音声を出し続けるモードを追加する。
現状はキーを押した瞬間に番号読み上げ音声を 1 回再生するだけで、
スポットライトのフォーカス先を切り替えたときの音声ミックスを確認しづらい。
sora-demo の fake のように適当な音を出し続けられるようにし、
キー操作で仮想クライアント単位に無音 / 復帰を切り替えられるようにする。

## 現状

`--fake-audio-capture` 未指定の場合、`Zakuro::Run` は `fake_audio_key_trigger` を true にして
`VirtualClientConfig::AudioType::External` を選び、`GameAudioManager::AddGameAudio` が返す
`render_audio` を仮想クライアントの音声ソースにする。

`src/fake_audio_key_trigger.h` の `FakeAudioKeyTrigger` は 100 ミリ秒ごとにキー入力を読み、
数字キー `1`〜`9`、`0` で `VoiceNumberReader::Read` の番号読み上げ WAV を
`GameAudioManager::Play` で 1 回だけ再生する。キー入力が無い間は無音であり、
音声を出し続けるモードは存在しない。

スポットライトは `ZakuroConfig` の `sora_spotlight` / `sora_spotlight_number` /
`sora_spotlight_focus_rid` / `sora_spotlight_unfocus_rid` で指定し、`Zakuro::Run` で
`sora::SoraSignalingConfig` の同名フィールドへ転送している。

仮想クライアントごとに独立した音声を扱うには、現状の音声ソースの構成を見直す必要がある。
`Zakuro::Run` の `gam->AddGameAudio(16000)` は 1 回しか呼ばれておらず、
`ScenarioPlayer` の `OP_PLAY_VOICE_NUMBER_CLIENT` は `gam->Play(client_id, buf)` を
クライアント ID 付きで呼ぶ。仮想クライアントごとに無音 / 復帰を切り替えるには、
クライアント数分の `GameAudio` をどのように登録・対応付けるかの設計が要る。

## 設計方針

常時音声出力モードを追加する。`FakeAudioKeyTrigger` 相当のキー処理を拡張し、
仮想クライアント単位で「出力中 / 無音」の状態を持つ。

- 音源は既存の `GameAudioManager` の仕組みを使い、出力中のクライアントへ連続して
  音声バッファを供給し続ける。音の種類は次のいずれかとする。
  - 番号読み上げを「イチイチイチ...」と間断なく再生し続ける
  - sora-demo の fake のように一定のトーンやノイズを出し続ける
- キー操作でクライアントごとに無音 / 復帰をトグルする。例として数字キー `1` を押すと
  1 番を無音にし、もう一度 `1` を押すと復帰させる。既存の数字キーによる
  番号読み上げ再生との衝突をどう扱うかも設計に含める。
- 常時出力の有無は CLI オプションで切り替え、デフォルトは現状動作を維持する。
- 遅延フォーカスあり（次リリースのデフォルト）の場合に現状の zakuro では
  「フォーカスが取れない」という報告がある。常時音声出力とは独立した事象の可能性があり、
  切り分けが必要。別 issue に分離するかは設計時に対応する。

## 完了条件

- 常時音声出力モードを有効にすると、各仮想クライアントが連続して音声を送信し続けること
- キー操作で任意の仮想クライアントを無音 / 復帰できること
- デフォルト（モード無効）では現状のキー入力時のみの再生が維持されること
- スポットライトのフォーカスを切り替えたときに、対象クライアントの音声を継続して確認できること

## pending にした理由

現時点で実装予定が無い。加えて、キー操作と仮想クライアントの割り当て、
クライアント数分の `GameAudio` の登録方法、既存のキー割り当てとの衝突回避、
遅延フォーカス環境でのフォーカス取得問題の切り分けといった設計判断が必要なため pending とする。
