# main.cpp のリソース管理 (stats 非アトミック・Run 返り値捨て・RLIMIT_NOFILE・std::exit)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-main-resource-management
- Polished: 2026-09-29
- Milestone: 2026.1.0

## 目的

`main.cpp` および `util.cpp` のプロセスライフサイクル管理に以下の問題があり、
運用時にサイレントな障害を引き起こす。まとめて修正する。

- stats スレッドの JSON 書き出しがアトミックでない (途中で読まれると空 or 部分ファイル)
- `Zakuro::Run` の返り値を `main` が捨てており、1 インスタンス失敗が検知できない
- RLIMIT_NOFILE 1024 の下限チェックが `--vcs 1000` に対して過小 (1000 VC に対して 1024 では不足すると考えられる)
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
1000 VC に対して 1024 では不足すると考えられる (1 VC あたりの実際の FD 消費数は
この issue では確認できていない。設計方針の定数は保守的な見積もりとする)。
FD 枯渇で signaling が失敗しても現状はエラーにならず、ユーザーには気付けない。

### std::exit() 直接呼び出し

`src/util.cpp` の `Util::ParseArgs` は 8 箇所で `std::exit` を呼ぶ。種類は次の 6 つ。

- CLI11 の `ParseError` (`std::exit(app.exit(e))`)
- `--version` (`std::exit(0)`)
- `--show-video-codec-capability` (`std::exit(0)`)
- 必須オプション不足 (`--sora-signaling-url` / `--sora-channel-id` / `--sora-role` の 3 箇所、`std::exit(1)`)
- `--client-cert` と `--client-key` の片方のみの指定 (`std::exit(1)`)
- `--openh264` の絶対パス違反 (`std::exit(1)`)

`std::exit` はその場でプロセスを終了するため、`main` で終了コードを制御できず、
`main` のローカル変数の destructor も実行されない。呼び出し側の後続処理
(HTTP サーバーの停止、stats ファイルの最終書き出しなど) を終了前に実行する余地がない。

終了コードは 3 種類あり、一部は既存テストが期待値として固定している。

- `app.exit(e)` は CLI11 の終了コードを返す (検証エラーは `ValidationError` の 105)。
  `test/test_config_json.py` の `test_cli_validation_error_exits_with_cli11_code` が 105 を、
  同じテストが `Run with --help for more information.` の stderr 出力を検査している
- `std::exit(0)` は「何も起動せず正常終了」を意味する。`--version` と
  `--show-video-codec-capability` の 2 経路があるが、この終了コードを検査するテストは無い
- `std::exit(1)` のうち `--client-cert` と `--client-key` の片方のみの指定は
  `test/test_client_cert.py` の `test_client_cert_or_key_without_pair` /
  `test_client_cert_or_key_without_pair_cli` が検査している。必須オプション不足と
  `--openh264` の絶対パス違反は、この終了コードを検査するテストが無い

また `app.exit(e)` は終了コードを返すだけでなく、失敗メッセージの stderr 出力と
`--help` の stdout 出力も担っている。戻り値方式に変えても、この出力は維持する必要がある。

## 設計方針

### 終了コードの方針

`main` が返す終了コードは次のとおりに統一する。`Util::ParseArgs` の変更 (後述) と
RLIMIT チェックは、いずれもこの方針に従う。

- 正常終了は 0
- エラーは 1。ただし CLI11 が返す終了コード (検証エラーは 105) は CLI11 の値をそのまま使う
- 現行の `main` は RLIMIT チェックで `return -1` (終了コード 255) を返しているが、上記に合わせて 1 にする

### stats 書き出しのアトミック化

- テンポラリファイルに書き込み、**close してから** `ofs.fail()` を検査する。
  `std::ofstream` はバッファリングするため、`ofs << jstr` の直後は内容が書き出されていない。
  close を挟まずに rename すると、出力先に空ファイルや部分書き込みが見える。
  書き込みの失敗 (close の失敗を含む) は close 後の `fail()` で判定する
- `fail()` が真の場合は rename せず、エラーログを出してテンポラリファイルを削除する。
  出力先の直前の内容を壊さない
