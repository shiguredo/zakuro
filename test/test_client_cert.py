"""--client-cert / --client-key で指定するクライアント証明書の E2E テスト"""

import os
import subprocess
from pathlib import Path

import pytest

from zakuro import Zakuro, get_zakuro_executable_path

from conftest import (
    CLIENT_CERT_COMMON_NAME,
    MtlsCertificateVariants,
    MtlsCertificates,
    TlsProbeServer,
    build_local_instance,
    wait_for_stderr,
)

# クライアント証明書と秘密鍵を片方だけ指定した場合のエラーメッセージ
PAIR_REQUIRED_MESSAGE = "--client-cert and --client-key must be specified together"

# オプション名とエラーメッセージに使うラベルの対応
CERTIFICATE_LABELS = {"client-cert": "client cert", "client-key": "client key"}


def test_client_cert_is_sent(mtls_certificates: MtlsCertificates, free_port: int) -> None:
    """クライアント証明書を指定すると、TLS ハンドシェイクで証明書が送信される

    クライアント証明書を必須とするローカル TLS サーバーへ zakuro 実バイナリを接続し、
    サーバー側でクライアント証明書を受信できることを確認する。
    """
    server = TlsProbeServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    client_cert=mtls_certificates.client_cert,
                    client_key=mtls_certificates.client_key,
                )
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # TLS ハンドシェイクが完了すれば、クライアント証明書が送信されている
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
            assert server.peer_common_name == CLIENT_CERT_COMMON_NAME
        # プロセス終了後に stderr を検査する
        # Sora C++ SDK のログ文言に依存した確認であることに注意する
        assert "client_cert is set" in z.stderr_output
        assert "client_key is set" in z.stderr_output
        assert "client_cert is set, but" not in z.stderr_output
        assert "client_key is set, but" not in z.stderr_output
    finally:
        server.stop()


def test_client_cert_and_key_not_specified(
    mtls_certificates: MtlsCertificates, free_port: int
) -> None:
    """クライアント証明書と秘密鍵を指定しない場合は、SDK にこれらが設定されない

    Sora C++ SDK は証明書が設定されている場合に "client_cert is set" を出力するため、
    指定しない場合にこのログが出力されないことを確認する。
    """
    server = TlsProbeServer(
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[build_local_instance(f"wss://127.0.0.1:{server.port}/signaling")],
            http_port=free_port,
            log_level="info",
        ) as z:
            # TLS ハンドシェイクが完了するまで待ち、接続処理が動いていることを確認する
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
        # プロセス終了後に stderr を検査する
        # Sora C++ SDK のログ文言に依存した確認であることに注意する
        assert "client_cert is set" not in z.stderr_output
        assert "client_key is set" not in z.stderr_output
    finally:
        server.stop()


@pytest.mark.parametrize(
    "option_name",
    ["client-cert", "client-key"],
    ids=["client-cert", "client-key"],
)
def test_client_cert_or_key_without_pair(
    option_name: str, mtls_certificates: MtlsCertificates, free_port: int
) -> None:
    """クライアント証明書と秘密鍵の片方だけを指定した場合はエラーになる

    設定ファイル経由の指定でも検証されることを確認する。
    """
    # オプション名に応じて片方だけを指定する
    client_cert: Path | None = None
    client_key: Path | None = None
    if option_name == "client-cert":
        client_cert = mtls_certificates.client_cert
    else:
        client_key = mtls_certificates.client_key

    with pytest.raises(RuntimeError) as excinfo:
        with Zakuro(
            instances=[
                build_local_instance(
                    "wss://127.0.0.1:1/signaling",
                    client_cert=client_cert,
                    client_key=client_key,
                )
            ],
            http_port=free_port,
        ):
            pass

    # 設定ファイル経由の片方だけの指定でも非 0 で終了する
    error_output = str(excinfo.value)
    assert "exited unexpectedly with code 1" in error_output, (
        f"終了コードが 1 ではない: {error_output!r}"
    )
    assert PAIR_REQUIRED_MESSAGE in error_output, (
        f"ペア必須エラーが stderr に出ていない: {error_output!r}"
    )


@pytest.mark.parametrize(
    "option_name",
    ["client-cert", "client-key"],
    ids=["client-cert", "client-key"],
)
def test_client_cert_or_key_without_pair_cli(
    option_name: str, mtls_certificates: MtlsCertificates, free_port: int
) -> None:
    """コマンドラインで片方だけを指定した場合もエラーになる"""
    # 必須オプションを揃えたうえで、片方の証明書オプションだけを追加する
    args = [
        get_zakuro_executable_path(),
        "--sora-signaling-url",
        "wss://127.0.0.1:1/signaling",
        "--sora-channel-id",
        "mtls-test",
        "--sora-role",
        "sendrecv",
        "--http-port",
        str(free_port),
        "--no-video-device",
        "--no-audio-device",
    ]
    if option_name == "client-cert":
        args += ["--client-cert", str(mtls_certificates.client_cert)]
    else:
        args += ["--client-key", str(mtls_certificates.client_key)]

    result = subprocess.run(args, capture_output=True, text=True, timeout=30)
    assert result.returncode == 1, (
        f"終了コードが 1 ではない: {result.returncode}\nstderr: {result.stderr}"
    )
    assert PAIR_REQUIRED_MESSAGE in result.stderr, (
        f"ペア必須エラーが stderr に出ていない: {result.stderr!r}"
    )


