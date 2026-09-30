"""`--log-level` がログの出力を絞り込むことの E2E テスト

stderr とログファイル (`webrtc_logs_0`) の両方に指定したログレベルが適用されることを
検証する。libwebrtc のログ行は "[<タイムスタンプ>][<スレッド ID>] (<ファイル>:<行>): "
で始まる。zakuro がログ機構を経由せず std::cerr へ直接出すメッセージはこの形式を
持たないため、ログ行だけを対象に検証する。
"""

import re
import subprocess
import time
from pathlib import Path
from typing import Any

from test_helpers import (
    CONFIG_ERROR_TIMEOUT_SECONDS,
    DATA_CHANNELS_MESSAGE_PREFIX,
    STARTUP_WAIT_SECONDS,
    VALID_INSTANCE,
    terminate_zakuro,
    write_config_object,
)
from zakuro import get_zakuro_executable_path

# libwebrtc のログ行の接頭辞。タイムスタンプ・スレッド ID・ファイル:行を持つ。
# キュー名が付くと [スレッド ID:キュー名] になりこの正規表現に一致しない
LOG_LINE_PATTERN = re.compile(r"^\[\d+:\d+\]\[\d+\] \([^)]+:\d+\): ")

# LS_INFO で出力されるファイルディスクリプタの必要数のログの断片
FILE_DESCRIPTOR_MARKER = "file descriptor limit:"

# LS_VERBOSE でのみ出力される AEC3 の設定ログの断片
# --log-level verbose で LS_VERBOSE 以上のログが通ることを確認する
VERBOSE_LOG_MARKERS = ("(render_delay_buffer.cc:", "(matched_filter.cc:")


