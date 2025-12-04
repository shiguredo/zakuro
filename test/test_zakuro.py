"""Zakuro の基本的なテスト"""

from conftest import SoraConfig
from zakuro import Zakuro


def test_version(sora_config: SoraConfig, free_port: int) -> None:
    """バージョン情報を取得できることを確認"""
    with Zakuro(
        instances=[sora_config.build_instance(channel_name="version", role="sendrecv", vcs=1)],
        http_port=free_port,
    ) as z:
        version = z.rpc.get_version()
        assert "zakuro" in version
        assert "sora_cpp_sdk" in version
        assert "libwebrtc" in version
        assert "boost" in version