- 成功した場合のみ `std::rename` する (POSIX の rename はアトミック)。
  テンポラリファイルは出力先と同じディレクトリに作る (異なるファイルシステム間の rename は失敗するため)

### Zakuro::Run の返り値の集約

- `Zakuro::Run` の返り値を受け取り、エラーカウンタに集約する。`main` の末尾で
  `return has_error ? 1 : 0;` する
- `Zakuro::Run` は 1 (capturer 生成失敗・fake audio 読み込み失敗・signaling URL 不正・
  client cert / key の読み込み失敗) と 2 (DataChannels パース失敗) を返すが、
  `main` では 0 以外をすべてエラーとして扱う (区別はしない)
- `Zakuro::Run` は `main` が生成した `std::thread` のラムダ内で呼ばれるため、
  エラーカウンタは `std::atomic<bool>` にする。既存の `stats_countdown` は `stats_mut` で
  保護されているが、カウンタの更新をその区間に含めると `Run` の実行中はロックを保持できないため、
  atomic の方が素直である

### RLIMIT_NOFILE の必要数チェック

- 1 VC あたりの必要 FD 数の定数は **5** とする。libwebrtc の socket 多重化の実測値ではなく、
  WebSocket 用の 1 と ICE / DTLS 用の複数を見込んだ保守的な見積もりである。
  実装時に実際の FD 消費を測れる場合は測定値で定数を置き換えてよい。その場合は issue に記録する
- 必要 FD 数は、そのプロセスが起動する全インスタンスの `vcs` の**合計**に定数を掛けて求める。
  `--vcs` はインスタンスごとのオプションであり、設定ファイル経由では `instances` の各要素に書く。
  `instance-num` は `configs` の要素数を増やすため、`--vcs` 単体では決まらない
- チェックは `configs` を組み立てた後 (全インスタンスの `vcs` が確定した後) に行う。
  現行の `src/main.cpp` の冒頭にある一律 1024 のチェックは置き換える
  (1024 は `--vcs 1000` に対して過小であり、必要数に基づく判定に一本化する)
- 不足時は `setrlimit` で soft limit を必要数まで昇格することを試み、昇格しても不足する場合は
  英語のエラーメッセージ (必要数と現在の soft / hard limit を含める) を出して終了コード 1 で終了する。
  AGENTS.md の「ログメッセージは全て英語にすること」に従う
  (現行の日本語メッセージは issues/0032 が英語化を担当しているが、本 issue で置き換える)

### Util::ParseArgs の戻り値化

- `Util::ParseArgs` の戻り値を `enum class ParseArgsResult { Continue, ExitSuccess, ErrorExit }` と
  `int exit_code` の組にする。呼び出し元は `int` だけを見て「0 なら続行」と誤解しないようにする
  (`--version` などの正常終了も 0 を返すため、`int` だけでは「続行」と区別できない)
- `Continue` の場合は `main` が後続処理に進む
- `ExitSuccess` の場合は `main` が 0 を返して終了する (`--version` /
  `--show-video-codec-capability`。出力は従来どおり `Util::ParseArgs` 内で行う)
- `ErrorExit` の場合は `main` が `exit_code` を返して終了する。`exit_code` は次のとおり
  - CLI11 の `ParseError` は `app.exit(e)` の戻り値 (検証エラーは 105)。メッセージ出力と
    `--help` の出力は従来どおり `app.exit(e)` に担わせる
  - 必須オプション不足・`--client-cert` と `--client-key` の片方のみの指定・
    `--openh264` の絶対パス違反は 1
- `src/util.h` の宣言と `src/main.cpp` の 2 箇所の呼び出し (1 回目と、設定ファイルから
  組み立てた引数の再パース) を追随させる

### 進め方

- 4 項目は独立して実装・コミットできる。実装順序は「`std::exit` の戻り値化 → `Zakuro::Run` の
  返り値の集約 → stats のアトミック化 → RLIMIT チェック」を推奨する。終了コードの方針を最初に
  固めると、他の 3 項目のテスト期待値が確定するため
- `issues/0041-refactor-zakuro-run-and-parse-args-split.md` も `Util::ParseArgs` のシグネチャを
  変更する (`ParseArgsOutput` 構造体に引数をまとめる)。本 issue を先に実装する。
  0041 は本 issue の戻り値方式を前提に、引数の受け渡しだけを構造体化する

