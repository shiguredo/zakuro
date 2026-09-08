# GitHub Actions build.yml の改善 (pytest 実行・apt-get update・可読性)

- Created: 2026-08-27
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-ci-workflow-improvements
- Polished: 2026-09-08

## 目的

`.github/workflows/build.yml` に以下の複数問題があり、CI が回帰保証として機能していない。まとめて修正する。

- pytest を一切実行しない (`test/test_zakuro.py` が回帰保証にならない)
- `apt-get install` の前に `apt-get update` が無い
- build ステップ (`python3 run.py build ...`) に `name:` フィールドが無く可読性が悪い
- matrix の runner 選択が三項演算子で書かれており可読性が悪い

## 現状

### pytest 未実行

`build.yml` は `python3 run.py build ${target} --package` を実行するのみ。
`test/` 配下の pytest は CI で走らない。`CHANGES.md ## develop` の misc に `[ADD] pytest を使った E2E テストを追加する` があるが、CI ジョブで呼ばれないため実質デッドコード。

なお `test/test_zakuro.py` の唯一のテスト `test_version` は `sora_config` フィクスチャ (`test/conftest.py` の `sora_config`) を要求し、
`TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` のいずれかが未設定だと `pytest.skip()` される。
このため、環境変数を渡さずに pytest を実行するとテストは 1 件も実行されず全て skip になる
(`test_version` の検証内容は GetVersion のみであり実 Sora 接続は不要だが、フィクスチャ経由で環境変数に依存している)。

### apt-get update 無し

`DEBIAN_FRONTEND=noninteractive sudo apt-get -y install libx11-dev libxext-dev` の前に `apt-get update` を打っていない。
GitHub Actions の Ubuntu runner はパッケージリストが古い場合があり、install が失敗することがある。

### name: の欠落

`name:` フィールドが無いステップは `- run: python3 run.py build ${{ matrix.name }} --package` のみ (`build_linux` / `build_macos` の両方)。
`Get package name` ステップには `name: Get package name` が既に付いている。
他のステップには `name:` があるのに build ステップだけ統一されていない。

### matrix の可読性

`runs-on: ${{ matrix.name == 'ubuntu-24.04_x86_64' && 'ubuntu-24.04' || 'ubuntu-22.04' }}` のような三項演算子。
将来 arm64 を追加すると複雑化する。`matrix.include` で `{name, runner}` を対にする方が明快。

## 設計方針

- `build_linux` / `build_macos` の後段に pytest 実行ステップを追加する
  - `test/pyproject.toml` の dev グループ (`pytest` / `httpx` / `pyjwt` / `python-dotenv`) をインストールし、`test/` ディレクトリで `pytest` を実行する
  - 現状 `requires-python` が `>=3.14` のため、実行には Python 3.14 が必要。0033 (テストハーネス改善) で `>=3.12` に緩和されるため、0033 の完了を前提とするか、0033 未完了の場合は Python 3.14 の取得を許容する
- `sora_config` フィクスチャが要求する `TEST_SIGNALING_URLS` / `TEST_CHANNEL_ID_PREFIX` / `TEST_SECRET_KEY` をリポジトリシークレットから CI の環境変数として設定し、`test_version` が実行されるようにする
  - fork からの PR などシークレットが使えない環境では `sora_config` 依存テストが skip されるのを許容する
  - 環境変数無しでも Sora 不要テストが実行できるようテスト側を分離する変更は 0043 (E2E テスト拡充) の完了条件 (「実 Sora 不要なテストは環境変数無しで実行できること」) が担当する。本 issue は CI ワークフロー側を対象とする
- `sudo apt-get -y update` を `DEBIAN_FRONTEND=noninteractive sudo apt-get -y install libx11-dev libxext-dev` の直前に追加する
  (`DEBIAN_FRONTEND=noninteractive` を維持し、既存行の置き換えでは失わないこと)
- `python3 run.py build ${target} --package` ステップに `name:` を追加する (`build_linux` / `build_macos` の両方)
- matrix を `include:` 形式に書き換えて `{name, runner}` を対にする (`build_linux` のみ)

## 完了条件

- `pytest` が CI で実行されていること (シークレット設定済みの環境では `test_version` が実行されること。シークレットが無い環境では skip を許容する)
- `apt-get update` が明示的に実行されていること
- build ステップに `name:` が付いていること
- matrix の runner 選択が三項演算子ではなく `include` で表現されていること
