import os
import time
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import jwt
import pytest
from dotenv import load_dotenv

# .env ファイルを読み込む
load_dotenv()

# プロジェクトルートディレクトリ
PROJECT_ROOT = Path(__file__).parent.parent


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
        audio: bool = True,
        video: bool = True,
        fake_capture_device: bool = True,
        **kwargs: Any,
    ) -> dict[str, Any]:
        """zakuro インスタンス設定を構築する

        Args:
            channel_name: チャンネル名（channel_id の生成に使用）
            role: Sora のロール (sendonly, recvonly, sendrecv)
            vcs: 仮想クライアント数
            audio: 音声を有効化
            video: 映像を有効化
            fake_capture_device: フェイクキャプチャデバイスを使用
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
                "audio": audio,
                "video": video,
            },
            "vcs": vcs,
            "fake-capture-device": fake_capture_device,
        }

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
def free_port():
    """利用可能なポート番号を提供するフィクスチャ

    OS に空きポートを割り当ててもらうことで、ポート衝突を回避します。
    """
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


