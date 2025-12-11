"""Zakuro - WebRTC Load Testing Tool Python bindings."""

from __future__ import annotations

import json
import os
import platform
import shlex
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
from types import TracebackType
from typing import Any, Literal, Self

try:
    import httpx

    HTTPX_AVAILABLE = True
except ImportError:
    HTTPX_AVAILABLE = False


def get_binary_path() -> str:
    """zakuro バイナリのパスを取得する。

    以下の順序で検索:
    1. 環境変数 ZAKURO_BINARY_PATH
    2. PATH 上の zakuro コマンド
    3. 開発用: _build/ ディレクトリ内のバイナリ
    """
    # 環境変数で明示的に指定されている場合
    env_path = os.environ.get("ZAKURO_BINARY_PATH")
    if env_path:
        path = Path(env_path)
        if path.exists():
            return str(path)
        raise RuntimeError(f"ZAKURO_BINARY_PATH is set but file not found: {env_path}")

    # PATH 上の zakuro コマンドを検索
    which_path = shutil.which("zakuro")
    if which_path:
        return which_path

    # 開発用: _build/ ディレクトリ内のバイナリを検索
    dev_path = _find_dev_binary()
    if dev_path:
        return dev_path

    raise RuntimeError(
        "zakuro binary not found. Either:\n"
        "  1. Install via pip (pip install zakuro-py)\n"
        "  2. Set ZAKURO_BINARY_PATH environment variable\n"
        "  3. Build with: python3 run.py build <target>"
    )


def _find_dev_binary() -> str | None:
    """開発用: _build/ ディレクトリからバイナリを検索"""
    # このファイルの位置から project root を推定
    # python/zakuro/__init__.py -> python/zakuro -> python -> project_root
    project_root = Path(__file__).parent.parent.parent
    build_dir = project_root / "_build"

    if not build_dir.exists():
        return None

    available_targets = [
        d.name
        for d in build_dir.iterdir()
        if d.is_dir() and (d / "release" / "zakuro" / "zakuro").exists()
    ]

    if not available_targets:
        return None

    if len(available_targets) == 1:
        target = available_targets[0]
    else:
        system = platform.system().lower()
        machine = platform.machine().lower()

        if system == "darwin":
            preferred = ["macos_arm64"] if machine in ("arm64", "aarch64") else ["macos_x86_64"]
        elif system == "linux":
            preferred = (
                ["ubuntu-24.04_x86_64", "ubuntu-22.04_x86_64"]
                if machine == "x86_64"
                else ["ubuntu-24.04_arm64", "ubuntu-22.04_arm64"]
            )
        else:
            preferred = []

        target = next((p for p in preferred if p in available_targets), available_targets[0])

    return str(project_root / "_build" / target / "release" / "zakuro" / "zakuro")


class RpcClient:
    """JSON-RPC クライアント"""

    def __init__(self, http_client: Any, host: str, port: int) -> None:
        self._http_client = http_client
        self._host = host
        self._port = port
        self._request_id = 0

    def _call(self, method: str, params: dict[str, Any] | None = None) -> Any:
        """JSON-RPC メソッドを呼び出す"""
        self._request_id += 1
        url = f"http://{self._host}:{self._port}/rpc"
        payload: dict[str, Any] = {
            "jsonrpc": "2.0",
            "method": method,
            "id": self._request_id,
        }
        if params:
            payload["params"] = params

        response = self._http_client.post(url, json=payload)
        response.raise_for_status()
        data = response.json()

        if "error" in data:
            raise RuntimeError(f"JSON-RPC error: {data['error']}")

        if data.get("id") != self._request_id:
            raise RuntimeError(
                f"JSON-RPC id mismatch: expected {self._request_id}, got {data.get('id')}"
            )

        return data.get("result")

    def get_version(self) -> dict[str, str]:
        """バージョン情報を取得"""
        return self._call("GetVersion")

    def query(self, query: str) -> dict[str, Any]:
        """DuckDB に対して SELECT クエリを実行"""
        return self._call("Query", {"query": query})


