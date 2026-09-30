# `wait_for_stderr_line` がバッファ済みの行を検出できない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-wait-for-stderr-line-buffering
- Polished: 2026-09-30

## 目的

`test/test_helpers.py` の `wait_for_stderr_line` は、子プロセスの stderr に特定の行が現れる
まで待つ同期点として E2E テストから使われている。しかし `select.select` でファイル
ディスクリプタの可読性を待ちながら `readline` で読む構造になっているため、
`readline` が内部バッファへ先読みした行を取りこぼす。取りこぼすと、起動していないのに
「起動しなかった」と誤判定してテストが失敗する。テストの信頼性に直結する欠陥である。

## 現状

`wait_for_stderr_line` は `select.select` で stderr の可読性を待ち、可読になったら
`readline` で 1 行読み、そのたびに `select.select` を timeout 0 で再実行して
さらに読めるかを確認する。可読でなくなったら外側のループへ戻る。

問題は、`select.select` が見るのはファイルディスクリプタのカーネルバッファであり、
`readline` が読むのは `BufferedReader` の内部バッファだという点である。`readline` は
1 行返すために複数行を内部バッファへ先読みする。先読みされた行はカーネルバッファから
消えているため `select.select` は not-ready を返し、外側のループへ戻ってしまう。
内部バッファに残った行は、子プロセスが次に書き込むまで読まれない。

発生条件は「`readline` で 1 行読んだ直後に内部バッファが空でない」ことである。

確認したこと (制御した子プロセスで実測):

- 子プロセスが 5 行 (`line1` 〜 `line5`) をまとめて出力してから 10 秒沈黙する場合に
  `wait_for_stderr_line(process, "line3", 2)` を呼ぶと、2.09 秒で `found=False` になり、
  読み出した行は `['line1']` の 1 件だけになる。`line2` 以降は内部バッファに残ったまま
  返らない
- 子プロセスが 20 ミリ秒間隔で 1 行ずつ出力する場合は、書き込みのたびに `select.select` が
  可読を返すため `found=True` になる。つまり「次の書き込みで 1 行ずつしか進まない」ことが
  この欠陥の正体である
- 子プロセスが 4 秒後に 1 行だけ出力する場合に `timeout=1` で呼ぶと、現行実装は 1.03 秒で
  `found=False` を返す (timeout を守る)

現行の zakuro で実測では検出できている理由:

- 実バイナリ `_build/macos_arm64/release/zakuro/zakuro` で `HTTP_SERVER_STARTED_MARKER`
  (`HTTP server started on`) を待つと 5 回とも `found=True` になった
- ただしマーカーは全 55 行のうち 3 行目に出て、マーカーから最終行までは 1.9〜4.1 ミリ秒で
  途切れる。その後 20 秒間 stderr は 1 行も出ない (`add_reconnect_scenario` は `duration` が
  0 のとき `Sleep(10000, 10000)` するだけで再接続しない)
- つまり検出できているのは「マーカー行の後もログが出続ける」からではなく、マーカーが
  起動時のバーストの 3 行目にあり、リーダーが最初のバーストで拾えているためである。
  検出の余裕は数ミリ秒しかない

`wait_for_stderr_line` は次の 4 ファイルから使われている。

- `test/test_main_resource.py` (`HTTP_SERVER_STARTED_MARKER` / `RAISED_FILE_DESCRIPTOR_MARKER`)
- `test/test_config_json.py` (`DATA_CHANNELS_SENDING_MARKER`)
- `test/test_readers.py` (`HTTP_SERVER_STARTED_MARKER` / `Y4M_OPEN_ERROR_MARKER`)
- `test/test_http_server.py` (`HTTP_SERVER_STARTED_MARKER` / `HTTP_ACCEPT_ERROR_MARKER`)

## 設計方針

`select.select` が見るファイルディスクリプタと、行を取り出すバッファを一致させる。
`io.BufferedReader` を経由せず、ファイルディスクリプタから直接読む。

- `process.stderr.fileno()` に対して `os.read` で読む。カーネルバッファから直接読むため、
  `select.select` の可読性と取り出せるデータが一致し、内部バッファへの先読みによる
  取りこぼしが起きない
- **マーカー行の改行より先を読まない**。`os.read` は今カーネルバッファにある分をまとめて
  返すため、まとめて読んでマーカー行を見つけた時点で `return` すると、同じチャンクに
  含まれていた後続の行がカーネルバッファから消えたうえで捨てられる。同じプロセスの
  stderr に対して `wait_for_stderr_line` を続けて 2 回呼ぶ箇所があり
  (`test/test_readers.py` と `test/test_http_server.py`)、2 回目が検出できなくなる。
  1 バイトずつ改行まで読む (`os.read(fd, 1)`) か、読み過ぎた分を保持して次の呼び出しで
  先に消費する形にする。テスト用途では 1 回の呼び出しで読む量は数千バイトなので
  1 バイトずつの読み出しで問題ない