def _invalid_data_channels_instance() -> dict[str, Any]:
    """DataChannels の解析に失敗するインスタンス設定を返す

    接続前の解析で失敗して短時間で終了するため、LS_ERROR のログを確実に観測できる。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["sora"]["data-channels"] = [
        {"label": "log-level-test", "direction": "sendrecv", "compress": "true"}
    ]
    return instance


def _log_lines(stderr: str) -> list[str]:
    """stderr から libwebrtc のログ行だけを取り出す"""
    return [line for line in stderr.splitlines() if LOG_LINE_PATTERN.match(line)]


def _normalize_log_lines(lines: list[str]) -> set[str]:
    """タイムスタンプとスレッド ID を落としてログ行を比較できるようにする"""
    return {LOG_LINE_PATTERN.sub("", line) for line in lines}


def _log_file_text(working_directory: Path) -> str:
    """ログファイルの内容を返す"""
    log_file = working_directory / "webrtc_logs_0"
    assert log_file.exists(), "webrtc_logs_0 が作られていない"
    return log_file.read_text(encoding="utf-8")


def _run_zakuro(
    config_path: Path, extra_args: list[str], working_directory: Path
) -> subprocess.CompletedProcess[str]:
    """zakuro 実バイナリを起動して終了まで待つ

    ログファイルは作業ディレクトリに作られるため、テストごとに分けたディレクトリを
    cwd に指定する。
    """
    args = [get_zakuro_executable_path(), "--config", str(config_path), *extra_args]
    return subprocess.run(
        args,
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=working_directory,
    )


def _capture_startup_stderr(
    config_path: Path, extra_args: list[str], working_directory: Path
) -> str:
    """起動直後の stderr を集めて返す

    接続先は到達しない URL なので接続は成功しない。起動時のログが出そろうまで待ってから
    終了させる。SIGTERM で終了させるため、その前に出た出力だけが対象になる。
    """
    args = [get_zakuro_executable_path(), "--config", str(config_path), *extra_args]
    process = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=working_directory,
    )
    try:
        time.sleep(STARTUP_WAIT_SECONDS)
    finally:
        _, stderr = terminate_zakuro(process)
    return stderr


def test_log_level_none_suppresses_all_logs(tmp_path: Path) -> None:
    """--log-level none で stderr とログファイルのどちらにもログが出ないことを確認する

    LS_NONE は LoggingConfig のしきい値としては全抑止になる。ログ機構を経由しない
    std::cerr の出力 (解析失敗の通知) は残るが、ログ行は 1 行も出ない。
    空の webrtc_logs_0 が作られることは許容する。
    """
    config_path = write_config_object(
        tmp_path, "log_level_none.jsonc", {"instances": [_invalid_data_channels_instance()]}
    )
    result = _run_zakuro(config_path, ["--log-level", "none"], tmp_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\nstderr={result.stderr!r}"
    )
    # ログ機構を経由しないメッセージはログレベルに関係なく出る
    assert "failed to parse DataChannels" in result.stderr, (
        f"解析失敗の通知が出ていない: stderr={result.stderr!r}"
    )
    assert _log_lines(result.stderr) == [], f"ログが出力されている: stderr={result.stderr!r}"
    assert _log_file_text(tmp_path) == "", "ログファイルにログが出力されている"


def test_log_level_error_suppresses_info_logs(tmp_path: Path) -> None:
    """設定ファイルの log-level が error の場合に LS_ERROR 以上のログだけが出ることを確認する

    設定ファイルの log-level は --log-level に変換されて CLI と同じ経路に入る。
    LS_INFO のファイルディスクリプタのログが出ず、LS_ERROR の解析失敗の理由だけが
    stderr とログファイルに出ることを確認する。
    """
    config: dict[str, Any] = {
        "log-level": "error",
        "instances": [_invalid_data_channels_instance()],
    }
    config_path = write_config_object(tmp_path, "log_level_error.jsonc", config)
    result = _run_zakuro(config_path, [], tmp_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\nstderr={result.stderr!r}"
    )
    # stderr には別スレッドの std::cerr 出力が行単位で割り込むことがあるため
    # 部分一致で確認する
    assert DATA_CHANNELS_MESSAGE_PREFIX in result.stderr, (
        f"LS_ERROR のログが出ていない: stderr={result.stderr!r}"
    )
    assert FILE_DESCRIPTOR_MARKER not in result.stderr, (
        f"LS_INFO のログが出ている: stderr={result.stderr!r}"
    )
    log_text = _log_file_text(tmp_path)
    assert DATA_CHANNELS_MESSAGE_PREFIX in log_text, (
        f"ログファイルに LS_ERROR のログが出ていない: log={log_text!r}"
    )
    assert FILE_DESCRIPTOR_MARKER not in log_text, (
        f"ログファイルに LS_INFO のログが出ている: log={log_text!r}"
    )


def test_log_level_info_matches_default(tmp_path: Path) -> None:
    """--log-level 未指定と info の出力が同じで、ログの形式が現行と同じことを確認する

    既定値はこれまでどおり LS_INFO 以上のログを出す。形式は
    [タイムスタンプ][スレッド ID] (ファイル:行): のままで、キュー名が増えたり
    スレッド ID が消えたりしない。差分はタイムスタンプとスレッド ID だけである。
    """
    default_dir = tmp_path / "default"
    info_dir = tmp_path / "info"
    default_dir.mkdir()
    info_dir.mkdir()
    config_path = write_config_object(
        tmp_path, "log_level_info.jsonc", {"instances": [_invalid_data_channels_instance()]}
    )

    default_result = _run_zakuro(config_path, [], default_dir)
    info_result = _run_zakuro(config_path, ["--log-level", "info"], info_dir)

    # stderr には別スレッドの std::cerr 出力が行単位で割り込むことがあるため、
    # 行を組み立てる検証は割り込みの起きないログファイルで行う
    for result in (default_result, info_result):
        assert result.returncode == 1, (
            f"終了コードが 1 ではない: returncode={result.returncode}\nstderr={result.stderr!r}"
        )
        # 未指定でも LS_INFO と LS_ERROR の両方が出ること
        assert FILE_DESCRIPTOR_MARKER in result.stderr, (
            f"LS_INFO のログが出ていない: stderr={result.stderr!r}"
        )
        assert DATA_CHANNELS_MESSAGE_PREFIX in result.stderr, (
            f"LS_ERROR のログが出ていない: stderr={result.stderr!r}"
        )

    default_log_lines = _log_lines(_log_file_text(default_dir))
    info_log_lines = _log_lines(_log_file_text(info_dir))

    # ログの形式が現行どおり [タイムスタンプ][スレッド ID] (ファイル:行): で始まること
    assert any(
        re.match(r"^\[\d+:\d+\]\[\d+\] \(main\.cpp:\d+\): file descriptor limit:", line)
        for line in default_log_lines
    ), f"ログの形式が変わっている: lines={default_log_lines!r}"

    # ログファイルにも LS_INFO と LS_ERROR の両方が出ること
    for log_lines in (default_log_lines, info_log_lines):
        assert any(FILE_DESCRIPTOR_MARKER in line for line in log_lines), (
            f"ログファイルに LS_INFO のログが出ていない: lines={log_lines!r}"
        )
        assert any(DATA_CHANNELS_MESSAGE_PREFIX in line for line in log_lines), (
            f"ログファイルに LS_ERROR のログが出ていない: lines={log_lines!r}"
        )

    # 未指定と info の出力が一致すること (差分はタイムスタンプとスレッド ID のみ)
    assert _normalize_log_lines(default_log_lines) == _normalize_log_lines(info_log_lines), (
        f"未指定と info の出力が異なる: default={default_log_lines!r} info={info_log_lines!r}"
    )


def test_log_level_verbose_outputs_verbose_logs(tmp_path: Path) -> None:
    """--log-level verbose で LS_INFO に加えて LS_VERBOSE のログも出ることを確認する

    verbose は LS_INFO 以上のログをすべて通すため、info の出力に加えて LS_VERBOSE の
    ログが増える。webrtc の音声処理 (AEC3) は起動時に LS_VERBOSE のログを出すため、
    info では現れず verbose でだけ現れることを確認できる。
    """
    info_dir = tmp_path / "info"
    verbose_dir = tmp_path / "verbose"
    info_dir.mkdir()
    verbose_dir.mkdir()
    config_path = write_config_object(
        tmp_path, "log_level_verbose.jsonc", {"instances": [VALID_INSTANCE]}
    )

    info_stderr = _capture_startup_stderr(config_path, ["--log-level", "info"], info_dir)
    verbose_stderr = _capture_startup_stderr(config_path, ["--log-level", "verbose"], verbose_dir)

    # stderr には別スレッドの std::cerr 出力が行単位で割り込むことがあるため、
    # 行を組み立てる検証は割り込みの起きないログファイルで行う
    assert FILE_DESCRIPTOR_MARKER in info_stderr, (
        f"info に LS_INFO のログが出ていない: stderr={info_stderr!r}"
    )
    assert FILE_DESCRIPTOR_MARKER in verbose_stderr, (
        f"verbose に LS_INFO のログが出ていない: stderr={verbose_stderr!r}"
    )

    info_lines = _log_lines(_log_file_text(info_dir))
    verbose_lines = _log_lines(_log_file_text(verbose_dir))

    assert info_lines, f"info のログが出ていない: lines={info_lines!r}"
    assert verbose_lines, f"verbose のログが出ていない: lines={verbose_lines!r}"
    # LS_VERBOSE のログは verbose でだけ出ること
    assert not any(any(marker in line for marker in VERBOSE_LOG_MARKERS) for line in info_lines), (
        f"info に LS_VERBOSE のログが出ている: lines={info_lines!r}"
    )
    assert any(any(marker in line for marker in VERBOSE_LOG_MARKERS) for line in verbose_lines), (
        f"verbose に LS_VERBOSE のログが出ていない: lines={verbose_lines!r}"
    )
    assert len(verbose_lines) > len(info_lines), (
        f"verbose が info より多くのログを出していない: "
        f"info={len(info_lines)} verbose={len(verbose_lines)}"
    )
    # ログファイルにも LS_VERBOSE のログが出ること
    verbose_log = _log_file_text(verbose_dir)
    assert any(marker in verbose_log for marker in VERBOSE_LOG_MARKERS), (
        f"ログファイルに LS_VERBOSE のログが出ていない: log={verbose_log!r}"
    )
