"""DuckDB 統計出力のテスト"""

import tempfile
import time
from pathlib import Path

import duckdb

from conftest import SoraConfig
from zakuro import Zakuro


def find_duckdb_file(duckdb_dir: Path) -> Path | None:
    """DuckDB ファイルを検索する"""
    db_files = list(duckdb_dir.glob("zakuro_*.db"))
    if db_files:
        return db_files[0]
    return None


def test_duckdb_file_created(
    sora_config: SoraConfig,
    free_port: int,
):
    """--duckdb-dir 指定時に DuckDB ファイルが作成されることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
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
    sora_config: SoraConfig,
    free_port: int,
):
    """zakuro テーブルにバージョン情報が記録されることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
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
    sora_config: SoraConfig,
    free_port: int,
):
    """connection テーブルに接続情報が記録されることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
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
            assert len(result) >= 1, "No records in connection table"

            # 必須カラムに値が入っていることを確認
            row = conn.execute(
                "SELECT channel_id, session_id, role FROM connection LIMIT 1"
            ).fetchone()
            assert row is not None
            assert row[0] is not None and len(row[0]) > 0, "channel_id is empty"
            assert row[1] is not None and len(row[1]) > 0, "session_id is empty"
            assert row[2] is not None and len(row[2]) > 0, "role is empty"
        finally:
            conn.close()


def test_rtc_stats_tables_exist(
    sora_config: SoraConfig,
    free_port: int,
):
    """rtc_stats_* テーブルが作成されることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
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

            # 少なくとも outbound_rtp にはレコードがあることを確認（送信側統計）
            result = conn.execute(
                "SELECT COUNT(*) FROM rtc_stats_outbound_rtp"
            ).fetchone()
            assert result is not None
            assert result[0] >= 1, "No records in rtc_stats_outbound_rtp table"

            # outbound_rtp の必須カラムに値が入っていることを確認
            row = conn.execute(
                "SELECT channel_id, session_id, ssrc, kind FROM rtc_stats_outbound_rtp LIMIT 1"
            ).fetchone()
            assert row is not None
            assert row[0] is not None and len(row[0]) > 0, "channel_id is empty"
            assert row[1] is not None and len(row[1]) > 0, "session_id is empty"
            assert row[2] is not None, "ssrc is empty"
            assert row[3] is not None and len(row[3]) > 0, "kind is empty"
        finally:
            conn.close()


def test_duckdb_interval(
    sora_config: SoraConfig,
    free_port: int,
):
    """--duckdb-interval で指定した間隔で統計が収集されることを確認"""
    interval = 2.0
    wait_time = 10

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
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
            # interval=2秒、wait=10秒なら、最低でも数レコードはあるはず
            result = conn.execute(
                "SELECT COUNT(*) FROM rtc_stats_outbound_rtp"
            ).fetchone()
            assert result is not None
            count = result[0]
            assert count >= 1, "No records in rtc_stats_outbound_rtp table"

            # 複数回の統計収集が行われていることを確認
            # interval=2秒、wait=10秒なので、少なくとも2回以上は収集されているはず
            assert count >= 2, f"Expected at least 2 records, but got {count}"
        finally:
            conn.close()


def test_query_rpc(
    sora_config: SoraConfig,
    free_port: int,
):
    """Query RPC で DuckDB にクエリを実行できることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="query_rpc_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            # zakuro テーブルにデータが入るまで待機
            time.sleep(3)

            # 単純な SELECT クエリを実行
            rows = z.rpc.query("SELECT version FROM zakuro")
            assert isinstance(rows, list)
            assert len(rows) >= 1

            # バージョン情報が取得できることを確認
            assert rows[0]["version"] is not None


def test_query_rpc_with_cte(
    sora_config: SoraConfig,
    free_port: int,
):
    """Query RPC で WITH 句（CTE）を含むクエリを実行できることを確認"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="query_rpc_cte_test",
                    role="sendrecv",
                    vcs=1,
                ),
            ],
            http_port=free_port,
            duckdb_dir=str(tmp_duckdb_dir),
            duckdb_interval=1.0,
            startup_timeout=60,
        ) as z:
            # zakuro テーブルにデータが入るまで待機
            time.sleep(3)

            # WITH 句を使ったクエリを実行
            rows = z.rpc.query("""
                WITH zakuro_versions AS (
                    SELECT version, webrtc_version
                    FROM zakuro
                )
                SELECT * FROM zakuro_versions
            """)
            assert isinstance(rows, list)
            assert len(rows) >= 1

            # カラムが期待通りであることを確認
            assert "version" in rows[0]
            assert "webrtc_version" in rows[0]


def test_query_rpc_reject_non_select(
    sora_config: SoraConfig,
    free_port: int,
):
    """Query RPC で SELECT 以外のクエリが拒否されることを確認"""
    import pytest

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_duckdb_dir = Path(tmp_dir)
        with Zakuro(
            instances=[
                sora_config.build_instance(
                    channel_name="query_rpc_reject_test",
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

            # INSERT クエリは拒否されるべき
            with pytest.raises(RuntimeError) as excinfo:
                z.rpc.query("INSERT INTO zakuro (version) VALUES ('test')")
            assert "Only SELECT queries are allowed" in str(excinfo.value)

            # DELETE クエリは拒否されるべき
            with pytest.raises(RuntimeError) as excinfo:
                z.rpc.query("DELETE FROM zakuro")
            assert "Only SELECT queries are allowed" in str(excinfo.value)

            # DROP クエリは拒否されるべき
            with pytest.raises(RuntimeError) as excinfo:
                z.rpc.query("DROP TABLE zakuro")
            assert "Only SELECT queries are allowed" in str(excinfo.value)
