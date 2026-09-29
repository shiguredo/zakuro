# `wait_for_stderr_line` がバッファ済みの行を検出できない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-wait-for-stderr-line-buffering
- Polished: {YYYY-MM-DD}

## 目的

`test/test_helpers.py` の `wait_for_stderr_line` は、子プロセスの stderr に特定の行が現れる
まで待つ同期点として E2E テストから使われている。しかし行が `BufferedReader` の内部バッファに
読み込まれた後に子プロセスが出力を止めると、その行を検出できないまま待ち時間を消費して
`False` を返す。

この関数の戻り値を同期点にしているテストは、取り逃すと「起動しなかった」と誤判定するか、
逆に検証したい事象を観測しないまま通過する。テストの信頼性に直結する欠陥である。

## 現状

`wait_for_stderr_line` は `select.select` で stderr のファイルディスクリプタが読める状態に
なるのを待ち、読めるようになってから `readline` で 1 行ずつ読む。`readline` は
`BufferedReader` を経由するため、1 行読む際に複数行が内部バッファへまとめて読み込まれる
ことがある。

内部バッファに残った行は、ファイルディスクリプタが読める状態でなくなるため
`select.select` では検出できず、子プロセスが次に書き込むまで (`readline` が呼ばれないため)
読み出されない。関数は読み出し済みの行のリストだけを返すため、バッファに残ったマーカー行は
見落とされる。

確認したこと (制御した子プロセスで実測):

- 子プロセスが 5 行をまとめて出力してから 10 秒沈黙する場合に
  `wait_for_stderr_line(process, "line3", 2)` を呼ぶと、2.06 秒で `found=False` かつ
  読み出した行は 0 件になる
- 現行の zakuro はマーカー行の後も接続リトライのログを出し続けるため、実測では検出できて
  いる (10 回連続で成功、検出まで 0.009〜0.024 秒)。ログの出方が変わると取り逃す

`wait_for_stderr_line` は `test/test_main_resource.py` と `test/test_config_json.py` から
使われている。

## 設計方針

- ファイルディスクリプタの可読性だけで判断せず、内部バッファに残った行を先に読み出す
- `select` を使う場合は、可読でなくても保留中の行があれば読む。`BufferedReader` の
  `peek` / 専用スレッドでの読み続けなど、バッファ済みの行を取りこぼさない実装にする
- 関数の戻り値の形 (`list[str]` と `bool`) と呼び出し側の使い方は変えない
- 同じ検査をしている `terminate_zakuro` の結合方法 (`"\n".join` を使うかどうか) も
  あわせて揃える

## 完了条件

- 子プロセスが複数行をまとめて出力した後に沈黙しても、マーカー行を検出できること
  (上記の再現手順で `found=True` かつ行が返ること)
- マーカー行が現れない場合は待ち時間の経過後に `False` を返すこと (既存の挙動を維持)
- `test/test_main_resource.py` と `test/test_config_json.py` の全テストが pass すること

## 解決方法

{YYYY-MM-DD} に記入
