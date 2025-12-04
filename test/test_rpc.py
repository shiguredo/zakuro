"""Zakuro の JSON-RPC API テスト"""

import tempfile
import time

from conftest import SoraConfig
from zakuro import Zakuro


def test_query(sora_config: SoraConfig, free_port: int) -> None:
    """Query メソッドで SQL クエリを実行できることを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(channel_name="query_test", role="sendrecv", vcs=1)
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 接続が確立されるまで待機
            time.sleep(3)

            # シンプルなクエリを実行
            result = z.rpc.query("SELECT 1 as value")
            assert "rows" in result
            assert len(result["rows"]) == 1
            assert result["rows"][0]["value"] == 1


def test_query_connection_table(sora_config: SoraConfig, free_port: int) -> None:
    """Query メソッドで connection テーブルをクエリできることを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(channel_name="query_conn_test", role="sendrecv", vcs=1)
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 接続が確立されるまで待機
            time.sleep(3)

            # connection テーブルをクエリ
            result = z.rpc.query("SELECT COUNT(*) as count FROM connection")
            assert "rows" in result
            assert len(result["rows"]) == 1
            assert result["rows"][0]["count"] >= 1


def test_list_connections(sora_config: SoraConfig, free_port: int) -> None:
    """ListConnections メソッドで接続一覧を取得できることを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="list_conn_test", role="sendrecv", vcs=2
                )
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 接続が確立されるまで待機
            time.sleep(3)

            result = z.rpc.list_connections()
            assert "connections" in result
            assert "total_count" in result
            assert result["total_count"] >= 2


def test_list_connections_with_limit(sora_config: SoraConfig, free_port: int) -> None:
    """ListConnections メソッドで limit パラメータが機能することを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="list_conn_limit_test", role="sendrecv", vcs=3
                )
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 接続が確立されるまで待機
            time.sleep(3)

            result = z.rpc.list_connections(limit=2)
            assert "connections" in result
            assert len(result["connections"]) <= 2
            assert result["total_count"] >= 3
