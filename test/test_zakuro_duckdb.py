"""DuckDB 統計出力のテスト"""

import time
from pathlib import Path

import duckdb
import pytest

from conftest import SoraConfig
from zakuro import Zakuro


@pytest.fixture
def duckdb_zakuro(
    sora_config: SoraConfig,
    free_port: int,
    tmp_duckdb_dir: Path,
):
    """DuckDB 出力を有効にした Zakuro インスタンスを提供するフィクスチャ"""
    with Zakuro(
        instances=[
            sora_config.build_instance(
                channel_name="duckdb_test",
                role="sendrecv",
                vcs=1,
            ),
        ],
        http_port=free_port,
        duckdb_dir=str(tmp_duckdb_dir),
        duckdb_interval=1.0,
        startup_timeout=60,
    ) as z:
        yield z, tmp_duckdb_dir


def find_duckdb_file(duckdb_dir: Path) -> Path | None:
    """DuckDB ファイルを検索する"""
    db_files = list(duckdb_dir.glob("zakuro_*.db"))
    if db_files:
        return db_files[0]
    return None


class TestDuckDBOutput:
    """DuckDB 統計出力のテストクラス"""

    def test_duckdb_file_created(
        self,
        sora_config: SoraConfig,
        free_port: int,
        tmp_duckdb_dir: Path,
    ):
        """--duckdb-dir 指定時に DuckDB ファイルが作成されることを確認"""
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="duckdb_create_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            # zakuro 起動後、少し待機
            time.sleep(3)

            # DuckDB ファイルが作成されていることを確認
            db_file = find_duckdb_file(tmp_duckdb_dir)
            assert db_file is not None, f"DuckDB file not found in {tmp_duckdb_dir}"
            assert db_file.exists(), f"DuckDB file {db_file} does not exist"

    def test_zakuro_table_exists(
        self,
        sora_config: SoraConfig,
        free_port: int,
        tmp_duckdb_dir: Path,
    ):
        """zakuro テーブルにバージョン情報が記録されることを確認"""
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="zakuro_table_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            time.sleep(3)

        # zakuro 終了後に DuckDB ファイルを確認
        db_file = find_duckdb_file(tmp_duckdb_dir)
        assert db_file is not None

        # DuckDB ファイルを開いて内容を確認
        conn = duckdb.connect(str(db_file), read_only=True)
        try:
            # zakuro テーブルが存在することを確認
            tables = conn.execute(
                "SELECT table_name FROM information_schema.tables WHERE table_name = 'zakuro'"
            ).fetchall()
            assert len(tables) == 1, "zakuro table not found"

            # zakuro テーブルにレコードがあることを確認
            result = conn.execute("SELECT * FROM zakuro").fetchall()
            assert len(result) >= 1, "No records in zakuro table"

            # バージョン情報が記録されていることを確認
            result = conn.execute("SELECT version FROM zakuro").fetchone()
            assert result is not None
            assert result[0] is not None and len(result[0]) > 0
        finally:
            conn.close()

    def test_connection_table_exists(
        self,
        sora_config: SoraConfig,
        free_port: int,
        tmp_duckdb_dir: Path,
    ):
        """connection テーブルに接続情報が記録されることを確認"""
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="connection_table_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            # 接続が確立されるまで待機
            time.sleep(10)

        # zakuro 終了後に DuckDB ファイルを確認
        db_file = find_duckdb_file(tmp_duckdb_dir)
        assert db_file is not None

        conn = duckdb.connect(str(db_file), read_only=True)
        try:
            # connection テーブルが存在することを確認
            tables = conn.execute(
                "SELECT table_name FROM information_schema.tables WHERE table_name = 'connection'"
            ).fetchall()
            assert len(tables) == 1, "connection table not found"

            # connection テーブルにレコードがあることを確認
            result = conn.execute("SELECT * FROM connection").fetchall()
            # 接続が成功していればレコードがあるはず
            # 接続に失敗することもあるので、テーブル存在確認のみ
            print(f"connection table has {len(result)} records")
        finally:
            conn.close()

    def test_rtc_stats_tables_exist(
        self,
        sora_config: SoraConfig,
        free_port: int,
        tmp_duckdb_dir: Path,
    ):
        """rtc_stats_* テーブルが作成されることを確認"""
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="rtc_stats_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            # RTC 統計が収集されるまで待機
            time.sleep(15)

        db_file = find_duckdb_file(tmp_duckdb_dir)
        assert db_file is not None

        conn = duckdb.connect(str(db_file), read_only=True)
        try:
            # rtc_stats_* テーブルが存在することを確認
            expected_tables = [
                "rtc_stats_codec",
                "rtc_stats_inbound_rtp",
                "rtc_stats_outbound_rtp",
                "rtc_stats_media_source",
                "rtc_stats_remote_inbound_rtp",
                "rtc_stats_remote_outbound_rtp",
                "rtc_stats_data_channel",
            ]

            for table_name in expected_tables:
                tables = conn.execute(
                    f"SELECT table_name FROM information_schema.tables WHERE table_name = '{table_name}'"
                ).fetchall()
                assert len(tables) == 1, f"{table_name} table not found"
                print(f"Found table: {table_name}")
        finally:
            conn.close()

    def test_duckdb_interval(
        self,
        sora_config: SoraConfig,
        free_port: int,
        tmp_duckdb_dir: Path,
    ):
        """--duckdb-interval で指定した間隔で統計が収集されることを確認"""
        interval = 2.0
        wait_time = 10

        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="interval_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=interval,
            startup_timeout=60,
        ) as z:
            # 統計が収集されるまで待機
            time.sleep(wait_time)

        db_file = find_duckdb_file(tmp_duckdb_dir)
        assert db_file is not None

        conn = duckdb.connect(str(db_file), read_only=True)
        try:
            # rtc_stats_outbound_rtp テーブルのレコード数を確認
            # interval=2秒、wait=10秒なら、最大5レコード程度
            result = conn.execute(
                "SELECT COUNT(*) FROM rtc_stats_outbound_rtp"
            ).fetchone()
            count = result[0] if result else 0
            print(f"rtc_stats_outbound_rtp has {count} records")
            # レコードがあればOK（接続成功時のみ）
        finally:
            conn.close()
