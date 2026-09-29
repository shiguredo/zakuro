"""--client-cert / --client-key で指定するクライアント証明書の E2E テスト"""

import os
import shutil
import socket
import ssl
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import pytest

from zakuro import Zakuro, get_zakuro_executable_path

# クライアント証明書の commonName と一致することを確認する
CLIENT_CERT_COMMON_NAME = "zakuro-test-client"

# クライアント証明書と秘密鍵を片方だけ指定した場合のエラーメッセージ
PAIR_REQUIRED_MESSAGE = "--client-cert and --client-key must be specified together"

# オプション名とエラーメッセージに使うラベルの対応
CERTIFICATE_LABELS = {"client-cert": "client cert", "client-key": "client key"}


@dataclass(frozen=True)
class MtlsCertificates:
    """テスト用に生成した mTLS の証明書一式"""

    ca_cert: Path
    ca_key: Path
    server_cert: Path
    server_key: Path
    client_cert: Path
    client_key: Path


@dataclass(frozen=True)
class MtlsCertificateVariants:
    """テスト用に生成した追加形式のクライアント証明書と秘密鍵"""

    trusted_client_cert: Path
    chain_client_cert: Path
    rsa_client_cert: Path
    rsa_client_key: Path
    ec_client_cert: Path
    ec_client_key: Path
    encrypted_client_key: Path


def _run_openssl(args: list[str]) -> None:
    """openssl コマンドを実行する"""
    result = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        # 失敗時は openssl の出力を含めて原因を分かりやすくする
        raise RuntimeError(
            f"openssl command failed: args={' '.join(args)} stderr={result.stderr.strip()}"
        )


@pytest.fixture
def openssl_path() -> str:
    """openssl コマンドのパスを返す"""
    openssl = shutil.which("openssl")
    if openssl is None:
        # テスト用証明書の生成に openssl コマンドが必要なため、無い環境ではスキップする
        pytest.skip("テスト用証明書の生成には openssl コマンドが必要")
    else:
        return openssl


@pytest.fixture
def mtls_certificates(tmp_path: Path, openssl_path: str) -> MtlsCertificates:
    """テスト用の自己署名 CA、サーバー証明書、クライアント証明書を生成する"""
    openssl = openssl_path

    ca_key = tmp_path / "ca-key.pem"
    ca_cert = tmp_path / "ca-cert.pem"
    server_key = tmp_path / "server-key.pem"
    server_cert = tmp_path / "server-cert.pem"
    client_key = tmp_path / "client-key.pem"
    client_csr = tmp_path / "client.csr"
    client_cert = tmp_path / "client-cert.pem"

    # テスト用の CA を作成する
    _run_openssl(
        [
            openssl,
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-sha256",
            "-days",
            "1",
            "-nodes",
            "-keyout",
            str(ca_key),
            "-out",
            str(ca_cert),
            "-subj",
            "/CN=zakuro-test-ca",
            # openssl.cnf の内容に依存せず CA であることを明示する
            "-addext",
            "basicConstraints=critical,CA:TRUE",
        ]
    )
    # サーバー証明書を作成する
    # zakuro は insecure で接続するため、サーバー証明書の検証は行われない
    _run_openssl(
        [
            openssl,
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-sha256",
            "-days",
            "1",
            "-nodes",
            "-keyout",
            str(server_key),
            "-out",
            str(server_cert),
            "-subj",
            "/CN=127.0.0.1",
        ]
    )
    # クライアント証明書を作成する
    _run_openssl(
        [
            openssl,
            "req",
            "-newkey",
            "rsa:2048",
            "-sha256",
            "-nodes",
            "-keyout",
            str(client_key),
            "-out",
            str(client_csr),
            "-subj",
            f"/CN={CLIENT_CERT_COMMON_NAME}",
        ]
    )
    _run_openssl(
        [
            openssl,
            "x509",
            "-req",
            "-in",
            str(client_csr),
            "-CA",
            str(ca_cert),
            "-CAkey",
            str(ca_key),
            "-CAcreateserial",
            "-out",
            str(client_cert),
            "-days",
            "1",
            "-sha256",
        ]
    )

    return MtlsCertificates(
        ca_cert=ca_cert,
        ca_key=ca_key,
        server_cert=server_cert,
        server_key=server_key,
        client_cert=client_cert,
        client_key=client_key,
    )


def _generate_client_certificate(
    openssl: str,
    *,
    key_path: Path,
    csr_path: Path,
    cert_path: Path,
    ca_cert: Path,
    ca_key: Path,
) -> None:
    """秘密鍵からクライアント証明書を生成する"""
    _run_openssl(
        [
            openssl,
            "req",
            "-new",
            "-key",
            str(key_path),
            "-out",
            str(csr_path),
            "-subj",
            f"/CN={CLIENT_CERT_COMMON_NAME}",
        ]
    )
    _run_openssl(
        [
            openssl,
            "x509",
            "-req",
            "-in",
            str(csr_path),
            "-CA",
            str(ca_cert),
            "-CAkey",
            str(ca_key),
            "-CAcreateserial",
            "-out",
            str(cert_path),
            "-days",
            "1",
            "-sha256",
        ]
    )


