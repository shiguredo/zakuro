# README のヘルプ抜粋を zakuro --help に合わせて再生成する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/update-readme-help-regenerate
- Polished: 2026-09-30

## 目的

`README.md` の「ヘルプ」セクションの抜粋を `zakuro --help` の実際の出力に一致させ、その旨と
オプション変更時の更新手順を示す注記を README に残す。

抜粋の再生成自体は 2026-09-30 のコミット `1465631` (「2026.1.0 リリースに向けて
ドキュメントを実装に合わせて修正する」) で実施済みである。現在の README の抜粋は
`--http-host` / `--http-port` を含み、`zakuro --help` の出力と一致している。しかし、
「抜粋が `zakuro --help` の出力を反映している」旨と「オプションの追加・変更・削除時には
`zakuro --help` を再実行して抜粋を更新する」旨の注記が無いため、将来のオプション変更で
抜粋が乖離したまま放置され、この問題が再発する。

## 現状

- `Util::ParseArgs` は `--http-host` (HTTP host address to bind) と
  `--http-port` (HTTP port number、範囲 1-65535) を定義している
- `src/main.cpp` の `main` は `--http-host` と `--http-port` の両方指定を必須とし、
  片方だけの場合はエラー終了する
- `README.md` の「ヘルプ」セクションの抜粋は 2026-09-30 のコミット `1465631` で
  再生成済みであり、現在は `--http-host` / `--http-port` を含む。
  その後の `--log-level` の変更 (issues/0072) は `std::optional<int>` 化のみで、
  CLI11 のヘルプ表記は変わらないため、`--log-level` の行の再生成は不要
- `README.md` の「ヘルプ」セクションに、抜粋が `zakuro --help` の出力を反映している
  旨と、オプション変更時に再実行する旨の注記が無い
- `--ui` / `--ui-remote-url` は廃止済みで実装に存在しない
- 先行の issues/0018 は UI オプションを前提としていたため、UI 廃止に伴い
  対応不要として closed になった。本 issue はその後継で、現行実装のみを対象にする
- open の issues/0052 はヘルプのデフォルト値表示を `always_capture_default()` に変える。
  0052 が先に入ると `--help` の表記形式が変わるため、本 issue 実装時は
  0052 の反映状況を確認してから再生成する
- open の issues/0054 は `--scenario` を削除し、README のヘルプ抜粋に残る
  `--scenario` の行の削除を本 issue の再生成に委ねている。0054 が先に入ると
  `--help` から `--scenario` が消えるため、本 issue 実装時は 0054 の反映状況も確認する

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
