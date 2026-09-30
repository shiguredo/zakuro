"""main.cpp の資源管理の E2E テスト

接続 ID の stats ファイルの書き出し (アトミックな置き換え)・ファイルディスクリプタの
必要数の判定・引数の解析で終了する経路の終了コードを検証する。
設定ファイルの異常入力は test_config_json.py が検証する。
"""

import json
import re
import resource
import shlex
import signal
import subprocess
import time
from collections.abc import Callable
from pathlib import Path

from test_helpers import (
    CONFIG_ERROR_TIMEOUT_SECONDS,
    HTTP_SERVER_STARTED_MARKER,
    STARTUP_WAIT_SECONDS,
    VALID_INSTANCE,
    run_zakuro,
    terminate_zakuro,
    wait_for_stderr_line,
    write_config_object,
)
from zakuro import get_zakuro_executable_path

# stats ファイルの 1 回目の書き出しを待つ時間 (秒)
# stats スレッドは 10 秒ごとに書き出すため、余裕を持って待つ
STATS_WRITE_WAIT_SECONDS = 13

# ファイルディスクリプタの soft limit を昇格したことを示すログの断片
RAISED_FILE_DESCRIPTOR_MARKER = "raised the file descriptor limit:"


def _spawn_zakuro(
    config_path: Path,
    extra_args: list[str],
    tmp_path: Path,
    setup: Callable[[], None] | None = None,
) -> subprocess.Popen[bytes]:
    """zakuro 実バイナリを起動する (テスト内で起動し続ける用途)"""
    args = [get_zakuro_executable_path(), "--config", str(config_path)]
    args.extend(extra_args)
    return subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp_path,
        preexec_fn=setup,
    )


def _file_descriptor_limit(soft: int, hard: int | None = None) -> None:
    """子プロセスでファイルディスクリプタの limit を設定する

    hard を省略した場合は継承した値を使う。hard を下げると setrlimit による
    昇格ができなくなるため、昇格を防ぎたい場合に指定する。
    """
    _, inherited_hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    if hard is None:
        hard = inherited_hard
    resource.setrlimit(resource.RLIMIT_NOFILE, (soft, min(hard, inherited_hard)))


def _wait_for_replacement(stats_path: Path, before_inode: int) -> bool:
    """stats ファイルが置き換わる (inode が変わる) まで待つ"""
    deadline = time.monotonic() + STATS_WRITE_WAIT_SECONDS
    while time.monotonic() < deadline:
        if stats_path.stat().st_ino != before_inode:
            return True
        time.sleep(0.2)
    return False


