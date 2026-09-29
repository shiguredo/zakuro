"""Y4MReader と WavReader の異常入力の E2E テスト

`--fake-video-capture` に渡す Y4M と `--fake-audio-capture` に渡す WAV の異常入力で、
プロセスがシグナルで異常終了しないことを検証する。接続先は到達しない URL にして、
メディアの読み出しだけを対象にする。

WAV は `WavReader::Load` の失敗が `Zakuro::Run` のエラーになって終了コードと標準エラー
出力に現れるため、終了コード 1 と `failed to load fake audio` で検証できる。

Y4M の異常なヘッダは `FakeVideoCapturer` が `Failed to Y4MReader::Open` と戻り値を
ログに出すため、エラーとして扱われたことを観測できる。E2E で観測できないのは
プレーン別 stride のコピーの正しさだけである (現行 libwebrtc は `stride_y == width` を
返すため、修正前の一括書き込みでも同じ結果になる。コード上の保証として確認する)。

読み出しを続ける系は `--http-host` / `--http-port` を指定して起動したままにし、
一定時間後に `SIGKILL` で終了させる。終了コードが `SIGKILL` の値であれば、その間は
シグナルで落ちずに動き続けたことになる。
"""

import select
import signal
import struct
import subprocess
import time
from pathlib import Path

import pytest

from test_helpers import (
    CONFIG_ERROR_TIMEOUT_SECONDS,
    HTTP_SERVER_STARTED_MARKER,
    STARTUP_WAIT_SECONDS,
    VALID_INSTANCE,
    wait_for_stderr_line,
    write_config_object,
)
from zakuro import get_zakuro_executable_path

# fake audio の読み込みに失敗したときに出すメッセージの断片
FAKE_AUDIO_ERROR_MARKER = "failed to load fake audio"

# WavReader が読めないチャンクサイズのときに返す値
# fmt チャンクは 16 バイトあるため、data チャンクのサイズ検査 (-10) で拒否される
WAV_CHUNK_SIZE_ERROR_RESULTS = ("result=-10",)

# Y4MReader がヘッダを読んだときに標準出力へ出す断片
# Y4M の読み出しの経路に入ったことを確認する同期点に使う
Y4M_HEADER_MARKER = "YUV4MPEG2 "

# Y4MReader::Open が失敗したときに出すメッセージの断片
Y4M_OPEN_ERROR_MARKER = "Failed to Y4MReader::Open"

# Y4MReader::GetFrame が失敗したときに出すメッセージの断片
Y4M_FRAME_ERROR_MARKER = "Failed to Y4MReader::GetFrame"

# Y4MReader::ReadHeader が異常なヘッダに対して返す値
Y4M_READ_HEADER_ERROR_RESULT = "result=-9"

# WavReader が fmt チャンクの長さ不足で返す値
WAV_FMT_SIZE_ERROR_RESULT = "result=-11"

# WavReader がサンプルレートの範囲外で返す値
WAV_SAMPLE_RATE_ERROR_RESULT = "result=-12"

# 1 フレームが大きすぎる Y4M に対して返す値
Y4M_FRAME_SIZE_ERROR_RESULT = "result=-13"

# 起動したままにしてから SIGKILL で終了させるまでの時間 (秒)
# この間に読み出しが繰り返される
RUN_SECONDS = 4