@pytest.mark.parametrize(
    "option_name",
    ["client-cert", "client-key"],
    ids=["client-cert", "client-key"],
)
@pytest.mark.parametrize(
    "failure",
    ["empty", "unreadable"],
    ids=["empty", "unreadable"],
)
def test_client_cert_or_key_load_failure(
    option_name: str,
    failure: str,
    mtls_certificates: MtlsCertificates,
    tmp_path: Path,
    free_port: int,
) -> None:
    """クライアント証明書または秘密鍵の読み込みに失敗した場合はエラーになり、接続しない

    内容が空の場合とファイルを開けない場合の両方を確認する。
    インスタンスが 1 つだけの場合はエラー終了後にプロセス自体が終了するため、
    起動時のエラーメッセージを stderr で確認できる。
    """
    # 読み込みに失敗するファイルを用意する
    broken_file = tmp_path / "broken.pem"
    if failure == "empty":
        broken_file.write_text("")
    else:
        broken_file.write_text("dummy")
        broken_file.chmod(0o000)
        if os.access(broken_file, os.R_OK):
            # root やパーミッションを無視するファイルシステムでは再現できない
            pytest.skip("読み取り権限のないファイルを再現できない環境")

    # 指定するオプションに応じて、もう一方には有効なファイルを指定する
    certificate_label = CERTIFICATE_LABELS[option_name]
    if option_name == "client-cert":
        client_cert = broken_file
        client_key = mtls_certificates.client_key
    else:
        client_cert = mtls_certificates.client_cert
        client_key = broken_file

    # エラーメッセージは失敗の種類と対象で決まる
    if failure == "empty":
        expected_message = f"{certificate_label} is empty"
    else:
        expected_message = f"failed to load {certificate_label}"

    server = TlsProbeServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        # 読み込みに失敗したインスタンスは接続せず、プロセスが終了する
        with pytest.raises(RuntimeError, match=expected_message):
            with Zakuro(
                instances=[
                    build_local_instance(
                        f"wss://127.0.0.1:{server.port}/signaling",
                        client_cert=client_cert,
                        client_key=client_key,
                    )
                ],
                http_port=free_port,
                log_level="info",
            ):
                pass
    finally:
        server.stop()
    # stop() はサーバースレッドを join するため、ここでの読み出しは競合しない
    assert server.connection_count == 0, "読み込みに失敗したインスタンスが接続している"


def test_client_cert_load_failure_keeps_other_instances(
    mtls_certificates: MtlsCertificates, tmp_path: Path, free_port: int
) -> None:
    """証明書の読み込みに失敗したインスタンス以外は接続を継続する

    読み込みに失敗したインスタンスは接続せず、もう一方のインスタンスは
    TLS ハンドシェイクまで進むことを確認する。
    """
    broken_file = tmp_path / "broken.pem"
    broken_file.write_text("")

    server = TlsProbeServer(
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling-broken",
                    client_cert=broken_file,
                    client_key=mtls_certificates.client_key,
                ),
                build_local_instance(f"wss://127.0.0.1:{server.port}/signaling-healthy"),
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # 読み込みに失敗したインスタンスだけがエラーになる
            assert wait_for_stderr(z, "client cert is empty"), (
                f"読み込み失敗のエラーメッセージが出力されなかった: {z.stderr_output!r}"
            )
            # もう一方のインスタンスは接続処理を続ける
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
    finally:
        server.stop()
    # stop() はサーバースレッドを join するため、ここでの読み出しは競合しない
    # 読み込みに失敗したインスタンスが接続していたら 2 になる
    assert server.connection_count == 1, "接続したインスタンスの数が想定と異なる"