class Zakuro:
    """Zakuro プロセスを管理するクラス

    instances にインスタンス設定のリストを渡します。

    使用例:
        # 単一インスタンス
        with Zakuro(
            instances=[sora_config.build_instance(vcs=1)],
            http_port=18080,
        ) as z:
            version = z.rpc.get_version()

        # 複数インスタンス
        with Zakuro(
            instances=[
                sora_config.build_instance(vcs=2),
                sora_config.build_instance(vcs=3, role="recvonly"),
            ],
            http_port=18080,
        ) as z:
            ...
    """

    def __init__(
        self,
        instances: list[dict[str, Any]],
        # HTTP サーバー設定
        http_port: int = 18080,
        http_host: str = "127.0.0.1",
        # ログ設定
        log_level: Literal["verbose", "info", "warning", "error", "none"] | None = None,
        # DuckDB 設定
        duckdb_dir: str | None = None,
        duckdb_interval: float | None = None,
        # 起動待機設定
        startup_timeout: int = 30,
    ) -> None:
        if not HTTPX_AVAILABLE:
            raise RuntimeError("httpx is required for Zakuro class. Install with: pip install httpx")

        self._executable_path = get_binary_path()
        self._process: subprocess.Popen[Any] | None = None

        # 設定
        self._config = {"instances": instances}
        self._http_port = http_port
        self._http_host = http_host
        self._log_level = log_level
        self._duckdb_dir = duckdb_dir
        self._duckdb_interval = duckdb_interval
        self._startup_timeout = startup_timeout

        # HTTP クライアントと RPC クライアント
        self._http_client: Any | None = None
        self._rpc: RpcClient | None = None

        # 一時ファイル
        self._temp_config_file: str | None = None

    @property
    def rpc(self) -> RpcClient:
        """RPC クライアントを取得"""
        if self._rpc is None:
            raise RuntimeError("RPC client not initialized. Use 'with Zakuro(...) as z:'")
        return self._rpc

    def __enter__(self) -> Self:
        """コンテキストマネージャーの開始"""
        try:
            args = self._build_args()
            cmd = [self._executable_path] + args
            quoted_cmd = " ".join(shlex.quote(arg) for arg in cmd)
            print(f"Starting zakuro: {quoted_cmd}")

            self._process = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )
            print(f"Started zakuro process with PID: {self._process.pid}")

            self._wait_for_startup()

            self._http_client = httpx.Client(timeout=10.0)
            self._rpc = RpcClient(self._http_client, self._http_host, self._http_port)

            return self
        except Exception as e:
            if self._process:
                print(f"Cleaning up due to exception: {e}")
            self._cleanup()
            raise

    def __exit__(
        self,
        _exc_type: type[BaseException] | None,
        _exc_val: BaseException | None,
        _exc_tb: TracebackType | None,
    ) -> Literal[False]:
        """コンテキストマネージャーの終了"""
        self._rpc = None

        if self._http_client:
            self._http_client.close()
            self._http_client = None

        self._cleanup()
        return False

    def _build_args(self) -> list[str]:
        """コマンドライン引数を構築"""
        args: list[str] = []

        # HTTP サーバー設定
        args.extend(["--http-port", str(self._http_port)])
        args.extend(["--http-host", self._http_host])

        # ログレベル
        if self._log_level:
            args.extend(["--log-level", self._log_level])

        # DuckDB 設定
        if self._duckdb_dir:
            args.extend(["--duckdb-dir", self._duckdb_dir])
        if self._duckdb_interval is not None:
            args.extend(["--duckdb-interval", str(self._duckdb_interval)])

        # config を一時ファイルに書き出し
        fd, temp_path = tempfile.mkstemp(suffix=".jsonc", prefix="zakuro_config_")
        with open(fd, "w", encoding="utf-8") as f:
            json.dump(self._config, f, ensure_ascii=False, indent=2)
        self._temp_config_file = temp_path
        args.extend(["--config", temp_path])

        return args

    def _wait_for_startup(self) -> None:
        """プロセスが起動して HTTP サーバーが利用可能になるまで待機"""
        if not self._process:
            raise RuntimeError("Process not started")

        # プロセスが完全に起動するまで少し待機
        time.sleep(2)

        print(f"Waiting for HTTP server on port {self._http_port}...")
        start_time = time.time()

        with httpx.Client() as client:
            while time.time() - start_time < self._startup_timeout:
                if self._process.poll() is not None:
                    error_msg = (
                        f"zakuro process exited unexpectedly with code {self._process.returncode}"
                    )
                    if self._process.stderr:
                        stderr_output = self._process.stderr.read()
                        if stderr_output:
                            error_msg += f"\nStderr:\n{stderr_output}"
                    raise RuntimeError(error_msg)

                try:
                    # ヘルスチェックエンドポイントで起動確認
                    url = f"http://{self._http_host}:{self._http_port}/.ok"
                    response = client.get(url, timeout=5)
                    if response.status_code == 200:
                        elapsed = time.time() - start_time
                        print(f"Zakuro started successfully ({elapsed:.1f}s)")
                        return
                except httpx.ConnectError:
                    pass
                except httpx.ConnectTimeout:
                    pass
                except httpx.HTTPStatusError:
                    pass

                time.sleep(0.5)

            self._cleanup()
            raise RuntimeError(f"zakuro failed to start within {self._startup_timeout}s")

    def _cleanup(self) -> None:
        """プロセスをクリーンアップ"""
        if self._process:
            pid = self._process.pid
            print(f"Terminating zakuro process (PID: {pid})")
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
                print(f"Zakuro process (PID: {pid}) terminated")
            except subprocess.TimeoutExpired:
                print(f"Force killing zakuro process (PID: {pid})")
                self._process.kill()
                self._process.wait()
            self._process = None
            time.sleep(0.2)

        # 一時設定ファイルを削除
        if self._temp_config_file:
            try:
                Path(self._temp_config_file).unlink(missing_ok=True)
            except OSError:
                pass
            self._temp_config_file = None


__all__ = ["Zakuro", "RpcClient", "get_binary_path"]