def test_run_failure_exits_with_nonzero(tmp_path: Path) -> None:
    """Zakuro::Run が失敗したときに main が非ゼロ終了することを確認する

    `--fake-audio-capture` に 0 バイトのファイルを指定すると、WavReader::Load が
    size < 20 で拒否して Zakuro::Run が 1 を返す。この経路は設定ファイルの読み込み
    エラー (main 自身が検出する) ではないため、戻り値の集約を検証できる。
    """
    # 0 バイトのファイルは CLI11 の ExistingFile を通るが、WavReader::Load は拒否する。
    # --no-audio-device を指定すると fake audio の経路に入らないため指定しない
    empty_wav = tmp_path / "empty.wav"
    empty_wav.write_bytes(b"")
    instance = {
        "fake-audio-capture": str(empty_wav),
        "no-video-device": True,
        "sora": {
            "signaling-url": "wss://127.0.0.1:1/signaling",
            "channel-id": "main-resource-run-failure",
            "role": "sendrecv",
        },
    }
    config_path = write_config_object(tmp_path, "run_failure.jsonc", {"instances": [instance]})
    result = run_zakuro(config_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert "failed to load fake audio" in result.stderr, (
        f"fake audio の読み込み失敗が stderr に出ていない: stderr={result.stderr!r}"
    )


def test_valid_config_is_converted_to_arguments(free_port: int, tmp_path: Path) -> None:
    """正しい型の設定が CLI 引数へ正しく変換され、CLI11 の検証を通ることを確認する

    異常系だけを検証していると、正しい設定が CLI11 の検証で弾かれる退行
    (値付きオプションに値が渡らない等) を検出できないため、正常系も検証する。
    接続先は到達しない URL なので、接続前の時点で終了させて出力だけを確認する。
    値付きオプションは種類ごとに 1 つでは足りない (取り違えはキー単位で起きる) ため、
    真偽値・数値・文字列を複数含めて検証する。
    """
    config = {
        "log-level": "error",
        "http-port": free_port,
        "http-host": "127.0.0.1",
        "instance-hatch-rate": 2,
        "instances": [
            {
                "name": "config-json-positive",
                "vcs": 1,
                "framerate": 30,
                "resolution": "VGA",
                # 値付きの真偽値オプションは値トークンが必要
                # 値が渡らないと CLI11 が次の引数を値として食い、検証エラーになる
                "initial-mute-video": True,
                "initial-mute-audio": True,
                "no-video-device": True,
                "no-audio-device": True,
                "sora": {
                    "signaling-url": ["wss://127.0.0.1:1/signaling"],
                    "channel-id": "config-json-positive",
                    "role": "sendrecv",
                    "video": False,
                    "audio": False,
                    "simulcast": True,
                    "simulcast-rid": "rid0",
                    "spotlight": True,
                    "spotlight-number": 1,
                    "data-channel-signaling": False,
                    "ignore-disconnect-websocket": True,
                    "video-bit-rate": 1000,
                },
            }
        ],
    }
    config_path = write_config_object(tmp_path, "valid.jsonc", config)

    # http-port と http-host は設定ファイル側で指定しているため、ここでは追加しない
    process = _spawn_zakuro(config_path, [], tmp_path)
    stderr_lines: list[str] = []
    try:
        # 組み立てたコマンドラインが出力されるまで待つ
        stderr_lines, started = wait_for_stderr_line(
            process, HTTP_SERVER_STARTED_MARKER, STARTUP_WAIT_SECONDS
        )
    finally:
        stdout, stderr_tail = terminate_zakuro(process)

    stderr = "".join(stderr_lines) + stderr_tail
    assert started, f"CLI11 の検証を通らなかった (HTTP サーバーが起動していない): stderr={stderr!r}"

    # 組み立てたコマンドラインの出力後にクラッシュしていないこと
    # テストからは SIGTERM で終了させる。zakuro は SIGTERM を捕捉して終了コード 0 で
    # 終了するが、捕捉する前にシグナルで終了する場合もあるため両方を受け付ける。
    # それ以外の終了コード (SIGABRT 等) は異常として扱う
    assert process.returncode in (0, -signal.SIGTERM), (
        f"クラッシュまたは異常終了した: returncode={process.returncode}\n"
        f"stdout={stdout!r}\nstderr={stderr!r}"
    )

    # 値付きオプションが値とともに展開されていること
    # 引数をトークンに分割して隣接する組で比較する
    # 部分文字列で比較すると "--vcs 1" が "--vcs 10" にも一致してしまう
    command_line = stdout.splitlines()[0] if stdout.splitlines() else ""
    tokens = shlex.split(command_line)
    expected_arguments = [
        "--log-level error",
        f"--http-port {free_port}",
        "--http-host 127.0.0.1",
        "--instance-hatch-rate 2",
        "--name config-json-positive",
        "--vcs 1",
        "--framerate 30",
        "--resolution VGA",
        "--initial-mute-video true",
        "--initial-mute-audio true",
        "--no-video-device",
        "--no-audio-device",
        "--sora-signaling-url wss://127.0.0.1:1/signaling",
        "--sora-channel-id config-json-positive",
        "--sora-role sendrecv",
        "--sora-video false",
        "--sora-audio false",
        "--sora-simulcast true",
        "--sora-simulcast-rid rid0",
        "--sora-spotlight true",
        "--sora-spotlight-number 1",
        "--sora-data-channel-signaling false",
        "--sora-ignore-disconnect-websocket true",
        "--sora-video-bit-rate 1000",
    ]
    for expected in expected_arguments:
        expected_tokens = shlex.split(expected)
        assert any(
            tokens[i : i + len(expected_tokens)] == expected_tokens
            for i in range(len(tokens) - len(expected_tokens) + 1)
        ), f"引数 {expected!r} が組み立てられていない: command_line={command_line!r}"


def _limit_file_size_and_ignore_sigxfsz() -> None:
    """子プロセスでファイルサイズの上限を 1 バイトにし、SIGXFSZ を無視する

    stats の書き出しを必ず失敗させるための設定。SIGXFSZ を無視しないと
    上限を超えた書き込みでプロセスがシグナルにより終了してしまう。
    """
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (1, 1))