@pytest.fixture
def mtls_certificate_variants(
    mtls_certificates: MtlsCertificates, openssl_path: str, tmp_path: Path
) -> MtlsCertificateVariants:
    """追加形式のクライアント証明書と秘密鍵を生成する"""
    openssl = openssl_path

    # TRUSTED CERTIFICATE 形式のクライアント証明書を作成する
    trusted_client_cert = tmp_path / "trusted-client-cert.pem"
    _run_openssl(
        [
            openssl,
            "x509",
            "-in",
            str(mtls_certificates.client_cert),
            "-trustout",
            "-out",
            str(trusted_client_cert),
        ]
    )

    # クライアント証明書と CA 証明書をつないだ証明書チェーンを作成する
    chain_client_cert = tmp_path / "chain-client-cert.pem"
    chain_client_cert.write_text(
        mtls_certificates.client_cert.read_text() + mtls_certificates.ca_cert.read_text()
    )

    # RSA PRIVATE KEY 形式の秘密鍵と証明書を作成する
    rsa_client_key = tmp_path / "rsa-client-key.pem"
    _run_openssl([openssl, "genrsa", "-out", str(rsa_client_key), "2048"])
    if "RSA PRIVATE KEY" not in rsa_client_key.read_text():
        # OpenSSL 3 の genrsa は PKCS#8 で出力するため PKCS#1 へ変換する
        _run_openssl(
            [
                openssl,
                "rsa",
                "-in",
                str(rsa_client_key),
                "-out",
                str(rsa_client_key),
                "-traditional",
            ]
        )
        if "RSA PRIVATE KEY" not in rsa_client_key.read_text():
            raise RuntimeError(f"RSA PRIVATE KEY 形式に変換できなかった: {rsa_client_key}")
    rsa_client_cert = tmp_path / "rsa-client-cert.pem"
    _generate_client_certificate(
        openssl,
        key_path=rsa_client_key,
        csr_path=tmp_path / "rsa-client.csr",
        cert_path=rsa_client_cert,
        ca_cert=mtls_certificates.ca_cert,
        ca_key=mtls_certificates.ca_key,
    )

    # EC PRIVATE KEY 形式の秘密鍵と証明書を作成する
    ec_client_key = tmp_path / "ec-client-key.pem"
    _run_openssl(
        [
            openssl,
            "ecparam",
            "-name",
            "prime256v1",
            "-genkey",
            "-noout",
            "-out",
            str(ec_client_key),
        ]
    )
    ec_client_cert = tmp_path / "ec-client-cert.pem"
    _generate_client_certificate(
        openssl,
        key_path=ec_client_key,
        csr_path=tmp_path / "ec-client.csr",
        cert_path=ec_client_cert,
        ca_cert=mtls_certificates.ca_cert,
        ca_key=mtls_certificates.ca_key,
    )

    # 暗号化された秘密鍵を作成する (パスフレーズはテスト用のダミー)
    encrypted_client_key = tmp_path / "encrypted-client-key.pem"
    _run_openssl(
        [
            openssl,
            "pkcs8",
            "-topk8",
            "-in",
            str(mtls_certificates.client_key),
            "-out",
            str(encrypted_client_key),
            "-passout",
            "pass:dummy-passphrase",
        ]
    )

    return MtlsCertificateVariants(
        trusted_client_cert=trusted_client_cert,
        chain_client_cert=chain_client_cert,
        rsa_client_cert=rsa_client_cert,
        rsa_client_key=rsa_client_key,
        ec_client_cert=ec_client_cert,
        ec_client_key=ec_client_key,
        encrypted_client_key=encrypted_client_key,
    )


