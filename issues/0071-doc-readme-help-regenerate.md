# README のヘルプ抜粋を zakuro --help に合わせて再生成する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/fix-readme-help-regenerate
- Polished: {YYYY-MM-DD}

## 目的

`README.md` の埋め込みヘルプ抜粋が `zakuro --help` の実際の出力と乖離している。
特に `src/util.cpp` の `Util::ParseArgs` が定義する `--http-host` / `--http-port` が
README に無く、利用者が HTTP サーバー機能の CLI オプションに気付けない。
ヘルプ抜粋を実装出力に合わせて再生成する。

## 現状

- `Util::ParseArgs` は `--http-host` (HTTP host address to bind) と
  `--http-port` (HTTP port number、範囲 1-65535) を定義している
- `src/main.cpp` の `main` は `--http-host` と `--http-port` の両方指定を必須とし、
  片方だけの場合はエラー終了する
- `README.md` の「ヘルプ」セクションの抜粋には `--http-host` / `--http-port` が無い
- `--ui` / `--ui-remote-url` は廃止済みで実装に存在しない
- 先行の issues/0018 は UI オプションを前提としていたため、UI 廃止に伴い
  対応不要として closed になった。本 issue はその後継で、現行実装のみを対象にする
- open の issues/0052 はヘルプのデフォルト値表示を `always_capture_default()` に変える。
  0052 が先に入ると `--help` の表記形式が変わるため、本 issue 実装時は
  0052 の反映状況を確認してから再生成する

## 設計方針

- `zakuro --help` の実行結果をそのまま `README.md` のヘルプ抜粋に貼り付ける
- 廃止済みの `--ui` / `--ui-remote-url` は載せない (実装に無いため自然に含まれない)
- CLI11 のヘルプ出力にコメント行は挿入しない。実装側の「共通オプション」と
  「インスタンス毎のオプション」の境界コメントを抜粋に入れると、
  `--help` 出力との一致を壊すため
- 「ヘルプ」セクションに、抜粋が `zakuro --help` の出力を反映している旨と、
  オプション変更時は `--help` を再実行して抜粋を更新する旨の注記を、
  抜粋ブロックの外に置く
- `--http-host` / `--http-port` の併用必須制約の詳細説明は README ヘルプ抜粋の範囲外とする
  (`doc/USE.md` 側の話。本 issue では扱わない)

## 完了条件

- `README.md` のヘルプ抜粋に `--http-host` / `--http-port` が記載されていること
- `zakuro --help` の実際の出力と README の抜粋が一致していること
- `README.md` の「ヘルプ」セクションに、抜粋が `zakuro --help` の出力を反映している旨と、
  オプションの追加・変更・削除時には `zakuro --help` を再実行して抜粋を更新する旨の注記があること
  (注記は抜粋ブロックの外)
- ヘルプ抜粋に `--ui` / `--ui-remote-url` が記載されていないこと
