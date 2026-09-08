# Zakuro::Run の loop_index が未初期化のまま使われる分岐がある

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
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

以下 2 点を修正する。

- 宣言時に `int loop_index = 0;` で初期化する
- if / else if チェーンの末尾に
  `else { std::cerr << "unsupported scenario: " << config_.scenario << std::endl; return 1; }`
  を追加する (メッセージは AGENTS.md の規約に従い英語で出す)

これで `Zakuro::Run` 単体で `loop_index` の初期化が保証され、想定外の scenario に対しては
`Zakuro::Run` が明示的なエラーを返して後続処理を打ち切る。

`Zakuro::Run` の戻り値を `main` が終了コードへ反映する変更は issue 0031 (main.cpp のリソース管理) で扱う。
本 issue では `Zakuro::Run` が 0 以外を返すことまでを保証する。
プロセスの非ゼロ終了の確認は issue 0031 の実装後になる点に注意する (同じ方針を issue 0011 が採っている)。

## 完了条件

- `loop_index` が宣言時に初期化されていること
- 3 分岐のいずれにも該当しない場合 (現行 entrypoint からは到達しないため、コード上の保証として確認する)
  に、`Zakuro::Run` が英語のエラーメッセージを `std::cerr` に出力して 0 以外を返すこと
- `-Wuninitialized` 相当を有効にしてもコンパイル警告が出ないこと