def test_stats_write_failure_keeps_previous_file(tmp_path: Path) -> None:
    """stats の書き出しに失敗した場合、出力先の直前の内容が保たれることを確認する

    書き込みを決定的に失敗させるため、ファイルサイズの上限を 1 バイトにした子プロセスで
    起動する。テンポラリファイルへ書き切ってから rename で置き換えるため、失敗時は
    出力先が置き換わらず、テンポラリファイルも残らない。
    出力先を開いて truncate してから書き込む実装では、直前の内容が壊れる。
    """
    stats_path = tmp_path / "stats.json"
    previous = '{"previous":"KEEP-ME"}'
    stats_path.write_text(previous, encoding="utf-8")
    config_path = write_config_object(
        tmp_path, "stats_failure.jsonc", {"instances": [VALID_INSTANCE]}
    )
    process = _spawn_zakuro(
        config_path,
        ["--output-file-connection-id", str(stats_path)],
        tmp_path,
        setup=_limit_file_size_and_ignore_sigxfsz,
    )
    try:
        # stats スレッドは 10 秒ごとに書き出すため、その間隔より長く待つ
        time.sleep(STATS_WRITE_WAIT_SECONDS)
    finally:
        _, stderr = terminate_zakuro(process)

    # 出力先は直前の内容のままであること
    assert stats_path.read_text(encoding="utf-8") == previous, (
        f"出力先の内容が変わっている: {stats_path.read_text(encoding='utf-8')!r}"
    )
    # 不完全なテンポラリファイルを残さないこと
    assert list(tmp_path.glob("stats.json.tmp.*")) == [], "テンポラリファイルが残っている"
    # 想定した失敗経路を通っていること
    assert "Failed to write the stats file" in stderr, (
        f"書き出し失敗のメッセージが出ていない: stderr={stderr!r}"
    )


def test_stats_file_is_valid_json_after_write(tmp_path: Path) -> None:
    """stats ファイルに完全な JSON が書き出され、テンポラリが残らないことを確認する

    書き出しの途中の状態が外部から読まれないこと (アトミック性) は、読み手のタイミングに
    依存するため確率的にしか観測できない。決定的な検証は、書き込みを必ず失敗させて
    「出力先が置き換わらない」ことを確認する
    `test_stats_write_failure_keeps_previous_file` が担う。
    """
    stats_path = tmp_path / "stats.json"
    stats_path.write_text("{}", encoding="utf-8")
    before_inode = stats_path.stat().st_ino
    config_path = write_config_object(tmp_path, "stats.jsonc", {"instances": [VALID_INSTANCE]})
    process = _spawn_zakuro(
        config_path,
        ["--output-file-connection-id", str(stats_path)],
        tmp_path,
    )
    try:
        assert _wait_for_replacement(stats_path, before_inode), (
            "stats ファイルが置き換えられていない"
        )
        # 書き出されたファイルが完全な JSON として読めること
        json.loads(stats_path.read_text(encoding="utf-8"))
        assert list(tmp_path.glob("stats.json.tmp.*")) == [], "テンポラリファイルが残っている"
    finally:
        terminate_zakuro(process)

    # 終了させた後も完全な JSON として読めること
    json.loads(stats_path.read_text(encoding="utf-8"))


def test_stats_write_failure_does_not_create_target(tmp_path: Path) -> None:
    """stats の書き出しに失敗した場合、出力先を作らないことを確認する

    出力先のディレクトリが存在しない場合はテンポラリファイルを開けない。
    このとき出力先もディレクトリも作らない。
    """
    stats_path = tmp_path / "not_exist_dir" / "stats.json"
    config_path = write_config_object(
        tmp_path, "stats_missing_dir.jsonc", {"instances": [VALID_INSTANCE]}
    )
    process = _spawn_zakuro(
        config_path,
        ["--output-file-connection-id", str(stats_path)],
        tmp_path,
    )
    try:
        # 書き出しの間隔 (10 秒) より長く待つ
        time.sleep(STATS_WRITE_WAIT_SECONDS)
        assert not stats_path.exists(), "出力先のファイルが作られている"
        assert not stats_path.parent.exists(), "出力先のディレクトリが作られている"
    finally:
        terminate_zakuro(process)


