"""テストで共有するヘルパ"""

import json
import select
import subprocess
import time
from pathlib import Path
from typing import Any

import pytest

from zakuro import get_zakuro_executable_path

# 設定ファイルを読み込むだけで終了することを期待する待ち時間 (秒)
# 終了しない場合はハングの退行とみなして失敗させる
CONFIG_ERROR_TIMEOUT_SECONDS = 10

# 通常の起動を待つ時間 (秒)
# 接続は成功しないため、起動直後の出力を確認できるだけの時間だけ待つ
STARTUP_WAIT_SECONDS = 3

# HTTP サーバーが起動したことを示すログの断片
# CLI11 の検証はコマンドラインの出力後に実行されるため、
# このログを同期点にして検証が完了したことを確認する
HTTP_SERVER_STARTED_MARKER = "HTTP server started on"

# 設定ファイルの読み込みに失敗したときのエラーメッセージの断片
CONFIG_ERROR_MARKER = "failed to load config file"

# DataChannels の解析に失敗したときのエラーメッセージの断片
# 実際の出力は "[<name>] failed to parse DataChannels" であり、
# インスタンス名は設定によって変わるため共通部分だけを検査する
DATA_CHANNELS_ERROR_MARKER = "failed to parse DataChannels"

# 値の型が想定と異なるときのエラーメッセージの断片
VALUE_TYPE_ERROR_MARKER = "has an unexpected value type"

# フラグオプションの値が真偽値でないときのエラーメッセージの断片
BOOLEAN_ERROR_MARKER = "must be a boolean"

# 設定ファイルの異常入力を検証するときに使う有効なインスタンス設定
# 実際には接続しないため、到達しない signaling URL を入れておく
# 値はキーごとに文字列・真偽値・数値・ネストしたオブジェクトと型が異なるため Any を使う
VALID_INSTANCE: dict[str, Any] = {
    "no-video-device": True,
    "no-audio-device": True,
    "sora": {
        "signaling-url": "wss://127.0.0.1:1/signaling",
        "channel-id": "config-json-test",
        "role": "sendrecv",
    },
}


def write_config(tmp_path: Path, name: str, content: str) -> Path:
    """設定ファイルを書き出す"""
    path = tmp_path / name
    path.write_text(content, encoding="utf-8")
    return path


def write_config_object(tmp_path: Path, name: str, obj: object) -> Path:
    """オブジェクトを JSONC として書き出す"""
    return write_config(tmp_path, name, json.dumps(obj, ensure_ascii=False, indent=2))


def run_zakuro(
    config_path: Path,
    timeout_seconds: int = CONFIG_ERROR_TIMEOUT_SECONDS,
    *,
    working_directory: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    """zakuro 実バイナリを設定ファイル付きで実行する

    cwd を指定した場合は zakuro が作業ディレクトリに作る webrtc_logs_0 をそのディレクトリへ
    逃がせる。上限時間を超えた場合はハングの退行とみなして失敗させる。
    """
    args = [get_zakuro_executable_path(), "--config", str(config_path)]
    try:
        return subprocess.run(
            args,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
            cwd=working_directory,
        )
    except subprocess.TimeoutExpired as e:
        # ハング時も原因を追えるように、その時点までの stderr を失敗メッセージに含める
        pytest.fail(
            f"zakuro が {timeout_seconds} 秒以内に終了しなかった: "
            f"config={config_path.name} stderr={e.stderr!r}"
        )


def wait_for_stderr_line(
    process: subprocess.Popen[bytes], marker: str, timeout: int
) -> tuple[list[str], bool]:
    """実行中のプロセスの標準エラー出力を marker を含む行が現れるまで読み続ける

    戻り値は読み出した行のリストと、marker が見つかったかどうか。
    上限時間を超えた場合は、その時点までに読み出した行と False を返す。
    marker を同期点にすることで、起動が完了したことを確認できる。
    """
    lines: list[str] = []
    # stderr=PIPE で起動しているため必ず非 None になる
    assert process.stderr is not None
    stderr = process.stderr
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([stderr], [], [], 0.1)
        if not ready:
            # プロセスが終了した場合は読み出すものが無くなる
            if process.poll() is not None:
                break
            continue
        ready, _, _ = select.select([stderr], [], [], 0)
        while ready:
            line = stderr.readline()
            if line == b"":
                return lines, False
            text = line.decode("utf-8", errors="replace").rstrip("\n")
            lines.append(text)
            if marker in text:
                return lines, True
            ready, _, _ = select.select([stderr], [], [], 0)
    return lines, False


def terminate_zakuro(process: subprocess.Popen[bytes]) -> tuple[str, str]:
    """起動し続けている zakuro を終了させ、標準出力と標準エラー出力を返す"""
    process.terminate()
    try:
        stdout_bytes, stderr_bytes = process.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        # 終了しない場合はプロセスを残さないようにしてから失敗させる
        process.kill()
        stdout_bytes, stderr_bytes = process.communicate()
        pytest.fail(f"zakuro が終了しなかった: stdout={stdout_bytes!r} stderr={stderr_bytes!r}")
    return (
        stdout_bytes.decode("utf-8", errors="replace"),
        stderr_bytes.decode("utf-8", errors="replace"),
    )
