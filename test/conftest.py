import itertools
import os
import time
import uuid
from dataclasses import dataclass
from typing import Any

import jwt
import pytest
from dotenv import load_dotenv

# .env ファイルを読み込む
load_dotenv()


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


@pytest.fixture(scope="session")
def port_allocator():
    """セッション全体で共有されるポート番号アロケーター

    エフェメラルポート開始の 55000 から始まるポート番号を順番に生成します。
    複数のテストが並列実行されても、各テストに一意のポート番号が割り当てられます。
    """
    return itertools.count(55000)


@pytest.fixture
def free_port(port_allocator):
    """利用可能なポート番号を提供するフィクスチャ

    各テスト関数で使用すると、自動的に一意のポート番号が割り当てられます。
    """
    return next(port_allocator)
