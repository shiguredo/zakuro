# Zakuro::Run の loop_index が未初期化のまま使われる分岐がある

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-loop-index-uninitialized-fallback
- Polished: 2026-09-08
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` の `int loop_index;` が特定の条件でどの分岐にも入らず、
未初期化のまま `const int li = loop_index + 1;` に使用される未定義動作の経路を修正する。

## 現状

`src/zakuro.cpp` の `Zakuro::Run` は `int loop_index;` を宣言後、以下の 3 分岐でのみ代入している。

- `!fake_audio_key_trigger` の場合
- `fake_audio_key_trigger && config_.scenario == ""` の場合
- `fake_audio_key_trigger && config_.scenario == "reconnect"` の場合

この if / else if チェーンには else が無い。そのためどの分岐にも該当しない場合は
`loop_index` が未初期化のまま `const int li = loop_index + 1;` に流れ、未定義動作になる。

現行 entrypoint では `config_.scenario` が CLI11 の `--scenario` バリデータ
(`src/util.cpp` の `CLI::IsMember({"", "reconnect"})`) で `""` と `"reconnect"` に絞られている。
JSONC 設定ファイルも `Util::ParseInstanceToArgs` で CLI 引数へ変換した後に同じバリデータを通して
再パースされる (`src/main.cpp` の `Util::ParseArgs` 呼び出し) ため、現行構成ではこの未初期化パスに到達しない。
ただし `Zakuro::Run` 単体では「`loop_index` は 3 分岐のいずれかで必ず代入される」という不変条件を
表現していない。`scenario` の許容値が増えた際に `Zakuro::Run` の分岐更新を忘れると、
また CLI パース以外で `ZakuroConfig` を構築する entrypoint (テストや将来のバインディングなど) が加わると、
未初期化の `loop_index` が使用され UB になる。

## 設計方針

以下 3 点を修正する。

- 宣言時に `int loop_index = 0;` で初期化する
- 想定外の scenario の検証を、`VirtualClient` を生成する前 (`vcs` の宣言と `io_context` の
  ブロックより前) に置き、英語のメッセージを `std::cerr` に出して 0 以外を返す
  (メッセージは AGENTS.md の規約に従い英語で出す)
- if / else if チェーンを total にする。`scenario == ""` の分岐を最後の `else` にし、
  `scenario == "reconnect"` の分岐をその直前に置く。検証により、キー入力トリガー利用時に
  ここへ来る値は `""` か `"reconnect"` だけになる

チェーンの末尾に `else { ... return 1; }` を置く案は採らない。`VirtualClient` を生成した後に
早期 return すると `vcs.clear()` を飛ばし、`VirtualClient::retry_timer_` が `io_context` より
後に破棄される未定義動作 (issue 0007 で修正済み) を再導入するためである。

これで `Zakuro::Run` 単体で `loop_index` の初期化が保証され、想定外の scenario に対しては
`Zakuro::Run` が明示的なエラーを返して後続処理を打ち切る。

`Zakuro::Run` の戻り値を `main` が終了コードへ反映する変更は issue 0031 (main.cpp のリソース管理)
で扱い、0031 は完了済みである。そのため本 issue のエラーはプロセスの終了コード 1 として
観測できる。ただし現行の entrypoint では CLI11 の `--scenario` のバリデータが許容値を
`""` と `"reconnect"` に絞っているため、この検証は CLI 以外で `ZakuroConfig` を構築した場合の
防御になる。

## 完了条件

- `loop_index` が宣言時に初期化されていること
- 3 分岐のいずれにも該当しない場合 (現行 entrypoint からは到達しないため、コード上の保証として確認する)
  に、`Zakuro::Run` が英語のエラーメッセージを `std::cerr` に出力して 0 以外を返すこと
- if / else if チェーンが total であり、`loop_index` がどの経路でも代入されること
- `-Wuninitialized` 相当を有効にしてもコンパイル警告が出ないこと
- `--scenario` の許容値が CLI11 のバリデータで守られていること (許容値が増えたときに
  `Zakuro::Run` 側の更新を忘れると、この前提が崩れるため)

## 解決方法

`src/zakuro.cpp` の `Zakuro::Run` を次のように修正した。

- `int loop_index;` を `int loop_index = 0;` に変更した
- 想定外の scenario の検証を、`std::vector<std::shared_ptr<VirtualClient>> vcs;` の宣言と
  `boost::asio::io_context ioc{1};` のブロックより前に追加した。条件は
  `fake_audio_key_trigger && config_.scenario != "" && config_.scenario != "reconnect"` で、
  英語のメッセージ `[<name>] unsupported scenario: <値>` を `std::cerr` に出して 1 を返す
- if / else if チェーンを total にした。`config_.scenario == "reconnect"` の分岐を先に置き、
  `config_.scenario == ""` の分岐を最後の `else` にした。これにより、検証を通過した値
  (`fake_audio_key_trigger` が真なら `""` か `"reconnect"` だけ) は必ずどれかの分岐に入り、
  `loop_index` が必ず代入される

チェーンの末尾に `else { ... return 1; }` を置く案は採らなかった。`VirtualClient` を生成した後に
早期 return すると `vcs.clear()` と `vc->Clear()` を飛ばし、`VirtualClient::retry_timer_` が
`io_context` より後に破棄される未定義動作 (issue 0007 で修正済み) を再導入するためである。

検証したこと:

- `python3 run.py build macos_arm64` が成功する
- `-Wall -Wextra -Wuninitialized -Wsometimes-uninitialized -Wconditional-uninitialized` を
  有効にしたコンパイルで、未初期化に関する警告が出ないこと
  (修正前は `-Wconditional-uninitialized` で `loop_index` の警告が出る)
- `Zakuro::Run` を直接呼ぶ一時的な検証プログラムをリンクして実行し、`scenario` が
  `"unsupported-by-validator"` のときに英語のメッセージを出して 1 を返し、`""` と
  `"reconnect"` では検証に掛からないことを確認した
- `uv run pytest -q` が 91 passed / 1 skipped で通る (追加したテストを含む)
- `clang-format -style=file` が `src/` の全ファイルで差分を出さない

`test/test_config_json.py` に、`--scenario` の許容値が CLI11 のバリデータで守られていることを
確認する `test_unsupported_scenario_exits_with_cli11_code` を追加した。バリデータを外した
実装でこのテストが失敗することも確認している。あわせて既存の CLI11 検証テスト
(`test_cli_validation_error_exits_with_cli11_code`) を、`sora` 配下のオプションを検証する形に
整理した。

`CHANGES.md` の `## develop` に `[FIX]` のエントリを追加した。
