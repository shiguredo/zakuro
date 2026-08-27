# GitHub Actions build.yml の改善 (pytest 実行・apt-get update・可読性)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-ci-workflow-improvements
- Polished: {YYYY-MM-DD}

## 目的

`.github/workflows/build.yml` に以下の複数問題があり、CI が回帰保証として機能していない。まとめて修正する。

- pytest を一切実行しない (`test/test_zakuro.py` が回帰保証にならない)
- `apt-get install` の前に `apt-get update` が無い
- `Get package name` ステップに `name:` フィールドが無く可読性が悪い
- matrix の runner 選択が三項演算子で書かれており可読性が悪い

## 現状

### pytest 未実行

`build.yml` は `python3 run.py build ${target} --package` を実行するのみ。
`test/` 配下の pytest は CI で走らない。`CHANGES.md ## develop` の misc に `[ADD] pytest を使った E2E テストを追加する` があるが、CI ジョブで呼ばれないため実質デッドコード。

### apt-get update 無し

`sudo apt-get -y install libx11-dev libxext-dev` の前に `apt-get update` を打っていない。
GitHub Actions の Ubuntu runner はパッケージリストが古い場合があり、install が失敗することがある。

### Get package name の name: 欠落

`id: package_name` はあるが `name:` フィールドが無く、ログの可読性が下がる。
他のステップには `name:` があるのに統一されていない。

### matrix の可読性

`runs-on: ${{ matrix.name == 'ubuntu-24.04_x86_64' && 'ubuntu-24.04' || 'ubuntu-22.04' }}` のような三項演算子。
将来 arm64 を追加すると複雑化する。`matrix.include` で `{name, runner}` を対にする方が明快。

## 設計方針

- `build_linux` / `build_macos` の後段に pytest step を追加。実 Sora 接続が必要な `test_signaling_urls` フィクスチャは
  リポジトリシークレット (`TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY`) から取得。
  最低でも `test_version` は実 Sora 不要なので必ず実行
- `sudo apt-get -y update && sudo apt-get -y install libx11-dev libxext-dev` に変更
- `Get package name` ステップに `name: Get package name` を追加
- matrix を `include:` 形式に書き換えて `{name, runner}` を対にする

## 完了条件

- `pytest` が CI で実行されていること (最低でも `test_version`)
- `apt-get update` が明示的に実行されていること
- `Get package name` に `name:` が付いていること
- matrix の runner 選択が三項演算子ではなく `include` で表現されていること
