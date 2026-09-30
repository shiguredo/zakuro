# 設定ファイルのトップレベルに `null` を指定すると文字列 "null" として扱われ、設定ミスに気付けない

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-config-null-value
- Polished: 2026-09-30

## 目的

設定ファイル (JSONC) のトップレベルに `null` を指定した場合の挙動が、
インスタンス配下のオプションと非対称で、設定ミスが無言で通るか、原因の分からない
CLI11 のエラーになる。`null` は「未設定」を意味する値であり、設定値としては拒否する。

## 現状

`src/main.cpp` のトップレベルの共通オプションは、値がオブジェクトまたは配列の場合だけを
設定エラーにしている。`null` は素通りし、`Util::PrimitiveValueToString` が
`boost::json::serialize` の結果である文字列 `"null"` を返すため、CLI 引数としては
「値が `null` という文字列」として渡る。

一方、インスタンス配下のオプションは `Util::ParseInstanceToArgs` がキーごとに
`is_string` / `is_number` / `is_bool` で型を検査するため、`null` は設定エラーになる。

`null` を指定したときの実際の挙動は次のとおり（`zakuro --config <file>` で確認）。

| 指定箇所 | 実際の挙動 |
| --- | --- |
| トップレベル `http-host` | 文字列 `"null"` として受理され、`--http-host null` として CLI11 へ渡る。ホスト名 `null` が名前解決できる環境では `HTTP server started on null:<port>` を出力して起動が継続する。解決できない環境では `failed to start HTTP server on null:<port>` を出力して終了コード 1 になる |
| トップレベル `output-file-connection-id` | 文字列 `"null"` として受理され、ファイル名 `null` に接続 ID が書き出される |
| トップレベル `log-level` / `http-port` / `instance-hatch-rate` | 文字列 `"null"` が CLI11 の検証で弾かれ、終了コード 105 になる |
| インスタンス配下 (`name` / `vcs` / `initial-mute-video` など) | `has an unexpected value type` を出力して終了コード 1 |
| `sora` 配下 (`channel-id` など) | `sora-channel-id has an unexpected value type` を出力して終了コード 1 |

`http-host` に `null` を指定した場合、設定値 `null` はホスト名 `"null"` として解釈される。
ホスト名 `null` が名前解決できる環境では、`HTTP server started on null:<port>` を出力して
起動が継続するため設定ミスに気付けない。解決できない環境では
`failed to start HTTP server on null:<port>` を出力して終了コード 1 になるが、これは
設定値に `null` を書いたことを直接伝えるエラーではなく、原因が読み取りにくい。
`log-level` などの終了コード 105 になる場合も、エラーメッセージからは `null` が原因である
ことが読み取りにくい。

再現手順:

1. `http-port` と `http-host: null` を書いた設定ファイルを用意する

   ```jsonc
   {
     "http-port": 18080,
     "http-host": null,
     "instances": [
       {
         "no-video-device": true,
         "no-audio-device": true,
         "sora": {
           "signaling-url": "wss://127.0.0.1:1/signaling",
           "channel-id": "config-null-test",
           "role": "sendrecv"
         }
       }
     ]
   }
   ```

2. `zakuro --config <file>` を実行する

期待: `http-host` の値が不正であることを示すエラーを出力して終了する
実際: `--http-host null` として受理され、ホスト名 `null` の名前解決に失敗する環境では
`failed to start HTTP server on null:18080` を出力して終了コード 1 になる。ホスト名
`null` が名前解決できる環境では `HTTP server started on null:18080` を出力して起動が継続する

## 設計方針

- `src/main.cpp` のトップレベルの共通オプションのループで、値が `null` の場合も設定エラーにする。
  エラーメッセージは既存の英語メッセージに合わせ、実際に受け付ける型 (`a string, a number,
  or a boolean`) を挙げる
- `src/util.cpp` の `Util::ParseInstanceToArgs` も同じ扱いに揃える。`null` を型エラーとして弾く
  方針は現状の `is_string` / `is_number` / `is_bool` で既に実現できているため、変更は不要。
  JSON をそのまま渡すオプション (`sora.metadata` など) は `null` も有効な JSON 値として
  そのまま渡す現状を維持する
- テストは `test/test_config_json.py` に追加する。トップレベルの `null` が設定エラーになること、
  インスタンス配下・`sora` 配下の `null` が設定エラーになることを検証する

## 完了条件

- `http-host` / `output-file-connection-id` に `null` を指定した場合、文字列 `"null"` として
  受理されず、英語のエラーメッセージを出力して終了コード 1 で終了すること
- キーを省略した場合は従来どおり既定値が使われること
- `test/test_config_json.py` の追加テストが pass すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
- 既存のテスト (`test/test_zakuro.py` / `test/test_client_cert.py` /
  `test/test_config_json.py`) が pass すること