def test_stats_file_preserves_permissions(tmp_path: Path) -> None:
    """stats ファイルの置き換えでパーミッションが変わらないことを確認する

    出力先のパーミッションをテンポラリファイルへ引き継ぐ。引き継がない実装では
    テンポラリを作るときのモードになり、利用者が設定したパーミッションが変わる。
    テンポラリのモードと異なる値で検証しないと、引き継ぎの有無を区別できない。
    """
    stats_path = tmp_path / "stats.json"
    stats_path.write_text("{}", encoding="utf-8")
    stats_path.chmod(0o644)
    before_inode = stats_path.stat().st_ino
    config_path = write_config_object(
        tmp_path, "stats_permission.jsonc", {"instances": [VALID_INSTANCE]}
    )
    process = _spawn_zakuro(
        config_path,
        ["--output-file-connection-id", str(stats_path)],
        tmp_path,
    )
    try:
        # 置き換えが起きたことを確認してからパーミッションを検査する。
        # 置き換えを待たないと、書き出しが起きなくてもテストが通ってしまう
        assert _wait_for_replacement(stats_path, before_inode), (
            "stats ファイルが置き換えられていない"
        )
        assert stats_path.stat().st_mode & 0o777 == 0o644, (
            f"パーミッションが変わっている: {oct(stats_path.stat().st_mode & 0o777)}"
        )
    finally:
        terminate_zakuro(process)


def test_file_descriptor_limit_is_rejected(tmp_path: Path) -> None:
    """必要数に対してソフトリミットが不足する場合は、起動を拒否することを確認する

    `--vcs` はインスタンスごとのオプションなので、コマンドラインから指定する。
    設定ファイルの `vcs` よりコマンドラインの `--vcs` が優先される。
    ソフトリミットは 1024 に下げ、ハードリミットも 1024 にして setrlimit による
    昇格を防ぐ。必要数 (1000 VC x 5) は 1024 を超えるため必ず拒否される。
    """
    config_path = write_config_object(tmp_path, "fd_limit.jsonc", {"instances": [VALID_INSTANCE]})
    args = [
        get_zakuro_executable_path(),
        "--config",
        str(config_path),
        "--vcs",
        "1000",
    ]

    result = subprocess.run(
        args,
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
        # ソフトリミットとハードリミットを 1024 に下げ、昇格を防ぐ
        preexec_fn=lambda: _file_descriptor_limit(soft=1024, hard=1024),
    )

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    # 必要数と現在の soft / hard limit がメッセージに含まれること
    assert "required=5000 soft=1024 hard=1024" in result.stderr, (
        f"必要数と現在の limit がメッセージに出ていない: stderr={result.stderr!r}"
    )


def test_file_descriptor_limit_is_raised(free_port: int, tmp_path: Path) -> None:
    """ソフトリミットが不足しても昇格できる場合は起動することを確認する

    ソフトリミットだけを 1024 に下げ、ハードリミットは継承した大きな値を残す。
    必要数 (300 VC x 5 = 1500) はソフトリミットを超えるが、setrlimit で昇格できる。
    昇格したことを示すログを確認する。このログは昇格の処理を削除すると出力されないため、
    「エラーが出ずに起動した」だけで通ってしまうことを防ぐ。
    """
    config_path = write_config_object(tmp_path, "fd_raise.jsonc", {"instances": [VALID_INSTANCE]})
    process = _spawn_zakuro(
        config_path,
        [
            "--http-port",
            str(free_port),
            "--http-host",
            "127.0.0.1",
            "--vcs",
            "300",
            "--vcs-hatch-rate",
            "0.1",
        ],
        tmp_path,
        setup=lambda: _file_descriptor_limit(soft=1024),
    )
    try:
        # 昇格のログを同期点にして、FD チェックを通過したことを確認する。
        # このログは昇格の処理を削除すると出力されないため、
        # 「エラーが出ずに起動した」だけでテストが通ることを防ぐ
        stderr_lines, raised = wait_for_stderr_line(
            process, RAISED_FILE_DESCRIPTOR_MARKER, STARTUP_WAIT_SECONDS
        )
    finally:
        stdout, stderr_tail = terminate_zakuro(process)

    stderr = "\n".join(stderr_lines) + stderr_tail
    assert raised, (
        f"FD の昇格が行われなかった (昇格のログが出ていない): stdout={stdout!r} stderr={stderr!r}"
    )
    # 必要数 (300 VC x 5 = 1500) を満たすように昇格していること
    # libwebrtc のログが同じ stderr に割り込むことがあるため、空白を許容して照合する
    assert re.search(r"raised the file descriptor limit: required=\s*1500\b", stderr), (
        f"昇格後の必要数がログに出ていない: stderr={stderr!r}"
    )


