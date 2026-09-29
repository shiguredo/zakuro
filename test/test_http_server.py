"""HTTP サーバーの起動失敗と accept のバックオフの E2E テスト

`--http-host` / `--http-port` の指定で resolve または bind に失敗した場合に、
`main` が起動失敗として非ゼロで終了し、起動していないサーバーを「起動した」と
ログに出さないことを検証する。あわせて fd 枯渇で accept が失敗し続ける場合に、
即座に再試行せず待ってから再試行することを検証する。

設定ファイルの異常入力は test_config_json.py が検証する。
`HttpServer::Stop` を複数スレッドから同時に呼ぶ検証は、実バイナリでは再現できない
(プロセス内で Stop は 1 回しか呼ばれない) ため、このテストの対象外とする。
"""

import contextlib
import resource
import socket
import subprocess
import time
from pathlib import Path

import pytest

from test_helpers import (
    CONFIG_ERROR_TIMEOUT_SECONDS,
    HTTP_SERVER_STARTED_MARKER,
    STARTUP_WAIT_SECONDS,
    VALID_INSTANCE,
    terminate_zakuro,
    wait_for_stderr_line,
    write_config_object,
)
from zakuro import get_zakuro_executable_path

# 起動に失敗したときに出るログの断片
HTTP_BIND_ERROR_MARKER = "Bind error:"
HTTP_RESOLVE_ERROR_MARKER = "Resolve error:"

# 起動に失敗したときに main が出すメッセージの断片
HTTP_START_FAILED_MARKER = "failed to start HTTP server on"

# accept が失敗したときに出すログの断片
HTTP_ACCEPT_ERROR_MARKER = "Accept error:"

# accept の再試行を待つときに出すログの断片
HTTP_ACCEPT_RETRY_MARKER = "Accept failed, retrying after"

# accept を再試行するまでの待ち時間 (ミリ秒)
# src/http_server.h の kAcceptRetryDelayMs と揃える
ACCEPT_RETRY_DELAY_MS = 1000

# ローカルのどのインターフェースにも割り当てられていないアドレス
# (TEST-NET-1)。この環境では bind が失敗する
UNASSIGNABLE_HOST = "192.0.2.1"

# 解決できないホスト名 (RFC 6761 の予約 TLD)。resolve が失敗する
UNRESOLVABLE_HOST = "no-such-host.invalid"

# fd 枯渇を再現するときに子プロセスへ設定するファイルディスクリプタの上限
# zakuro の起動には vcs * 5 = 5 個あれば足りるため、起動はできて accept だけが失敗する
FD_LIMIT_FOR_EXHAUSTION = 64

# fd を枯渇させるために張るクライアント接続の数
# 上限 64 に対して十分な数を張り、accept が EMFILE で失敗する状態にする
FD_EXHAUSTION_CONNECTIONS = 120

# accept エラーの発生を観測する時間 (秒)
# バックオフが 1 秒なので、この間に数回だけエラーが出る
ACCEPT_ERROR_WINDOW_SECONDS = 3

# 観測時間内に許容する accept エラーの件数
# 再試行間隔から決まる件数に、起動直後の 1 件分と余裕を足す
# 即座に再試行する実装では数百件になるため、この上限で区別できる
MAX_ACCEPT_ERRORS_IN_WINDOW = ACCEPT_ERROR_WINDOW_SECONDS * 1000 // ACCEPT_RETRY_DELAY_MS + 3

# 観測時間内に必要な accept の再試行の件数
# 再試行をやめる退行では 1 件しか出ないため、2 件以上を要求する
MIN_ACCEPT_RETRIES_IN_WINDOW = 2

# fd 枯渇で accept が失敗したときに出すエラーの断片
# EMFILE の strerror は macOS と Linux のどちらも "Too many open files"
FD_EXHAUSTION_ERROR_MARKER = "Too many open files"


