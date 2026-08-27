# main.cpp のリソース管理 (stats 非アトミック・Run 返り値捨て・RLIMIT_NOFILE・std::exit)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-main-resource-management
- Polished: {YYYY-MM-DD}

## 目的

`main.cpp` および `util.cpp` のプロセスライフサイクル管理に以下の問題があり、
運用時にサイレントな障害・リソース漏れを引き起こす。まとめて修正する。

- stats スレッドの JSON 書き出しがアトミックでない (途中で読まれると空 or 部分ファイル)
- `Zakuro::Run` の返り値を `main` が捨てており、1 インスタンス失敗が検知できない
- RLIMIT_NOFILE 1024 の下限チェックが `--vcs 1000` に対して過小 (実際は 5000+ 必要)
- `Util::ParseArgs` で `std::exit()` を直接呼んでおり、自動変数の destructor が走らずリソース漏れ

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
1 インスタンスがコマンドライン誤り・capturer 生成失敗・DataChannel パース失敗で異常終了しても、
`main` は 0 で返る。

### RLIMIT_NOFILE 1024 が過小

`src/main.cpp` は `lim.rlim_cur < 1024` をチェックするが、CLI では `--vcs 1000` を許容している。
1 VC あたり WebSocket 1 FD + ICE UDP socket 複数 + DTLS 状態で 3-5 FD 使うと、
1000 VC で 5000+ FD 必要。1024 では FD 枯渇して signaling が失敗するが、ユーザーには気付けない。

### std::exit() 直接呼び出し

`src/util.cpp` の `Util::ParseArgs` で `std::exit(app.exit(e));` / `std::exit(0);` / `std::exit(1);` を複数箇所で呼ぶ。
`std::exit` は自動変数の destructor を実行しないため、既に確保していた `ZakuroConfig` / スマートポインタ / WebRTC 初期化状態などが破棄されずリソース漏れする。

## 設計方針

- stats 書き出しはテンポラリファイルに書いてから `std::rename` (POSIX の rename はアトミック) する。加えて `ofs.fail()` を検査してエラーログを出す
- `Zakuro::Run` の返り値を受け取り、エラーカウンタに集約する。`main` の末尾で `return has_error ? 1 : 0;`
- `--vcs N` の指定に対する必要 FD 数を計算し、`lim.rlim_cur` と比較する。不足時は明示的にエラーで終了する。可能なら `setrlimit` で自動昇格を試みる
- `Util::ParseArgs` を `int` 戻り値 or `std::optional` に変更し、`main` に return して終了する

## 完了条件

- stats ファイルが部分書き込み状態で外部から読まれないこと (テストで再現し検証)
- 1 Zakuro インスタンスが失敗した際、`main` が非ゼロ終了すること
- `--vcs 1000` を指定して RLIMIT_NOFILE が不足する場合、明確なエラーメッセージで拒否すること
- CLI パースエラー時に `std::exit` ではなく通常の return フローで終了すること (デストラクタが走ること)
