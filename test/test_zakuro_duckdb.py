"""Zakuro の DuckDB 統計出力テスト"""

import glob
import tempfile
import time

import duckdb

from conftest import SoraConfig
from zakuro import Zakuro


def test_duckdb_output(sora_config: SoraConfig, free_port: int) -> None:
    """DuckDB 統計出力が正常に動作することを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(channel_name="duckdb_test", role="sendrecv", vcs=1)
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 統計が書き込まれるまで少し待機
            time.sleep(3)

            # DuckDB ファイルが生成されたことを確認
            db_files = glob.glob(f"{tmpdir}/zakuro_*.db")
            assert len(db_files) == 1, f"Expected 1 DuckDB file, found {len(db_files)}"

            db_path = db_files[0]

            # DuckDB ファイルを読み取り、テーブルが存在することを確認
            conn = duckdb.connect(db_path, read_only=True)
            try:
                # テーブル一覧を取得
                tables = conn.execute(
                    "SELECT table_name FROM information_schema.tables WHERE table_schema = 'main'"
                ).fetchall()
                table_names = [t[0] for t in tables]

                # 期待されるテーブルが存在することを確認
                expected_tables = [
                    "zakuro",
                    "connection",
                    "rtc_stats_codec",
                    "rtc_stats_inbound_rtp",
                    "rtc_stats_outbound_rtp",
                    "rtc_stats_media_source",
                    "rtc_stats_remote_inbound_rtp",
                    "rtc_stats_remote_outbound_rtp",
                    "rtc_stats_data_channel",
                ]
                for table in expected_tables:
                    assert table in table_names, f"Table '{table}' not found in DuckDB"

                # zakuro テーブルにレコードがあることを確認
                zakuro_count = conn.execute("SELECT COUNT(*) FROM zakuro").fetchone()[0]
                assert zakuro_count >= 1, "zakuro table should have at least 1 record"

                # connection テーブルにレコードがあることを確認
                connection_count = conn.execute("SELECT COUNT(*) FROM connection").fetchone()[0]
                assert connection_count >= 1, "connection table should have at least 1 record"

            finally:
                conn.close()


def test_duckdb_rtc_stats(sora_config: SoraConfig, free_port: int) -> None:
    """DuckDB に WebRTC 統計が記録されることを確認"""
    with tempfile.TemporaryDirectory() as tmpdir:
        with Zakuro(
            instances=[
                sora_config.build_instance(channel_name="rtc_stats_test", role="sendrecv", vcs=1)
            ],
            http_port=free_port,
            duckdb_dir=tmpdir,
        ) as z:
            # 統計収集が行われるまで待機（デフォルト間隔は 1 秒）
            time.sleep(5)

            db_files = glob.glob(f"{tmpdir}/zakuro_*.db")
            assert len(db_files) == 1

            conn = duckdb.connect(db_files[0], read_only=True)
            try:
                # 少なくとも 1 つの RTC 統計テーブルにデータがあることを確認
                rtc_tables = [
                    "rtc_stats_codec",
                    "rtc_stats_inbound_rtp",
                    "rtc_stats_outbound_rtp",
                    "rtc_stats_media_source",
                ]
                has_rtc_data = False
                for table in rtc_tables:
                    count = conn.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
                    if count > 0:
                        has_rtc_data = True
                        break

                assert has_rtc_data, "At least one RTC stats table should have records"

            finally:
                conn.close()
