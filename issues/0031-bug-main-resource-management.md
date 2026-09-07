# main.cpp のリソース管理 (stats 非アトミック・Run 返り値捨て・RLIMIT_NOFILE・std::exit)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-main-resource-management
- Polished: 2026-09-07

## 目的

`main.cpp` および `util.cpp` のプロセスライフサイクル管理に以下の問題があり、
運用時にサイレントな障害を引き起こす。まとめて修正する。

- stats スレッドの JSON 書き出しがアトミックでない (途中で読まれると空 or 部分ファイル)
- `Zakuro::Run` の返り値を `main` が捨てており、1 インスタンス失敗が検知できない
- RLIMIT_NOFILE 1024 の下限チェックが `--vcs 1000` に対して過小 (1000 VC では 1024 では不足する)
- `Util::ParseArgs` で `std::exit()` を直接呼んでおり、終了フローが `main` に返らず終了コードを制御できない

## 現状

### stats 非アトミック書き込み

`src/main.cpp` の stats スレッドは以下を実行する。

```cpp
std::ofstream ofs(connection_id_stats_file);
ofs << jstr;
```

デフォルトモードで開くとファイルを truncate してから書き込む。
書き込み途中に外部プロセスが読むと、空ファイルまたは部分書き込みを見る。
オーケストレータが `--output-file-connection-id` を polling する運用と噛み合わない。
`ofs.fail()` の検査もない。

### Zakuro::Run 返り値捨て

`src/main.cpp` は `Zakuro zakuro(config); zakuro.Run();` として `Run` の返り値を捨てている。
`Run` は 0 / 1 / 2 を返すが受け取っていない。
1 インスタンスが capturer 生成失敗・fake audio 読み込み失敗・signaling URL 不正・DataChannel パース失敗で
異常終了しても、`main` は 0 で返る。

### RLIMIT_NOFILE 1024 が過小

`src/main.cpp` は `lim.rlim_cur < 1024` をチェックするが、CLI では `--vcs 1000` を許容している。
1 VC は WebSocket 用の FD に加えて ICE / DTLS 用に複数の UDP socket を消費するため、
1000 VC では 1024 では明らかに不足する可能性が高い。
FD 枯渇で signaling が失敗しても現状はエラーにならず、ユーザーには気付けない。

### std::exit() 直接呼び出し

`src/util.cpp` の `Util::ParseArgs` で `std::exit(app.exit(e));` / `std::exit(0);` / `std::exit(1);` を複数箇所で呼ぶ
(CLI11 の ParseError、`--version`、`--show-video-codec-capability`、必須オプション不足、`--openh264` の絶対パス違反)。
`std::exit` はその場でプロセスを終了するため、`main` で終了コードを制御できず、
`main` のローカル変数の destructor も実行されない。呼び出し側の後続処理
(HTTP サーバーの停止、stats ファイルの最終書き出しなど) を終了前に実行する余地がない。

## 設計方針

- stats 書き出しはテンポラリファイルに書いてから `std::rename` する (POSIX の rename はアトミック)。
  テンポラリファイルは出力先と同じディレクトリに作る (異なるファイルシステム間の rename は失敗するため)。
  加えて `ofs.fail()` を検査してエラーログを出す
- `Zakuro::Run` の返り値を受け取り、エラーカウンタに集約する。`main` の末尾で `return has_error ? 1 : 0;`
- `--vcs N` の指定に対する必要 FD 数を計算し、`lim.rlim_cur` と比較する。
  1 VC あたりの必要 FD 数は libwebrtc の socket 多重化に依存するため、保守的な定数 (例: 1 VC あたり 5) を導入して見積もる。
  不足時は `setrlimit` で昇格を試み、昇格しても不足する場合は明確なエラーメッセージで終了する
- `Util::ParseArgs` を `int` 戻り値 or `std::optional` に変更し、エラー時は `main` が return で終了コードを返す

## 完了条件

- stats ファイルが部分書き込み状態で外部から読まれないこと
  (`--output-file-connection-id` 指定時に、書き込まれたファイルが常に完全な JSON として
  パースできることを pytest E2E テストで検証)
- 1 Zakuro インスタンスが失敗した際、`main` が非ゼロ終了すること
- `--vcs 1000` を指定して RLIMIT_NOFILE が不足する場合、明確なエラーメッセージで拒否すること
- CLI パースエラー時に `std::exit` ではなく `main` が return で終了コードを返すこと
  (終了フローが `main` に集約され、後始末を `main` で制御できること)