def _y4m(width: int, height: int, fps_num: int, fps_den: int, frames: int = 8) -> bytes:
    """Y4M のヘッダとフレームを作る

    フレーム数は、フレーム位置が進んで `GetFrame` の除算に到達する程度にしておく。
    """
    header = f"YUV4MPEG2 W{width} H{height} F{fps_num}:{fps_den} Ip A1:1 C420jpeg\n"
    y_size = width * height
    chroma_size = ((width + 1) // 2) * ((height + 1) // 2)
    body = b""
    for i in range(frames):
        body += b"FRAME\n" + bytes([(i * 37 + 1) % 256]) * (y_size + chroma_size * 2)
    return header.encode() + body


def _wav(data_chunk_size: int, fmt_size: int = 16, sample_rate: int = 48000) -> bytes:
    """data チャンクのサイズを指定して WAV を作る

    16bit / 1ch の PCM。data チャンクの中身は 8 バイトだけ入れる。
    `fmt_size` と `sample_rate` を変えると fmt チャンクの長さとサンプルレートを
    指定できる。
    """
    fmt_body = struct.pack(
        "<HHIIHH",
        1,
        1,
        sample_rate & 0xFFFFFFFF,
        (sample_rate * 2) & 0xFFFFFFFF,
        2,
        16,
    )
    # fmt チャンクの長さを 16 未満にする場合は、その長さまで切り詰める
    fmt_body = fmt_body[:fmt_size]
    fmt = b"fmt " + struct.pack("<I", fmt_size) + fmt_body
    data = b"data" + struct.pack("<I", data_chunk_size) + b"\x00\x00" * 4
    riff_size = 4 + len(fmt) + len(data)
    return b"RIFF" + struct.pack("<I", riff_size) + b"WAVE" + fmt + data


def _instance(video: bool = False) -> dict[str, object]:
    """接続先に到達しないインスタンス設定を返す

    `video` が真の場合は `--fake-video-capture` の経路に入るようにする。`no-video-device`
    を指定すると `Zakuro::Run` が capturer を作らないため、Y4M の読み出しが実行されない。
    `no-audio-device` も指定しない。指定すると fake audio の読み込み経路に入らないため、
    WAV の異常入力を検証できない。
    """
    instance = dict(VALID_INSTANCE)
    if video:
        instance.pop("no-video-device", None)
    else:
        instance["no-video-device"] = True
    # 指定すると WAV の読み込み経路に入らない
    instance.pop("no-audio-device", None)
    return instance


def _wait_for_stdout_line(
    process: subprocess.Popen[bytes], marker: str, timeout: int
) -> tuple[list[str], bool]:
    """実行中のプロセスの標準出力を marker を含む行が現れるまで読み続ける

    Y4MReader はヘッダを標準出力に出すため、読み出しが始まったことの確認に使う。
    戻り値は読み出した行のリストと、marker が見つかったかどうか。
    """
    lines: list[str] = []
    assert process.stdout is not None
    stdout = process.stdout
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([stdout], [], [], 0.1)
        if not ready:
            if process.poll() is not None:
                break
            continue
        line = stdout.readline()
        if line == b"":
            return lines, False
        text = line.decode("utf-8", errors="replace").rstrip("\n")
        lines.append(text)
        if marker in text:
            return lines, True
    return lines, False


def _run_to_completion(
    instance: dict[str, object], name: str, tmp_path: Path
) -> subprocess.CompletedProcess[str]:
    """起動したままにせずに zakuro を起動し、終了まで待つ"""
    config_path = write_config_object(tmp_path, name, {"instances": [instance]})
    return subprocess.run(
        [get_zakuro_executable_path(), "--config", str(config_path)],
        capture_output=True,
        text=True,
        cwd=tmp_path,
        timeout=CONFIG_ERROR_TIMEOUT_SECONDS,
    )


def _assert_runs_without_signal(
    instance: dict[str, object],
    name: str,
    tmp_path: Path,
    free_port: int,
    marker: str = HTTP_SERVER_STARTED_MARKER,
    on_stdout: bool = False,
    unexpected_marker: str | None = None,
) -> None:
    """起動したままにして、一定時間シグナルで落ちないことを確認する

    `marker` を同期点にしてから `RUN_SECONDS` 待ち、`SIGKILL` で終了させる。終了コードが
    `SIGKILL` の値であれば、その間は動き続けていたことになる。メディアの読み出しを
    検証する場合は、その読み出しが始まることを示すログを `marker` に渡す。
    `unexpected_marker` を渡すと、その間に標準エラー出力へその文字列が出ていないことも
    確認する。
    """
    config_path = write_config_object(tmp_path, name, {"instances": [instance]})
    process = subprocess.Popen(
        [
            get_zakuro_executable_path(),
            "--config",
            str(config_path),
            "--http-host",
            "127.0.0.1",
            "--http-port",
            str(free_port),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp_path,
    )
    stdout = b""
    stderr = b""
    try:
        if on_stdout:
            lines, started = _wait_for_stdout_line(process, marker, STARTUP_WAIT_SECONDS)
        else:
            lines, started = wait_for_stderr_line(process, marker, STARTUP_WAIT_SECONDS)
        assert started, (
            f"{name}: 同期点 {marker!r} を {STARTUP_WAIT_SECONDS} 秒以内に検出できなかった: "
            f"output={chr(10).join(lines)!r}"
        )
        time.sleep(RUN_SECONDS)
        # SIGKILL は捕捉できないため、この時点で生きていれば終了コードは SIGKILL になる
        process.kill()
        stdout, stderr = process.communicate(timeout=CONFIG_ERROR_TIMEOUT_SECONDS)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    assert process.returncode == -signal.SIGKILL, (
        f"{name}: 動き続けるはずが {RUN_SECONDS} 秒以内に終了した: "
        f"returncode={process.returncode}\n"
        f"stdout={stdout.decode('utf-8', 'replace')!r}\n"
        f"stderr={stderr.decode('utf-8', 'replace')!r}"
    )

    if unexpected_marker is not None:
        stderr_text = stderr.decode("utf-8", "replace")
        assert unexpected_marker not in stderr_text, (
            f"{name}: 想定外のログが出ている: {unexpected_marker!r}\nstderr={stderr_text!r}"
        )


def _assert_video_rejected(
    instance: dict[str, object],
    name: str,
    tmp_path: Path,
    free_port: int,
    expected_result: str,
) -> None:
    """Y4M の読み出しがエラーになり、プロセスがシグナルで落ちないことを確認する

    HTTP サーバーの起動を待ってから `Y4MReader` の失敗ログを待ち、`SIGKILL` で終了
    させる。終了コードが `SIGKILL` の値であれば、その間は動き続けていたことになる。
    """
    config_path = write_config_object(tmp_path, name, {"instances": [instance]})
    process = subprocess.Popen(
        [
            get_zakuro_executable_path(),
            "--config",
            str(config_path),
            "--http-host",
            "127.0.0.1",
            "--http-port",
            str(free_port),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp_path,
    )
    stderr_lines: list[str] = []
    try:
        _, started = wait_for_stderr_line(process, HTTP_SERVER_STARTED_MARKER, STARTUP_WAIT_SECONDS)
        assert started, (
            f"{HTTP_SERVER_STARTED_MARKER!r} を {STARTUP_WAIT_SECONDS} 秒以内に検出できなかった"
        )
        # リーダーの失敗ログを待つ。失敗をログに出さない実装では待ち時間を満了する
        stderr_lines, rejected = wait_for_stderr_line(
            process, Y4M_OPEN_ERROR_MARKER, STARTUP_WAIT_SECONDS
        )
        process.kill()
        stdout, stderr_tail = process.communicate(timeout=CONFIG_ERROR_TIMEOUT_SECONDS)
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()

    stderr = "\n".join(stderr_lines) + stderr_tail.decode("utf-8", "replace")
    assert rejected, (
        f"{name}: Y4M の失敗が stderr に出ていない: stderr={stderr!r}\n"
        f"stdout={stdout.decode('utf-8', 'replace')!r}"
    )
    assert expected_result in stderr, (
        f"{name}: 拒否の理由が想定と違う: expected={expected_result!r}\nstderr={stderr!r}"
    )
    assert process.returncode == -signal.SIGKILL, (
        f"{name}: 読み出しの間にシグナルで落ちた: returncode={process.returncode}\n"
        f"stderr={stderr!r}"
    )


@pytest.mark.parametrize(
    ("width", "height", "fps_num", "fps_den", "expected_result", "frames"),
    [
        # 分母が 0 の場合は GetFrame の除算が 0 除算になる (x86_64 では SIGFPE)
        (4, 2, 30, 0, Y4M_READ_HEADER_ERROR_RESULT, 8),
        # 負の寸法は GetSize が負値になり I420Buffer の生成で落ちる
        (-4, 2, 30, 1, Y4M_READ_HEADER_ERROR_RESULT, 8),
        (4, -2, 30, 1, Y4M_READ_HEADER_ERROR_RESULT, 8),
        # 極端に大きい寸法は GetSize の int 計算がオーバーフローする
        # フレームを作るとメモリを大量に確保するため、ヘッダだけを渡す
        (46341, 46341, 30, 1, Y4M_FRAME_SIZE_ERROR_RESULT, 0),
    ],
    ids=["fps-den-0", "width-negative", "height-negative", "size-too-large"],
)
def test_y4m_with_invalid_header_is_rejected(
    width: int,
    height: int,
    fps_num: int,
    fps_den: int,
    expected_result: str,
    frames: int,
    free_port: int,
    tmp_path: Path,
) -> None:
    """異常なヘッダの Y4M は読み出されず、エラーとして扱われる

    `Y4MReader::ReadHeader` が検証して `Open` がエラーを返すため、`GetFrame` には
    到達しない。`FakeVideoCapturer` が失敗をログに出すので、E2E でもエラーとして
    扱われたことを確認できる。
    """
    y4m_path = tmp_path / f"invalid_{width}_{height}_{fps_num}_{fps_den}.y4m"
    y4m_path.write_bytes(_y4m(width, height, fps_num, fps_den, frames))
    instance = _instance(video=True)
    instance["fake-video-capture"] = str(y4m_path)

    # 映像の読み出しに失敗しても Zakuro::Run はエラーにしないため、終了コードではなく
    # ログで確認する
    _assert_video_rejected(
        instance,
        f"y4m_invalid_{width}_{height}.jsonc",
        tmp_path,
        free_port,
        expected_result,
    )


def test_y4m_with_odd_size_is_read_without_crash(free_port: int, tmp_path: Path) -> None:
    """奇数の幅と高さを持つ Y4M を読み出しても異常終了しない

    現行 libwebrtc は `stride_y == width` / `stride_u == stride_v == (width + 1) / 2` を
    返すため、プレーン別 stride のコピーが正しいことはこの E2E では観測できない
    (修正前の一括書き込みでも同じ結果になる)。異常終了しないことだけを検証する。
    """
    y4m_path = tmp_path / "odd_size.y4m"
    y4m_path.write_bytes(_y4m(5, 3, 30, 1))
    instance = _instance(video=True)
    instance["fake-video-capture"] = str(y4m_path)

    _assert_runs_without_signal(
        instance,
        "y4m_odd_size.jsonc",
        tmp_path,
        free_port,
        Y4M_HEADER_MARKER,
        on_stdout=True,
        unexpected_marker=Y4M_FRAME_ERROR_MARKER,
    )


@pytest.mark.parametrize(
    ("chunk_size", "name", "reason"),
    [
        # uint32_t のまま csize + 8 を計算すると 2^32 でラップして 7 になり素通りする
        (0xFFFFFFFF, "wav_huge_chunk.jsonc", "0xFFFFFFFF の data チャンク"),
        # 同じくラップで 0 になり、チャンクの進みが 0 になる
        (0xFFFFFFF8, "wav_huge_chunk_f8.jsonc", "0xFFFFFFF8 の data チャンク"),
    ],
    ids=["0xffffffff", "0xfffffff8"],
)
def test_wav_data_chunk_size_is_rejected(
    chunk_size: int, name: str, reason: str, tmp_path: Path
) -> None:
    """data チャンクのサイズが巨大な WAV は未捕捉例外にならずエラーになる

    修正前はチャンクサイズを signed int で合成していたため負値になり、サイズ検査を
    すり抜けて `data.reserve` が `std::length_error` を投げて SIGABRT で落ちていた。
    """
    wav_path = tmp_path / f"huge_chunk_{chunk_size:x}.wav"
    wav_path.write_bytes(_wav(chunk_size))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    result = _run_to_completion(instance, name, tmp_path)

    assert result.returncode == 1, (
        f"{reason}: 終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert FAKE_AUDIO_ERROR_MARKER in result.stderr, (
        f"{reason}: fake audio の読み込み失敗が stderr に出ていない: stderr={result.stderr!r}"
    )
    # data チャンクのサイズ検査で拒否されたことを固定する
    assert any(r in result.stderr for r in WAV_CHUNK_SIZE_ERROR_RESULTS), (
        f"{reason}: 拒否の理由がチャンクサイズではない: stderr={result.stderr!r}"
    )


@pytest.mark.parametrize(
    ("fmt_size", "sample_rate", "expected_result"),
    [
        # fmt チャンクが 16 バイト未満だと PCM 形式を読み切れない
        (15, 48000, WAV_FMT_SIZE_ERROR_RESULT),
        (14, 48000, WAV_FMT_SIZE_ERROR_RESULT),
        # サンプルレートは 0 より大きく 1000000 以下に限る
        (16, 0, WAV_SAMPLE_RATE_ERROR_RESULT),
        (16, 1000001, WAV_SAMPLE_RATE_ERROR_RESULT),
        # MSB が立つ値は signed で合成すると負値になる
        (16, 0xFFFFFFFF, WAV_SAMPLE_RATE_ERROR_RESULT),
    ],
    ids=[
        "fmt-size-15",
        "fmt-size-14",
        "sample-rate-0",
        "sample-rate-1000001",
        "sample-rate-0xffffffff",
    ],
)
def test_wav_with_invalid_format_is_rejected(
    fmt_size: int, sample_rate: int, expected_result: str, tmp_path: Path
) -> None:
    """fmt チャンクが異常な WAV は未捕捉例外にならずエラーになる"""
    wav_path = tmp_path / f"bad_fmt_{fmt_size}_{sample_rate}.wav"
    wav_path.write_bytes(_wav(8, fmt_size, sample_rate))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    result = _run_to_completion(instance, f"wav_bad_fmt_{fmt_size}_{sample_rate}.jsonc", tmp_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert FAKE_AUDIO_ERROR_MARKER in result.stderr, (
        f"fake audio の読み込み失敗が stderr に出ていない: stderr={result.stderr!r}"
    )
    assert expected_result in result.stderr, (
        f"拒否の理由が想定と違う: expected={expected_result!r}\nstderr={result.stderr!r}"
    )


@pytest.mark.parametrize(
    ("fmt_size", "sample_rate"),
    [(16, 1), (16, 1000000)],
    ids=["sample-rate-1", "sample-rate-1000000"],
)
def test_wav_with_boundary_format_is_accepted(
    fmt_size: int, sample_rate: int, free_port: int, tmp_path: Path
) -> None:
    """サンプルレートの検査の境界にある値は受け付けられる (判定が過剰でないこと)"""
    wav_path = tmp_path / f"ok_fmt_{fmt_size}_{sample_rate}.wav"
    wav_path.write_bytes(_wav(8, fmt_size, sample_rate))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    _assert_runs_without_signal(
        instance, f"wav_ok_fmt_{fmt_size}_{sample_rate}.jsonc", tmp_path, free_port
    )


def test_valid_wav_is_accepted(free_port: int, tmp_path: Path) -> None:
    """正しい WAV は受け付けられる (異常入力の判定が過剰でないこと)"""
    wav_path = tmp_path / "ok.wav"
    wav_path.write_bytes(_wav(8))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    _assert_runs_without_signal(instance, "wav_ok.jsonc", tmp_path, free_port)


def test_missing_y4m_file_is_reported(tmp_path: Path) -> None:
    """存在しない Y4M を指定した場合は CLI11 がエラーにする"""
    instance = _instance()
    instance["fake-video-capture"] = str(tmp_path / "no_such_file.y4m")

    result = _run_to_completion(instance, "y4m_missing.jsonc", tmp_path)

    assert result.returncode != 0, (
        f"存在しない Y4M が受け付けられた: returncode={result.returncode}\nstderr={result.stderr!r}"
    )
