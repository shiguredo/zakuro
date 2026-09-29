"""複数の signaling URL で実 Sora に接続したときの abort を gdb で捕まえるデバッグ用スクリプト

調査専用であり、マージしない (feature/debug- ブランチ上でのみ使う)。
signaling URL・secret・access_token の実値は出力しない。
"""

import json
import os
import socket
import subprocess
import tempfile
import time
import uuid
from pathlib import Path

import jwt

# リポジトリルートはこのスクリプトの置き場所から求める
REPO_ROOT = Path(__file__).resolve().parent
EXECUTABLE = REPO_ROOT / "_build" / "ubuntu-24.04_x86_64" / "release" / "zakuro" / "zakuro"


def main() -> None:
    urls = [url.strip() for url in os.environ["TEST_SIGNALING_URLS"].split(",") if url.strip()]
    channel_id_prefix = os.environ["TEST_CHANNEL_ID_PREFIX"]
    secret_key = os.environ["TEST_SECRET_KEY"]

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    # conftest.py の SoraConfig と同じ形式でチャンネル ID と access_token を作る
    channel_id = f"{channel_id_prefix}debug_{uuid.uuid4().hex[:8]}"
    access_token = jwt.encode(
        {"channel_id": channel_id, "exp": int(time.time()) + 300}, secret_key, algorithm="HS256"
    )

    config = {
        "instances": [
            {
                "sora": {
                    "signaling-url": urls,
                    "channel-id": channel_id,
                    "role": "sendrecv",
                    "metadata": {"access_token": access_token},
                },
                "vcs": 1,
                "no-video-device": True,
                "no-audio-device": True,
            }
        ]
    }

    fd, config_path = tempfile.mkstemp(suffix=".jsonc", prefix="zakuro_debug_")
    with open(fd, "w", encoding="utf-8") as f:
        json.dump(config, f, ensure_ascii=False, indent=2)

    if not EXECUTABLE.exists():
        raise RuntimeError(f"zakuro executable not found: {EXECUTABLE}")

    # SIGABRT で停止した時点のスタックトレースを取得する
    gdb_command = [
        "gdb",
        "-batch",
        "-ex",
        "set pagination off",
        "-ex",
        "set print thread-events off",
        "-ex",
        "run",
        "-ex",
        "bt 20",
        "-ex",
        "frame 6",
        "-ex",
        "info registers rsi rdx",
        "-ex",
        "x/1s $rsi",
        "-ex",
        "frame 7",
        "-ex",
        "info registers rsi rdx",
        "-ex",
        "frame 8",
        "-ex",
        "info registers rsi rdx rcx r8 r9",
        "-ex",
        "thread apply all bt 6",
        "--args",
        str(EXECUTABLE),
        "--http-port",
        str(port),
        "--http-host",
        "127.0.0.1",
        "--log-level",
        "info",
        "--config",
        config_path,
    ]

    print("running zakuro under gdb")
    result = subprocess.run(gdb_command, capture_output=True, text=True, timeout=600)

    # 実値は伏字にしてから出力する
    output = result.stdout
    for url in urls:
        output = output.replace(url, "<signaling-url>")
    output = output.replace(access_token, "<access-token>")

    print("=== gdb stdout (末尾 15000 文字) ===")
    print(output[-15000:])
    print("=== gdb stderr (末尾 3000 文字) ===")
    print(result.stderr[-3000:])


if __name__ == "__main__":
    main()