@pytest.mark.parametrize(
    "option_name",
    ["client-cert", "client-key"],
    ids=["client-cert", "client-key"],
)
@pytest.mark.parametrize(
    "content_kind",
    ["whitespace", "not-pem"],
    ids=["whitespace", "not-pem"],
)
def test_client_cert_or_key_not_pem(
    option_name: str,
    content_kind: str,
    mtls_certificates: MtlsCertificates,
    tmp_path: Path,
    free_port: int,
) -> None:
    """PEM の開始行を含まないファイルを指定した場合はエラーになり、接続しない

    空白のみのファイルと PEM ではないファイルの両方を確認する。
    """
    # PEM として解釈できない内容のファイルを用意する
    not_pem_file = tmp_path / "not-pem.pem"
    if content_kind == "whitespace":
        not_pem_file.write_text("   \n\t\n")
    else:
        not_pem_file.write_text("this is not a pem file\n")

    # 指定するオプションに応じて、もう一方には有効なファイルを指定する
    certificate_label = CERTIFICATE_LABELS[option_name]
    if option_name == "client-cert":
        client_cert = not_pem_file
        client_key = mtls_certificates.client_key
    else:
        client_cert = mtls_certificates.client_cert
        client_key = not_pem_file
    expected_message = f"{certificate_label} is not PEM format"

    server = TlsProbeServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        # 読み込みに失敗したインスタンスは接続せず、プロセスが終了する
        with pytest.raises(RuntimeError, match=expected_message):
            with Zakuro(
                instances=[
                    build_local_instance(
                        f"wss://127.0.0.1:{server.port}/signaling",
                        client_cert=client_cert,
                        client_key=client_key,
                    )
                ],
                http_port=free_port,
                log_level="info",
            ):
                pass
    finally:
        server.stop()
    # stop() はサーバースレッドを join するため、ここでの読み出しは競合しない
    assert server.connection_count == 0, (
        "PEM として不正なファイルを指定したインスタンスが接続している"
    )


@pytest.mark.parametrize(
    "pem_format",
    ["rsa", "ec", "chain"],
    ids=["rsa", "ec", "chain"],
)
def test_client_pem_formats(
    pem_format: str,
    mtls_certificates: MtlsCertificates,
    mtls_certificate_variants: MtlsCertificateVariants,
    free_port: int,
) -> None:
    """RSA PRIVATE KEY / EC PRIVATE KEY 形式の秘密鍵と証明書チェーンでも mTLS のハンドシェイクが成立する"""
    # PEM の形式に応じて証明書と秘密鍵の組み合わせを選ぶ
    if pem_format == "rsa":
        client_cert = mtls_certificate_variants.rsa_client_cert
        client_key = mtls_certificate_variants.rsa_client_key
    elif pem_format == "ec":
        client_cert = mtls_certificate_variants.ec_client_cert
        client_key = mtls_certificate_variants.ec_client_key
    elif pem_format == "chain":
        # 証明書チェーンはベースのクライアント証明書に CA 証明書を連結したもの
        client_cert = mtls_certificate_variants.chain_client_cert
        client_key = mtls_certificates.client_key
    else:
        pytest.fail(f"未知の PEM 形式: {pem_format}")

    server = TlsProbeServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    client_cert=client_cert,
                    client_key=client_key,
                )
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # TLS ハンドシェイクが完了すれば、クライアント証明書が送信されている
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
            assert server.peer_common_name == CLIENT_CERT_COMMON_NAME
        # 新検証で弾かれていないことを確認する
        assert "is not PEM format" not in z.stderr_output
    finally:
        server.stop()


def test_client_cert_trusted_certificate(
    mtls_certificates: MtlsCertificates,
    mtls_certificate_variants: MtlsCertificateVariants,
    free_port: int,
) -> None:
    """TRUSTED CERTIFICATE の PEM は検証を通過し、SDK が読み込んでハンドシェイクが成立する"""
    server = TlsProbeServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    client_cert=mtls_certificate_variants.trusted_client_cert,
                    client_key=mtls_certificates.client_key,
                )
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # SDK が証明書を読み込んでいれば、クライアント証明書必須のサーバーと接続できる
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
            assert server.peer_common_name == CLIENT_CERT_COMMON_NAME
        assert "is not PEM format" not in z.stderr_output
    finally:
        server.stop()


def test_client_key_encrypted(
    mtls_certificates: MtlsCertificates,
    mtls_certificate_variants: MtlsCertificateVariants,
    free_port: int,
) -> None:
    """ENCRYPTED PRIVATE KEY は検証を通過して SDK に渡る

    Sora C++ SDK にはパスフレーズを渡す手段がないため、SDK 側では読み込みに失敗する。
    """
    server = TlsProbeServer(
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling",
                    client_cert=mtls_certificates.client_cert,
                    client_key=mtls_certificate_variants.encrypted_client_key,
                )
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # 検証を通過していれば接続処理が進む
            assert server.wait_for_handshake(timeout=15), "TLS ハンドシェイクが完了しなかった"
        # 検証は通過し、SDK 側で読み込みに失敗したことを確認する
        # Sora C++ SDK のログ文言に依存した確認であることに注意する
        assert "client key is not PEM format" not in z.stderr_output
        assert "client_key is set, but use_private_key failed" in z.stderr_output
    finally:
        server.stop()