## 完了条件

### stats ファイルのアトミック化

- stats ファイルが部分書き込み状態で外部から読まれないこと
- `--output-file-connection-id` 指定時に、書き込まれたファイルが常に完全な JSON として
  パースできることを pytest E2E テストで検証する
- 書き込みを失敗させた場合に、出力先の直前の内容が保たれること
  (`fail()` を検査して rename しない)

### Zakuro::Run の返り値の反映

- 1 Zakuro インスタンスが失敗した際、`main` が非ゼロ終了すること。
  `Zakuro::Run` の戻り値を反映する経路で検証する。設定ファイルの読み込み失敗など
  `main` 自身が検出する設定エラー (既に 1 を返している) では検証にならないため使わない
- テストは既存の `test/test_config_json.py` が使っている DataChannels のパース失敗
  (`Run` が 2 を返す経路) を使う
- `--fake-audio-capture` に 0 バイトのファイルを指定する経路 (`Run` が 1 を返す) も
  併せて検証することが望ましい。0 バイトのファイルは `WavReader::Load` が
  `size < 20` で拒否するため `Zakuro::Run` の fake audio 読み込み失敗の経路に入る。
  data チャンクが空の WAV は `WavReader::Load` が成功するため、この経路には入らない
  (空 data チャンクの扱いは issues/0008 が担当する)。また
  `--no-audio-device` を指定すると fake audio の経路に入らない点に注意する
- `test/test_config_json.py` の `test_data_channels_type_error_exits_without_crash` が
  期待している終了コード 0 を 1 に見直す。テストの docstring とファイル冒頭の
  「終了コードの期待値は経路によって異なる」の説明もあわせて更新する

### RLIMIT_NOFILE の必要数チェック

- 必要 FD 数が soft limit を超える場合、英語のエラーメッセージを出力して終了コード 1 で
  拒否すること。メッセージには必要数と現在の soft / hard limit を含める
- テストは soft limit を 1024 以上かつ必要 FD 数未満に設定して実行する。1024 未満にすると
  実装前の既存チェックでも拒否されて回帰テストとして成立しない。また hard limit も
  必要数未満に下げ、`setrlimit` による昇格で通ってしまわないようにする
- `setrlimit` で昇格できる場合は起動が継続すること

### CLI パースエラーの戻り値化

- `Util::ParseArgs` に `std::exit` が 1 つも残っていないこと
  (`--version` / `--show-video-codec-capability` の早期終了を含む)
- 終了フローが `main` に集約され、後始末を `main` で制御できること
  (パースエラーは HTTP サーバーや stats スレッドの生成前に起きるため、観測できるのは
  終了コードになる。`main` のローカル変数の destructor が実行されることを確認する)
- 既存のテストが期待する終了コードを維持すること
  - CLI11 の検証エラー (105) と、そのとき stderr に出る
    `Run with --help for more information.` (`test/test_config_json.py`)
  - `--client-cert` / `--client-key` の片方のみの指定 (1) (`test/test_client_cert.py`)
  - `--version` / `--show-video-codec-capability` の 0 と、必須オプション不足・
    `--openh264` の絶対パス違反の 1 は、終了コードを検査するテストが無い。
    `test/test_client_cert.py` の `test_client_cert_or_key_load_failure` は
    `Zakuro::Run` が 1 を返す経路を通るが、`test/zakuro.py` の `Zakuro` は
    プロセスの終了を検出すると `RuntimeError` (終了コードと stderr を含む) を送出するため、
    戻り値を反映して終了コードが変わってもテストは成立する

### 共通

- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- 既存の pytest (`test/test_zakuro.py` / `test/test_client_cert.py` /
  `test/test_config_json.py`) が pass すること
- `CHANGES.md` の `## develop` に `[FIX]` を追記すること

### 実装時に記録すること

- 1 VC あたりの必要 FD 数の定数を実測値で置き換えた場合は、その測定方法と値を issue に記録する
  (現状の 5 は libwebrtc の socket 多重化の実測値ではなく保守的な見積もりであり、
  この issue では根拠を確認できていない)