class ClientCertTlsServer:
    """TLS ハンドシェイクだけを行うローカルサーバー

    zakuro が mTLS のクライアント証明書を送信したかどうかをサーバー側で観測する。
    WebSocket のハンドシェイクには応答しない。
    """

    def __init__(
        self,
        *,
        ca_cert: Path,
        server_cert: Path,
        server_key: Path,
        require_client_cert: bool = True,
    ) -> None:
        self._context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self._context.load_cert_chain(certfile=server_cert, keyfile=server_key)
        if require_client_cert:
            # クライアント証明書を必須にして、送信された証明書を CA で検証する
            self._context.verify_mode = ssl.CERT_REQUIRED
            self._context.load_verify_locations(cafile=ca_cert)
        self._socket: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._port = 0
        self._handshake_done = threading.Event()
        self._connection_count = 0
        self._peer_common_name: str | None = None

    @property
    def port(self) -> int:
        """待ち受けポート番号を返す"""
        return self._port

    @property
    def connection_count(self) -> int:
        """受信した TCP 接続の数を返す"""
        return self._connection_count

    @property
    def peer_common_name(self) -> str | None:
        """クライアント証明書の commonName を返す"""
        return self._peer_common_name

    def start(self) -> None:
        """サーバーを起動する"""
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind(("127.0.0.1", 0))
        self._socket.listen(8)
        # stop() で close() しても accept() が起きない環境があるためタイムアウトを設定する
        self._socket.settimeout(0.5)
        self._port = self._socket.getsockname()[1]
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def wait_for_handshake(self, timeout: float) -> bool:
        """TLS ハンドシェイクの完了を待つ"""
        return self._handshake_done.wait(timeout)

    def stop(self) -> None:
        """サーバーを停止する"""
        if self._socket is not None:
            self._socket.close()
            self._socket = None
        if self._thread is not None:
            self._thread.join(timeout=5)
            self._thread = None

    def _serve(self) -> None:
        while True:
            server_socket = self._socket
            if server_socket is None:
                return
            try:
                connection, _ = server_socket.accept()
            except TimeoutError:
                # 接続待ちのタイムアウト。stop() されるまで接続を待ち続ける
                continue
            except OSError:
                # stop() でソケットを閉じた場合は終了する
                return
            # クライアントが TLS ハンドシェイクを始めない場合に止まらないようにする
            connection.settimeout(5)
            self._connection_count += 1
            try:
                with self._context.wrap_socket(connection, server_side=True) as tls:
                    self._peer_common_name = _get_common_name(tls.getpeercert())
                    self._handshake_done.set()
                    # WebSocket のハンドシェイクには応答せず、接続を閉じる
            except ssl.SSLError:
                # クライアント証明書が無い、または検証に失敗した場合はここに来る
                pass
            except OSError:
                # 接続が切断された場合など、ハンドシェイク以外の失敗は無視する
                pass
            finally:
                connection.close()


# ssl.SSLSocket.getpeercert() の戻り値はネストしたタプルを含む dict であり、
# 型を厳密に表現できないため Any を使う
def _get_common_name(certificate: dict[str, Any] | None) -> str | None:
    """証明書の subject から commonName を取り出す

    クライアント証明書が送信されなかった場合は certificate が None になる。
    """
    if certificate is None:
        return None
    subject = certificate.get("subject")
    if not isinstance(subject, tuple):
        return None
    for rdn in subject:
        for key, value in rdn:
            if key == "commonName":
                return str(value)
    return None


def _build_local_instance(
    signaling_url: str,
    *,
    client_cert: Path | None = None,
    client_key: Path | None = None,
) -> dict[str, object]:
    """ローカル TLS サーバーへ接続する zakuro インスタンス設定を構築する"""
    instance: dict[str, object] = {
        "sora": {
            "signaling-url": signaling_url,
            "channel-id": "mtls-test",
            "role": "sendrecv",
        },
        "vcs": 1,
        "no-video-device": True,
        "no-audio-device": True,
        # テスト用サーバー証明書は自己署名のため、サーバー証明書の検証を無効化する
        "insecure": True,
    }
    if client_cert is not None:
        instance["client-cert"] = str(client_cert)
    if client_key is not None:
        instance["client-key"] = str(client_key)
    return instance


def _wait_for_stderr(z: Zakuro, text: str, timeout: float = 15.0) -> bool:
    """zakuro の stderr に指定した文字列が出力されるまで待つ"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text in z.stderr_output:
            return True
        time.sleep(0.1)
    return text in z.stderr_output


def test_client_cert_is_sent(mtls_certificates: MtlsCertificates, free_port: int) -> None:
    """クライアント証明書を指定すると、TLS ハンドシェイクで証明書が送信される

    クライアント証明書を必須とするローカル TLS サーバーへ zakuro 実バイナリを接続し、
    サーバー側でクライアント証明書を受信できることを確認する。
    """
    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                _build_local_instance(
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
    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[_build_local_instance(f"wss://127.0.0.1:{server.port}/signaling")],
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
                _build_local_instance(
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

    server = ClientCertTlsServer(
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
                    _build_local_instance(
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

    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                _build_local_instance(
                    f"wss://127.0.0.1:{server.port}/signaling-broken",
                    client_cert=broken_file,
                    client_key=mtls_certificates.client_key,
                ),
                _build_local_instance(f"wss://127.0.0.1:{server.port}/signaling-healthy"),
            ],
            http_port=free_port,
            log_level="info",
        ) as z:
            # 読み込みに失敗したインスタンスだけがエラーになる
            assert _wait_for_stderr(z, "client cert is empty"), (
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

    server = ClientCertTlsServer(
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
                    _build_local_instance(
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

    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                _build_local_instance(
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
    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                _build_local_instance(
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
    server = ClientCertTlsServer(
        ca_cert=mtls_certificates.ca_cert,
        server_cert=mtls_certificates.server_cert,
        server_key=mtls_certificates.server_key,
        require_client_cert=False,
    )
    server.start()
    try:
        with Zakuro(
            instances=[
                _build_local_instance(
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
