"""pytest 共通のフィクスチャとヘルパー

実 Sora へ接続するテスト用の設定、テスト用の証明書を生成するフィクスチャ、Sora の
signaling サーバーの代わりに使うローカルサーバーを提供する。ローカルサーバーは実 TCP で
待ち受け、zakuro 実バイナリが接続処理を進めたかどうかを観測するために TLS ハンドシェイク
だけを行い、WebSocket のハンドシェイクには応答しない。

テストモジュールからは次の共有ヘルパーを import して使う。

- TlsProbeServer: TLS ハンドシェイクだけを行うローカルサーバー
- build_local_instance: ローカルサーバーへ接続する zakuro インスタンス設定の構築
- wait_for_stderr: zakuro の stderr に特定の文字列が出るまでの待機
- SoraConfig / get_deps_versions / get_zakuro_version: 実 Sora へ接続するテスト用の設定
"""

import os
import shutil
import socket
import ssl
import subprocess
import threading
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import jwt
import pytest
from dotenv import load_dotenv

from zakuro import Zakuro

# .env ファイルを読み込む
load_dotenv()

# プロジェクトルートディレクトリ
PROJECT_ROOT = Path(__file__).parent.parent

# クライアント証明書の commonName と一致することを確認する
CLIENT_CERT_COMMON_NAME = "zakuro-test-client"


def get_zakuro_version() -> str:
    """VERSION ファイルから zakuro のバージョンを取得"""
    version_file = PROJECT_ROOT / "VERSION"
    return version_file.read_text().strip()


def get_deps_versions() -> dict[str, str]:
    """DEPS ファイルから依存ライブラリのバージョンを取得"""
    deps_file = PROJECT_ROOT / "DEPS"
    versions = {}
    for line in deps_file.read_text().strip().split("\n"):
        if "=" in line:
            key, value = line.split("=", 1)
            versions[key] = value
    return versions


@dataclass
class SoraConfig:
    """Sora 接続用の設定"""

    signaling_urls: list[str]
    channel_id_prefix: str
    secret_key: str

    def build_channel_id(self, channel_name: str) -> str:
        """チャンネル ID を生成する"""
        return f"{self.channel_id_prefix}{channel_name}_{uuid.uuid4().hex[:8]}"

    def build_metadata(self, channel_id: str) -> dict[str, Any]:
        """メタデータを生成する"""
        payload = {
            "channel_id": channel_id,
            "exp": int(time.time()) + 300,
        }
        access_token = jwt.encode(payload, self.secret_key, algorithm="HS256")
        return {"access_token": access_token}

    def build_instance(
        self,
        *,
        channel_name: str,
        role: str,
        vcs: int = 1,
        no_video_device: bool = True,
        no_audio_device: bool = True,
        **kwargs: Any,
    ) -> dict[str, Any]:
        """zakuro インスタンス設定を構築する

        Args:
            channel_name: チャンネル名（channel_id の生成に使用）
            role: Sora のロール (sendonly, recvonly, sendrecv)
            vcs: 仮想クライアント数
            no_video_device: ビデオデバイスを無効化
            no_audio_device: オーディオデバイスを無効化
            **kwargs: インスタンス設定に追加するその他のオプション

        Returns:
            インスタンス設定の dict
        """
        channel_id = self.build_channel_id(channel_name)
        instance: dict[str, Any] = {
            "sora": {
                "signaling-url": self.signaling_urls,
                "channel-id": channel_id,
                "role": role,
                "metadata": self.build_metadata(channel_id),
            },
            "vcs": vcs,
        }

        if no_video_device:
            instance["no-video-device"] = True
        if no_audio_device:
            instance["no-audio-device"] = True

        # 追加のオプションをマージ
        instance.update(kwargs)

        return instance


@pytest.fixture
def sora_config() -> SoraConfig:
    """Sora 接続用の設定を提供するフィクスチャ"""
    # 環境変数から設定を取得（必須）
    signaling_urls_str = os.environ.get("TEST_SIGNALING_URLS")
    if not signaling_urls_str:
        pytest.skip("TEST_SIGNALING_URLS environment variable is required")

    # カンマ区切りをリストに変換
    signaling_urls = [url.strip() for url in signaling_urls_str.split(",")]

    channel_id_prefix = os.environ.get("TEST_CHANNEL_ID_PREFIX")
    if not channel_id_prefix:
        pytest.skip("TEST_CHANNEL_ID_PREFIX environment variable is required")

    secret_key = os.environ.get("TEST_SECRET_KEY")
    if not secret_key:
        pytest.skip("TEST_SECRET_KEY environment variable is required")

    return SoraConfig(
        signaling_urls=signaling_urls,
        channel_id_prefix=channel_id_prefix,
        secret_key=secret_key,
    )


