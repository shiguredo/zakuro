"""SoraClientContext の生成に失敗した場合の E2E テスト

利用できないビデオコーデック実装を指定すると `sora::SoraClientContext::Create` が nullptr を返す。
その戻り値を検査せずに使用すると、`VirtualClient::Connect` が `config_.context` を
dereference してプロセスがクラッシュする。
"""

import json
import subprocess
from pathlib import Path

import pytest

from zakuro import Zakuro, get_zakuro_executable_path

from conftest import (
    ServerCertificates,
    TlsProbeServer,
    build_local_instance,
)

# SoraClientContext の生成に失敗したときのエラーメッセージの断片
# 実際の出力は "[<name>] failed to create Sora client context" であり、
# インスタンス名は設定によって変わるため共通部分だけを検査する
CONTEXT_FAILURE_MESSAGE = "failed to create Sora client context"

# インスタンスの起動に失敗した zakuro が終了するまでの待ち時間 (秒)
# 終了しない場合はハングの退行とみなして失敗させる
CONTEXT_FAILURE_TIMEOUT_SECONDS = 10

# 到達しない signaling URL
# SoraClientContext の生成に失敗するため、接続処理には到達しない
UNREACHABLE_SIGNALING_URL = "wss://127.0.0.1:1/signaling"


def _run_with_config(config: dict[str, object], tmp_path: Path) -> subprocess.CompletedProcess[str]:
    """設定ファイルを渡して zakuro を起動し、終了コードと stderr を返す

    インスタンスの起動に失敗した場合はプロセスごと終了するため、HTTP サーバーが
    起動しているかどうかに依存しない方法で結果を取得する。
    `subprocess.run` は両方のパイプを同時に読むため、stderr が埋まって停止しない。
    """
    config_file = tmp_path / "zakuro_config.jsonc"
    config_file.write_text(json.dumps(config, ensure_ascii=False), encoding="utf-8")
    try:
        return subprocess.run(
            [get_zakuro_executable_path(), "--config", str(config_file)],
            capture_output=True,
            text=True,
            timeout=CONTEXT_FAILURE_TIMEOUT_SECONDS,
            # zakuro は作業ディレクトリに webrtc_logs_0 を作るため、ソースツリーを汚さない
            cwd=tmp_path,
        )
    except subprocess.TimeoutExpired as e:
        # ハング時も原因を追えるように、その時点までの stderr を失敗メッセージに含める
        pytest.fail(
            f"zakuro が {CONTEXT_FAILURE_TIMEOUT_SECONDS} 秒以内に終了しなかった: "
            f"config={config_file.name} stderr={e.stderr!r}"
        )


def _build_cli_args(signaling_url: str, http_port: int) -> list[str]:
    """OpenH264 ライブラリとして使えないファイルを指定する CLI 引数を構築する

    `--openh264` は CLI11 の `ExistingFile` を通る必要があるため、存在するファイルを指定する。
    OpenH264 ライブラリではないファイルを指定すると H.264 の cisco_openh264 エンジンが
    能力から消え、プリファレンスの検証に失敗して SoraClientContext の生成が失敗する。
    """
    return [
        get_zakuro_executable_path(),
        "--sora-signaling-url",
        signaling_url,
        "--sora-channel-id",
        "context-failure-test",
        "--sora-role",
        "sendrecv",
        "--no-video-device",
        "--no-audio-device",
        "--http-port",
        str(http_port),
        "--http-host",
        "127.0.0.1",
        "--h264-encoder",
        "cisco_openh264",
        "--openh264",
        "/dev/null",
    ]


def test_context_failure_does_not_crash(
    server_certificates: ServerCertificates, free_port: int, tmp_path: Path
) -> None:
    """SoraClientContext の生成に失敗した場合はクラッシュせずエラーを出力する

    設定ファイル経由の指定でも検査が働くことと、シグナルで強制終了していないことを
    確認する。生成に失敗したインスタンスは io_context を回す前に `Zakuro::Run` を
    抜けるため、接続先へは到達しない。
    `Zakuro::Run` の戻り値は `main` が捨てているため、終了コードには反映されない。
    """
    server = TlsProbeServer(
        server_cert=server_certificates.server_cert,
        server_key=server_certificates.server_key,
        # クライアント証明書は送らないため要求しない
        require_client_cert=False,
    )
    server.start()
    try:
        config = {
            "http-port": free_port,
            "http-host": "127.0.0.1",
            "instances": [
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    channel_id="context-failure-test",
                    extra={"h264-encoder": "cisco_openh264", "openh264": "/dev/null"},
                )
            ],
        }
        result = _run_with_config(config, tmp_path)
    finally:
        # stop() はサーバースレッドを join するため、この後の読み出しは競合しない
        server.stop()

    assert result.returncode >= 0, (
        f"シグナルで強制終了した: returncode={result.returncode}\nstderr: {result.stderr}"
    )
    assert CONTEXT_FAILURE_MESSAGE in result.stderr, (
        f"SoraClientContext の生成失敗が stderr に出ていない: {result.stderr!r}"
    )
    assert server.connection_count == 0, (
        "SoraClientContext の生成に失敗したインスタンスが接続先へ到達している"
    )


def test_context_failure_cli_exits_without_signal(free_port: int, tmp_path: Path) -> None:
    """CLI で OpenH264 ライブラリとして使えないファイルを指定してもクラッシュしない

    クラッシュした場合はシグナルによる強制終了 (負の終了コード) になるため、
    シグナルで終了していないことと、エラーメッセージが出力されていることを確認する。
    """
    result = subprocess.run(
        _build_cli_args(UNREACHABLE_SIGNALING_URL, free_port),
        capture_output=True,
        text=True,
        timeout=CONTEXT_FAILURE_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )

    # クラッシュした場合はシグナルによる強制終了で returncode が負になる
    assert result.returncode >= 0, (
        f"シグナルで強制終了した: returncode={result.returncode}\nstderr: {result.stderr}"
    )
    assert CONTEXT_FAILURE_MESSAGE in result.stderr, (
        f"SoraClientContext の生成失敗が stderr に出ていない: {result.stderr!r}"
    )


def test_context_creation_succeeds_on_normal_path(
    server_certificates: ServerCertificates, free_port: int
) -> None:
    """コーデック実装を明示指定しない場合は SoraClientContext の生成に成功する

    正常な経路で新たな検査が誤って失敗しないことを確認する。
    """
    server = TlsProbeServer(
        server_cert=server_certificates.server_cert,
        server_key=server_certificates.server_key,
        # クライアント証明書は送らないため要求しない
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    channel_id="context-success-test",
                )
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # SoraClientContext の生成に成功していれば、接続処理は接続先まで到達する
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
            assert CONTEXT_FAILURE_MESSAGE not in z.stderr_output, (
                f"正常な経路で SoraClientContext の生成に失敗している: {z.stderr_output!r}"
            )
    finally:
        server.stop()