def _run_with_http_options(
    config_path: Path, host: str, port: int, working_directory: Path
) -> subprocess.CompletedProcess[str]:
    """HTTP オプションを付けて zakuro を起動する

    オプションは設定ファイルではなく引数で渡す。設定ファイルのトップレベルに
    指定する場合と同じ経路を通るが、失敗する値をテスト側で決められる。
    """
    try:
        return subprocess.run(
            [
                get_zakuro_executable_path(),
                "--config",
                str(config_path),
                "--http-host",
                host,
                "--http-port",
                str(port),
            ],
            capture_output=True,
            text=True,
            cwd=working_directory,
            timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired as e:
        # 起動に失敗するはずのケースで終了しない場合はハングの退行とみなす
        pytest.fail(
            f"zakuro が {CONFIG_ERROR_TIMEOUT_SECONDS} 秒以内に終了しなかった: "
            f"host={host} port={port} stderr={e.stderr!r}"
        )


def _assert_start_failed(result: subprocess.CompletedProcess[str], error_marker: str) -> None:
    """HTTP サーバーの起動に失敗したときの終了コードと出力を検証する"""
    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert error_marker in result.stderr, (
        f"失敗の理由が stderr に出ていない: {error_marker!r}\nstderr: {result.stderr!r}"
    )
    assert HTTP_START_FAILED_MARKER in result.stderr, (
        f"起動失敗のメッセージが stderr に出ていない: "
        f"{HTTP_START_FAILED_MARKER!r}\nstderr: {result.stderr!r}"
    )
    # 起動していないサーバーを起動済みとして扱わないこと
    assert HTTP_SERVER_STARTED_MARKER not in result.stderr, (
        f"起動に失敗したのに起動済みのログが出ている: stderr={result.stderr!r}"
    )


def _limit_file_descriptors() -> None:
    """子プロセスのファイルディスクリプタの上限を下げる

    soft と hard の両方を下げるため、zakuro が上限を引き上げようとしても失敗し、
    低い上限のまま起動する。
    """
    _, inherited_hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    limit = min(FD_LIMIT_FOR_EXHAUSTION, inherited_hard)
    resource.setrlimit(resource.RLIMIT_NOFILE, (limit, limit))


def test_http_server_bind_failure_exits_with_error(tmp_path: Path) -> None:
    """ポートが使用中で bind に失敗した場合は、起動失敗として終了する

    bind の失敗は acceptor のコンストラクタの例外で通知される。ここではテスト側で
    ポートを占有し、必ず EADDRINUSE にさせる。
    """
    # テスト側でポートを占有して bind を必ず失敗させる
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as occupied:
        occupied.bind(("127.0.0.1", 0))
        occupied.listen(1)
        port = occupied.getsockname()[1]

        config_path = write_config_object(
            tmp_path, "http_bind_failure.jsonc", {"instances": [dict(VALID_INSTANCE)]}
        )
        result = _run_with_http_options(config_path, "127.0.0.1", port, tmp_path)

    _assert_start_failed(result, HTTP_BIND_ERROR_MARKER)


def test_http_server_bind_failure_on_unassignable_address(tmp_path: Path) -> None:
    """ローカルに割り当てられていないアドレスでも bind の失敗として扱う

    ポート占有 (EADDRINUSE) とは別の errno (EADDRNOTAVAIL) でも、同じく bind の
    失敗として報告されることを確認する。
    """
    config_path = write_config_object(
        tmp_path, "http_bind_unassignable.jsonc", {"instances": [dict(VALID_INSTANCE)]}
    )
    result = _run_with_http_options(config_path, UNASSIGNABLE_HOST, 18080, tmp_path)

    _assert_start_failed(result, HTTP_BIND_ERROR_MARKER)


def test_http_server_resolve_failure_exits_with_error(tmp_path: Path) -> None:
    """解決できないホスト名を指定した場合は、起動失敗として終了する"""
    config_path = write_config_object(
        tmp_path, "http_resolve_failure.jsonc", {"instances": [dict(VALID_INSTANCE)]}
    )
    result = _run_with_http_options(config_path, UNRESOLVABLE_HOST, 18081, tmp_path)

    _assert_start_failed(result, HTTP_RESOLVE_ERROR_MARKER)


def _start_http_server_with_low_fd_limit(
    config_path: Path, port: int, working_directory: Path
) -> tuple[subprocess.Popen[bytes], list[str]]:
    """ファイルディスクリプタの上限を下げた状態で zakuro を起動する

    起動できなかった場合は先に子プロセスを終了させてから失敗させる。fd を枯渇させた
    状態の子プロセスを残すと、ポートと stderr のパイプを保持したままになるためである。
    """
    process = subprocess.Popen(
        [
            get_zakuro_executable_path(),
            "--config",
            str(config_path),
            "--http-host",
            "127.0.0.1",
            "--http-port",
            str(port),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=working_directory,
        preexec_fn=_limit_file_descriptors,
    )
    lines, started = wait_for_stderr_line(process, HTTP_SERVER_STARTED_MARKER, STARTUP_WAIT_SECONDS)
    if not started:
        # terminate_zakuro でパイプを閉じるため、pytest.fail より先に呼ぶ
        stdout, stderr = terminate_zakuro(process)
        pytest.fail(
            f"HTTP サーバーが起動しなかった: stderr={chr(10).join(lines)}{stderr} stdout={stdout}"
        )
    return process, lines


def test_http_server_accept_error_does_not_spin(free_port: int, tmp_path: Path) -> None:
    """fd 枯渇で accept が失敗し続けても、待ってから再試行する

    ファイルディスクリプタの上限を下げた状態で多数の接続を張り、accept を EMFILE で
    失敗させる。即座に再試行すると短時間に大量のエラーが出るため、観測時間内の
    件数でスピンしていないことを判定する。あわせて再試行を続けていることも確認する。
    """
    config_path = write_config_object(
        tmp_path, "http_accept_backoff.jsonc", {"instances": [dict(VALID_INSTANCE)]}
    )
    process, stderr_lines = _start_http_server_with_low_fd_limit(config_path, free_port, tmp_path)
    clients: list[socket.socket] = []
    try:
        # fd を枯渇させて accept を EMFILE で失敗させる
        for _ in range(FD_EXHAUSTION_CONNECTIONS):
            client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client.settimeout(1)
            # 接続できない場合は accept が失敗している状態に近付いている
            with contextlib.suppress(OSError):
                client.connect(("127.0.0.1", free_port))
            clients.append(client)

        # 接続を張り終えるまでに出たログは計測対象から外す。接続処理自体に時間が
        # かかった場合に、その間のエラーを観測時間内の件数と数えないためである
        measured_from = len(stderr_lines)
        time.sleep(ACCEPT_ERROR_WINDOW_SECONDS)
    finally:
        for client in clients:
            client.close()
        stdout, stderr_tail = terminate_zakuro(process)

    # 終了コードは fd 上限を下げた状態では検証しない。上限を下げると libwebrtc の
    # スレッドもファイルディスクリプタを作れなくなり、Stop の不具合とは関係のない
    # 異常終了 (Linux では SIGABRT) になるためである。Stop の検証は
    # test_http_server_stops_cleanly が fd 上限を下げずに行う
    stderr = "\n".join(stderr_lines[measured_from:]) + stderr_tail
    accept_errors = stderr.count(HTTP_ACCEPT_ERROR_MARKER)
    retries = stderr.count(HTTP_ACCEPT_RETRY_MARKER)
    # fd を枯渇させられていない場合は、この検証が成立しないため失敗させる
    assert accept_errors > 0, (
        f"accept が失敗しなかった (fd を枯渇させられていない): stderr={stderr!r} stdout={stdout!r}"
    )
    # EMFILE による失敗であることを固定する
    assert FD_EXHAUSTION_ERROR_MARKER in stderr, (
        f"accept の失敗が fd 枯渇ではない: stderr={stderr!r}"
    )
    assert accept_errors <= MAX_ACCEPT_ERRORS_IN_WINDOW, (
        f"{ACCEPT_ERROR_WINDOW_SECONDS} 秒間に accept エラーが {accept_errors} 件出た "
        f"(待たずに再試行している): stderr={stderr!r}"
    )
    # 再試行をやめる退行では再試行のログが 1 件しか出ない
    assert retries >= MIN_ACCEPT_RETRIES_IN_WINDOW, (
        f"accept の再試行が {retries} 件しか出なかった "
        f"({MIN_ACCEPT_RETRIES_IN_WINDOW} 件以上を期待): stderr={stderr!r}"
    )