@pytest.fixture
def free_port() -> int:
    """利用可能なポート番号を提供するフィクスチャ

    OS に空きポートを割り当ててもらうことで、ポート衝突を回避します。
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


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


@dataclass(frozen=True)
class ServerCertificates:
    """テスト用に生成した自己署名のサーバー証明書と秘密鍵"""

    server_cert: Path
    server_key: Path


def _run_openssl(args: list[str]) -> None:
    """openssl コマンドを実行する"""
    result = subprocess.run(args, capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        # 失敗時は openssl の出力を含めて原因を分かりやすくする
        raise RuntimeError(
            f"openssl command failed: args={' '.join(args)} stderr={result.stderr.strip()}"
        )


def _generate_self_signed_certificate(
    openssl: str,
    key_path: Path,
    cert_path: Path,
    *,
    common_name: str,
    is_ca: bool = False,
) -> None:
    """自己署名証明書と秘密鍵を生成する

    is_ca が真の場合は CA 証明書として生成する。
    """
    args = [
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
        str(key_path),
        "-out",
        str(cert_path),
        "-subj",
        f"/CN={common_name}",
    ]
    if is_ca:
        # openssl.cnf の内容に依存せず CA であることを明示する
        args += ["-addext", "basicConstraints=critical,CA:TRUE"]
    _run_openssl(args)


def _sign_certificate_request(
    openssl: str,
    *,
    csr_path: Path,
    cert_path: Path,
    ca_cert: Path,
    ca_key: Path,
) -> None:
    """CSR に CA で署名して証明書を生成する"""
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


def _generate_client_certificate(
    openssl: str,
    *,
    key_path: Path,
    csr_path: Path,
    cert_path: Path,
    ca_cert: Path,
    ca_key: Path,
) -> None:
    """秘密鍵から CSR を作り、CA で署名したクライアント証明書を生成する

    CN は mTLS の検証で確認する CLIENT_CERT_COMMON_NAME にする。
    """
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
    _sign_certificate_request(
        openssl,
        csr_path=csr_path,
        cert_path=cert_path,
        ca_cert=ca_cert,
        ca_key=ca_key,
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
    _generate_self_signed_certificate(
        openssl, ca_key, ca_cert, common_name="zakuro-test-ca", is_ca=True
    )
    # サーバー証明書を作成する
    # zakuro は insecure で接続するため、サーバー証明書の検証は行われない
    _generate_self_signed_certificate(openssl, server_key, server_cert, common_name="127.0.0.1")
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
    _sign_certificate_request(
        openssl,
        csr_path=client_csr,
        cert_path=client_cert,
        ca_cert=ca_cert,
        ca_key=ca_key,
    )

    return MtlsCertificates(
        ca_cert=ca_cert,
        ca_key=ca_key,
        server_cert=server_cert,
        server_key=server_key,
        client_cert=client_cert,
        client_key=client_key,
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
            raise RuntimeError(f"failed to convert to RSA PRIVATE KEY: {rsa_client_key}")
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


@pytest.fixture
def server_certificates(tmp_path: Path, openssl_path: str) -> ServerCertificates:
    """テスト用の自己署名サーバー証明書と秘密鍵を生成する

    zakuro は insecure で接続するため、サーバー証明書の検証は行われない。
    """
    server_key = tmp_path / "server-key.pem"
    server_cert = tmp_path / "server-cert.pem"
    # 証明書の CN は接続先のホスト名に合わせる
    _generate_self_signed_certificate(
        openssl_path, server_key, server_cert, common_name="127.0.0.1"
    )
    return ServerCertificates(server_cert=server_cert, server_key=server_key)


def build_local_instance(
    signaling_url: str,
    *,
    client_cert: Path | None = None,
    client_key: Path | None = None,
    channel_id: str = "mtls-test",
    extra: dict[str, object] | None = None,
) -> dict[str, object]:
    """ローカルの TLS サーバーへ接続する zakuro インスタンス設定を構築する

    Args:
        signaling_url: 接続先の signaling URL
        client_cert: mTLS で送信するクライアント証明書 (不要な場合は None)
        client_key: mTLS で送信するクライアント秘密鍵 (不要な場合は None)
        channel_id: Sora のチャンネル ID
        extra: インスタンス設定に追加する任意のキー。
            zakuro の設定キー名をそのまま指定する

    Returns:
        インスタンス設定の dict
    """
    instance: dict[str, object] = {
        "sora": {
            "signaling-url": signaling_url,
            "channel-id": channel_id,
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
    if extra is not None:
        instance.update(extra)
    return instance


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


def wait_for_stderr(z: Zakuro, text: str, timeout: float = 15.0) -> bool:
    """zakuro の stderr に指定した文字列が出力されるまで待つ"""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if text in z.stderr_output:
            return True
        time.sleep(0.1)
    return text in z.stderr_output


class TlsProbeServer:
    """TLS ハンドシェイクだけを行うローカルサーバー

    zakuro が接続処理を進めたかどうかをサーバー側で観測する。
    WebSocket のハンドシェイクには応答しない。

    ca_cert を指定した場合はクライアント証明書を必須にして、送信された証明書を
    ca_cert で検証する。require_client_cert が偽の場合はクライアント証明書を要求せず、
    ca_cert を指定しても検証には使わない。
    """

    def __init__(
        self,
        *,
        server_cert: Path,
        server_key: Path,
        ca_cert: Path | None = None,
        require_client_cert: bool = True,
    ) -> None:
        if require_client_cert and ca_cert is None:
            # 検証用の CA が無いまま「クライアント証明書を必須にする」と、
            # CERT_NONE のサーバーが立ってテストが空振りする
            raise ValueError("ca_cert is required when require_client_cert is True")
        self._context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self._context.load_cert_chain(certfile=server_cert, keyfile=server_key)
        if ca_cert is not None and require_client_cert:
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
