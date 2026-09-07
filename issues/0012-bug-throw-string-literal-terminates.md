# throw の対象が文字列リテラルで std::terminate に至る

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-throw-string-literal-terminates
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` の video decoder factory ラムダで `throw "Invalid implementation";` と
`const char*` を投げている箇所を修正する。`const char*` は `std::exception` を継承しないため、
万一この分岐が実行されると `catch(const std::exception&)` では捕捉できず、
未捕捉例外として `std::terminate` に至る。

## 現状

`src/zakuro.cpp` の `context_config.video_codec_factory_config.create_video_decoder` ラムダは
`implementation` が `kCustom_1` 以外の場合に `throw "Invalid implementation";` を実行する。

ただし、現行実装ではこの分岐は実行されない。デコーダの実装は `preference` 構築の最後で
`CreateVideoCodecPreferenceFromImplementation(
capability, sora::VideoCodecImplementation::kCustom_1)` を Merge するため、
preference 内のデコーダは常に `kCustom_1` になる。Sora C++ SDK 側
(`sora_video_codec_factory.cpp` の `CreateVideoCodecFactory`) も、`kCustom_*` については
preference の値をそのまま `create_video_decoder` に渡すだけで、
これ以外の値が渡る経路は存在しない。つまりこの throw は現時点では到達不能なコードであり、
本 issue は「万一実行された場合に `std::terminate` に至る」という潜在的な問題と、
文字列リテラルの throw の是正を目的とする。

もしこの分岐が実行されると、`const char*` は `std::exception` を継承しないため、
Sora SDK 側の `SoraVideoDecoderFactory::Create` (try/catch なし) を通過して未捕捉のまま
上位へ伝播し、`std::terminate` に至る。`src/main.cpp` の `main` にも try ブロックはない。

## 設計方針

`throw std::runtime_error("Invalid implementation");` に置き換える。

`std::exception` を継承する型を投げることで、上位に `catch(const std::exception&)` が
あれば捕捉できるようになる。SDK 側の catch がなくても未捕捉例外として `std::terminate` に
至ることは変わらないが、`std::exception` を継承した型であれば terminate 時の診断情報
(`what()` のメッセージ) が残りやすくなる。`src/util.cpp` の既存の throw もすべて
`std::runtime_error` を用いており、コードベースの規約にも合致する。

なお、`src/zakuro.cpp` は `#include <stdexcept>` を直接インクルードしていない。
推移的なインクルードに依存したくない場合は、修正時に `#include <stdexcept>` を追加すること。

## 完了条件

- `git grep -n 'throw "' src/` の結果が 0 件になること
  (現状は `src/zakuro.cpp` の 1 件のみ)
- `src/zakuro.cpp` の `context_config.video_codec_factory_config.create_video_decoder` ラムダが
  `throw std::runtime_error(...)` を使うこと
- ビルドに成功し、既存の E2E テスト (`test/test_zakuro.py`) が通ること
