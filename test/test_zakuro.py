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
        assert "duckdb" in version
        assert "sora_cpp_sdk" in version
        assert "libwebrtc" in version
        assert "boost" in version


def test_list_connections(sora_config: SoraConfig, free_port: int) -> None:
    """接続一覧を取得できることを確認"""
    with Zakuro(
        instances=[sora_config.build_instance(channel_name="list_connections", role="sendrecv", vcs=2)],
        http_port=free_port,
    ) as z:
        result = z.rpc.list_connections()
        assert "connections" in result
        assert "total_count" in result
        assert result["total_count"] == 2


def test_query(sora_config: SoraConfig, free_port: int) -> None:
    """SQL クエリを実行できることを確認"""
    with Zakuro(
        instances=[sora_config.build_instance(channel_name="query", role="sendrecv", vcs=1)],
        http_port=free_port,
    ) as z:
        result = z.rpc.query("SELECT 1 as value")
        assert "rows" in result
        assert len(result["rows"]) == 1
        assert result["rows"][0]["value"] == 1