def test_file_descriptor_limit_sums_all_instances(tmp_path: Path) -> None:
    """必要 FD 数が全インスタンスの vcs の合計で見積もられることを確認する

    1 インスタンスずつは足りていても、合計すると不足する設定で拒否されることを確認する。
    インスタンス単体の vcs (400) だけを見る実装では 2048 のソフトリミットで起動してしまう。
    """
    instance = dict(VALID_INSTANCE)
    instance["vcs"] = 400
    config_path = write_config_object(
        tmp_path,
        "fd_sum.jsonc",
        {"instances": [instance, dict(instance)]},
    )
    args = [
        get_zakuro_executable_path(),
        "--config",
        str(config_path),
    ]

    result = subprocess.run(
        args,
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
        # 400 VC x 5 = 2000 は 2048 に収まるが、2 インスタンスの合計 4000 は収まらない
        preexec_fn=lambda: _file_descriptor_limit(soft=2048, hard=2048),
    )

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert "required=4000" in result.stderr, (
        f"合計の必要数がメッセージに出ていない: stderr={result.stderr!r}"
    )


def test_parse_args_early_exit_codes(tmp_path: Path) -> None:
    """引数の解析で終了する経路の終了コードを確認する

    `--version` と `--show-video-codec-capability` は何も起動せず 0 で終了し、
    `--help` も 0 で終了する (CLI11 の app.exit が 0 を返す経路)。
    `--openh264` に実在する相対パスを指定した場合と、必須オプションが足りない場合は
    1 で終了する。
    """
    executable = get_zakuro_executable_path()

    # --version は 0 で終了し、バージョン情報を出力する
    result = subprocess.run(
        [executable, "--version"],
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )
    assert result.returncode == 0, (
        f"--version の終了コードが 0 ではない: returncode={result.returncode}\n"
        f"stderr: {result.stderr!r}"
    )
    assert "Zakuro" in result.stdout, f"--version の出力がない: stdout={result.stdout!r}"

    # --show-video-codec-capability は 0 で終了する
    result = subprocess.run(
        [executable, "--show-video-codec-capability"],
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )
    assert result.returncode == 0, (
        f"--show-video-codec-capability の終了コードが 0 ではない: "
        f"returncode={result.returncode}\nstderr={result.stderr!r}"
    )

    # --help は 0 で終了する
    result = subprocess.run(
        [executable, "--help"],
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )
    assert result.returncode == 0, (
        f"--help の終了コードが 0 ではない: returncode={result.returncode}\n"
        f"stderr: {result.stderr!r}"
    )

    # --openh264 に実在する相対パスを指定した場合は 1 で終了する
    # 存在しないパスは CLI11 の ExistingFile で先に弾かれるため、実在するファイルを使う
    relative_library = tmp_path / "relative.so"
    relative_library.write_bytes(b"")
    result = subprocess.run(
        [
            executable,
            "--sora-signaling-url",
            "wss://127.0.0.1:1/signaling",
            "--sora-channel-id",
            "config-json-cli",
            "--sora-role",
            "sendrecv",
            "--openh264",
            "relative.so",
        ],
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )
    assert result.returncode == 1, (
        f"--openh264 の相対パスの終了コードが 1 ではない: "
        f"returncode={result.returncode}\nstderr: {result.stderr!r}"
    )
    assert "--openh264 file path must be absolute path" in result.stderr, (
        f"エラーメッセージが stderr に出ていない: stderr={result.stderr!r}"
    )

    # 必須オプションが無い場合は 1 で終了する
    result = subprocess.run(
        [executable, "--sora-signaling-url", "wss://127.0.0.1:1/signaling"],
        capture_output=True,
        text=True,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
        cwd=tmp_path,
    )
    assert result.returncode == 1, (
        f"必須オプション不足の終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stderr: {result.stderr!r}"
    )
    assert "--sora-channel-id is required" in result.stderr, (
        f"エラーメッセージが stderr に出ていない: stderr={result.stderr!r}"
    )
