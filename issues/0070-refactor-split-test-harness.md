# test/conftest.py の役割を分割し、共有ヘルパーを専用モジュールへ移す

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
- Branch: feature/refactor-split-test-harness
- Polished: 2026-09-30

## 目的

`test/conftest.py` が 671 行になり、次の 4 つの役割を 1 ファイルに抱えている。

- テスト用証明書の生成 (openssl の呼び出しと dataclass)
- TLS ハンドシェイクだけを行うローカルサーバー
- ローカル接続用のインスタンス設定の構築
- 実 Sora 接続用の設定

pytest の `conftest.py` はフィクスチャを定義するためのファイルである。しかし現在は
フィクスチャだけでなく共有ヘルパーも抱えており、テストコードが `from conftest import ...`
でヘルパーを取り出している。さらに `test/conftest.py` と多数のテストファイルが
`from zakuro import ...` / `from test_helpers import ...` のトップレベル import をしており、
この構成は pytest の既定である prepend モードがテストディレクトリ (`test/`) を sys.path
へ挿入することに依存している。`--import-mode=importlib` ではテストディレクトリが
sys.path へ挿入されないため `ModuleNotFoundError` になる (実際に失敗するのは
`test/conftest.py` の `from zakuro import Zakuro` であり、`from conftest import ...`
自体は importlib モードでも動作する)。役割ごとにモジュールを分け、import を import
モードに依存しない形に直す。

## 現状

`test/conftest.py` の内容は次のとおり。

| 定義 | 内容 | 種類 |
| --- | --- | --- |
| `SoraConfig` / `get_zakuro_version` / `get_deps_versions` | 実 Sora 接続用の設定 | クラスと関数 |
| `free_port` / `sora_config` | フィクスチャ | フィクスチャ |
| `MtlsCertificates` / `MtlsCertificateVariants` / `ServerCertificates` / `CLIENT_CERT_COMMON_NAME` | テスト用証明書の dataclass と定数 | クラスと定数 |
| `_run_openssl` / `_generate_self_signed_certificate` / `_sign_certificate_request` / `_generate_client_certificate` | openssl による証明書生成 | 関数 |
| `openssl_path` / `mtls_certificates` / `mtls_certificate_variants` / `server_certificates` | フィクスチャ | フィクスチャ |
| `TlsProbeServer` / `_get_common_name` | TLS ハンドシェイクだけを行うローカルサーバー | クラスと関数 |
| `build_local_instance` | ローカル接続用のインスタンス設定の構築 | 関数 |
| `wait_for_stderr` | zakuro の stderr の待機 | 関数 |

テストからの import は次の 3 ファイルにある。

- `test/test_client_cert.py`: `CLIENT_CERT_COMMON_NAME` / `MtlsCertificates` /
  `MtlsCertificateVariants` / `TlsProbeServer` / `build_local_instance` / `wait_for_stderr`
- `test/test_sora_client_context.py`: `ServerCertificates` / `TlsProbeServer` /
  `build_local_instance`
- `test/test_zakuro.py`: `SoraConfig` / `get_deps_versions` / `get_zakuro_version`

`from conftest import ...` だけでなく、`test/` 直下モジュールのトップレベル import 全てが
同じ依存を持つ。`from zakuro import ...` は 11 ファイル (conftest.py を含む)、
`from test_helpers import ...` は 6 ファイルにある。実際に `test/` で
`uv run pytest --import-mode=importlib --collect-only` を実行すると、`test/conftest.py`
の `from zakuro import Zakuro` が `ModuleNotFoundError: No module named 'zakuro'` になる。

`test/zakuro.py` (プロセス管理) と `test/test_helpers.py` (設定ファイル付きの実行と待機) は
共有ヘルパーのモジュールとして既に `test/` 直下に置かれている。共有ヘルパーを専用モジュールへ
置く作法は、この 2 ファイルで既に使われている。

## 設計方針

役割ごとに `test/` 直下のモジュールへ移し、`test/conftest.py` にはフィクスチャだけを残す。

- `conftest.py` に残すもの: `free_port` / `sora_config` / `openssl_path` /
  `mtls_certificates` / `mtls_certificate_variants` / `server_certificates`
- 移すもの: 証明書の生成と dataclass、`TlsProbeServer`、`build_local_instance`、
  `wait_for_stderr`、実 Sora 接続用の設定

モジュールの分け方とファイル名は実装時に決める。候補は次の 2 分割で、既存の
`test/test_helpers.py` に寄せるか、証明書と TLS サーバーを新しいモジュールにする。

- 証明書と TLS サーバー (openssl の呼び出し、dataclass、`TlsProbeServer`)
- 実 Sora 接続用の設定 (`SoraConfig` / `get_zakuro_version` / `get_deps_versions`) と
  `build_local_instance` / `wait_for_stderr`

`from conftest import` を全廃し、テストからは移設先のモジュールを import する。

ただし、移設先のモジュールを `from <モジュール名> import ...` で import するだけでは
importlib モードで動かない。`test/` 直下のモジュールは prepend モードの sys.path 挿入で
しか import できないため、import モードに依存しない仕組みを次のどちらか 1 つ選ぶ
(両案とも prepend / importlib の両モードで動作することを確認済み)。

- 案 A: `test/pyproject.toml` の `[tool.pytest.ini_options]` に `pythonpath = ["."]`
  を追加し、`test/` を sys.path へ載せる。import はトップレベルのままでよい。
  変更が小さい
- 案 B: `test/` をパッケージ化 (`__init__.py` を追加) し、`from zakuro import` を
  `from .zakuro import` のような相対 import に書き換える。pytest 設定の追加は不要

## 完了条件

- `test/conftest.py` にはフィクスチャと、フィクスチャを機能させるために必要な最小限の
  記述 (移設先モジュールからの import や `.env` の読み込みなど) だけが残っていること
- テストから `from conftest import` している箇所が無いこと
- `pytest --import-mode=importlib` でテストが起動し、`test/` 配下のテストが全て pass すること
  (実 Sora 接続のテストは環境変数が無い場合 skip される)
- `prek run --all-files` が pass すること (ruff-format / ruff-check / ty / pytest)
