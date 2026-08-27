# ParseDataChannels の std::cout デバッグ痕跡を除去する

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-parse-data-channels-debug-cout
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` から呼ばれる `ParseDataChannels` に、開発中の一時デバッグと思われる
`std::cout << __LINE__ << std::endl;` が 10 箇所以上残っている。
本番ログには繋がっておらず、失敗理由も行番号だけでは伝わらないため、
`RTC_LOG(LS_ERROR)` で意味のあるメッセージに置き換えて痕跡を除去する。

## 現状

`src/zakuro.cpp` の `ParseDataChannels` の各エラーパスは
`std::cout << __LINE__ << std::endl; return false;` の形になっており、
以下のような箇所すべてで行番号だけを stdout に出している。

- `dcs.is_array()` が false のとき
- 各 channel の `label` / `direction` が欠落・型不正のとき
- `interval` / `size-min` / `size-max` が数値でない、または範囲外のとき

`std::cout` はプロジェクトのログ経路（`RTC_LOG`）に載らず、
負荷試験時に行番号が大量に出ても原因究明の役に立たない。
AGENTS.md の「ログメッセージは全て英語」規約から見ても不適切。

## 設計方針

`std::cout << __LINE__ << std::endl;` をすべて削除し、代わりに以下のいずれかを行う。

- `RTC_LOG(LS_ERROR) << "ParseDataChannels: <該当項目> is invalid";` に置き換える
- 具体的な理由（"label is missing", "interval must be positive" 等）を含む英語メッセージにする

例外的に「一時的にデバッグしたい」用途で残す必要はない。すべて `RTC_LOG(LS_ERROR)` の意味のあるメッセージに置換する。

## 完了条件

- `ParseDataChannels` に `std::cout` が 0 件になること (`git grep -n 'std::cout' src/zakuro.cpp`)
- エラーパスすべてで具体的な英語メッセージが `RTC_LOG(LS_ERROR)` に出ること
- 不正な `sora-data-channels` を渡した際、ログから原因が特定できること