- `os.read` が空のバイト列を返したら EOF とみなして打ち切り、それまでに読んだ行と
  `False` を返す。現行実装も `readline` が空を返したときに同じ扱いをしている。
  EOF を打ち切りにしないと、子プロセスの終了後も待ち時間のあいだ `select.select` が
  ready を返し続けてビジーループになる
- `select.select` が not-ready のときに `process.poll()` で終了を確認する扱いは
  現行どおり残す
- 読み出した行は `decode("utf-8", errors="replace")` して行末を落とす。現行と同じ
  形の `list[str]` を返す
- マーカーが見つからないまま待ち時間が経過したら `False` を返す。既存の挙動を維持する
- 関数の戻り値の形 (`tuple[list[str], bool]`) と呼び出し側の使い方は変えない
- `import os` を `test/test_helpers.py` と `test/test_readers.py` に追加する
- `os.read` に置き換えたあとは、同じストリームに対して `readline` を混ぜない。
  `BufferedReader` が先読みした分は `os.read` から見えなくなるためである

`BufferedReader` の `peek` は使わない。実測で次の 2 点が確認できている。

- 内部バッファにデータがあれば `peek` は即座に返るが、バッファが空だとファイル
  ディスクリプタの生読み出しにいってブロックする。可読性の判定に使うと待ち時間を
  超えてブロックし、「待ち時間の経過後に `False` を返す」を壊す (実測: `timeout=1` を
  指定しても 4.13 秒かかった)
- `peek` が内部バッファへ先読みした行は `select.select` の可読性には現れないため、
  取りこぼしの原因そのものは解消しない

`os.read` を使う方式では `peek` 方式で問題になった 2 点がどちらも解消することを実測で
確認した (5 行バーストからの `line3` 検出が 0.02 秒、`timeout=1` で 1.03 秒で `False`)。

### あわせて直すもの

- `test/test_readers.py` の `_wait_for_stdout_line` は `wait_for_stderr_line` とほぼ同一の
  実装で同じ欠陥を持つ。標準出力を読む点と、`select.select` を timeout 0 で再確認する
  内側のループが無い点が違う。同じ方式に直す。共通化は `test/conftest.py` の役割分割と
  共有ヘルパーの整理を扱う別の issue に譲り、本 issue では両方を同じ方式に揃えるところまで
  とする
- `wait_for_stderr_line` の結果と `terminate_zakuro` の結果を連結している呼び出し側の
  書き方が揃っていない。`test/test_main_resource.py` の
  `"".join(stderr_lines) + stderr_tail` を `"\n".join` に揃える。`terminate_zakuro` 自体は
  `process.communicate()` の結果を返すだけで結合しておらず、変更しない

## 完了条件

- 子プロセスが複数行をまとめて出力した後に沈黙しても、マーカー行を検出できること。
  「5 行をまとめて出力してから沈黙する子プロセス」で `line3` を待つと `found=True` になり、
  読み出した行に `line1` 〜 `line3` が含まれること
- 同じプロセスの stderr に対して続けて 2 回呼んでも、2 回目が検出できること。
  「2 行をまとめて出力し、そのあと別の行を出力する子プロセス」で 1 行目のマーカーを
  待ったあと 2 行目のマーカーを待つと、どちらも `found=True` になること
- マーカー行が現れない場合は待ち時間の経過後に `False` を返し、待ち時間を超えて
  ブロックしないこと (既存の挙動を維持)。子プロセスが終了した場合は待ち時間を
  待たずに `False` を返すこと
- 上記 3 つを検証する回帰テストを `test/` に追加すること。`wait_for_stderr_line` に
  対して Python の子プロセス (`subprocess.Popen([sys.executable, "-c", ...])`) を使い、
  モックやスタブは使わない
- `test/test_readers.py` の `_wait_for_stdout_line` も同じ方式になっていること
- `wait_for_stderr_line` を使う 4 ファイル
  (`test/test_main_resource.py` / `test/test_config_json.py` / `test/test_readers.py` /
  `test/test_http_server.py`) を含む `test/` 配下の全テストが pass すること
- `test/test_main_resource.py` の `test_valid_config_is_converted_to_arguments` は設定
  ファイルで `"log-level": "error"` を指定しながら `HTTP_SERVER_STARTED_MARKER`
  (`LS_INFO`) を同期点にしている。`--log-level` が実際に効くようにする別の issue が
  先に実装された場合は、このテストの同期点もそちらの完了条件に従って変更される。本 issue
  の完了条件はその変更後の状態で評価する
- `python3 run.py build macos_arm64` などのビルドが通ること
- `CHANGES.md` の `## develop` の `### misc` に `[UPDATE]` のエントリを追加すること
  (テスト専用の修正で利用者向けの挙動を変えないため `[FIX]` ではなく `### misc`)

## 解決方法

{YYYY-MM-DD} に記入
