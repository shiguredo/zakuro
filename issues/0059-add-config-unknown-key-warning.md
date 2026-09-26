# 設定ファイルのトップレベルに書かれた未知のキーを警告する

- Created: 2026-09-26
- Completed: {YYYY-MM-DD}
- Branch: feature/add-config-unknown-key-warning
- Polished: {YYYY-MM-DD}

## 目的

設定ファイル (JSONC) のトップレベルに書かれた未知のキーを検出して警告する。

`log-level` や `http-port` はトップレベルに書けるため、利用者が同じ感覚で `client-cert` / `client-key` などのインスタンス用オプションをトップレベルに書いてしまうことがある。現状は黙って無視されるため、証明書が SDK に渡らず mTLS 接続の失敗まで気付けない。

## 現状

- `src/main.cpp` の設定ファイル処理は、トップレベルから `log-level` / `http-port` / `http-host` / `ui` / `ui-remote-url` / `output-file-connection-id` / `instance-hatch-rate` のみを取り込み、それ以外のキー (`instances` を除く) は無視する
- `client-cert` / `client-key` などのインスタンス用オプションは `instances` の各要素に書く必要があり、`src/util.cpp` の `Util::ParseInstanceToArgs` が扱う
- トップレベルに `client-cert` / `client-key` を書いた場合、証明書は `SoraSignalingConfig` に設定されず、`Util::ParseArgs` のペア検証にも届かない。警告も出力されないため、設定ミスに気付けない

## 設計方針

- `src/main.cpp` の設定ファイル読み込みで、トップレベルのキーを許可リスト (`log-level` / `http-port` / `http-host` / `ui` / `ui-remote-url` / `output-file-connection-id` / `instance-hatch-rate` / `instances`) と照合し、未知のキーがあれば `std::cerr` に警告を出力する
- 警告は英語にし、キー名を含める (例: `unknown top-level key in config file: client-cert`)
- 既存の挙動 (未知キーを無視して続行する) は変えない。エラーにはしない
- インスタンス配下の未知キーの検出は本 issue の対象外とする
- `test/zakuro.py` の `Zakuro` にトップレベルの追加キーを渡せる引数を追加し、pytest で警告を検証する

## 完了条件

- トップレベルに未知のキーを書いた設定ファイルで起動すると、警告が stderr に出力され、起動は継続すること
- 既知のキーのみの設定ファイルでは警告が出力されないこと
- 追加した pytest のテストが pass すること
- `python run.py build macos_arm64` など対象プラットフォームのビルドが通ること
