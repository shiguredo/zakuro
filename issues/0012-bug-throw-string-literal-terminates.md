# throw の対象が文字列リテラルで std::terminate に至る

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-throw-string-literal-terminates
- Polished: {YYYY-MM-DD}
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` の video decoder factory ラムダで `throw "Invalid implementation";` と
`const char*` を投げており、上位に `catch(const std::exception&)` があっても捕まえられず
`std::terminate` に至る経路を修正する。

## 現状

`src/zakuro.cpp` の `context_config.video_codec_factory_config.create_video_decoder` ラムダは
`implementation` が想定外の値 (`kCustom_1` 以外) の場合に `throw "Invalid implementation";` を実行する。

`const char*` は `std::exception` を継承しないため、Sora SDK 側で `catch(const std::exception&)` していても
捕まらず、`std::terminate` へフォールバックする。
`main` にも try は無いため、いずれにせよプロセスが強制終了する。

## 設計方針

`throw std::runtime_error("Invalid implementation");` に置き換える。

`std::exception` を継承する型を投げることで、上位の catch が機能する。
Sora SDK 側の catch がなくても、少なくとも `terminate_handler` にはより有用なエラーが渡る。

## 完了条件

- `throw "..."` のような文字列リテラルの throw がコードベースから 0 件になること
  (`git grep -n 'throw "' src/`)
- 対応するテストケース（`implementation` が `kCustom_1` 以外を渡す等）で
  `std::runtime_error` として例外を捕捉できること
