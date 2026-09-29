"""Y4MReader と WavReader の異常入力の E2E テスト

`--fake-video-capture` に渡す Y4M と `--fake-audio-capture` に渡す WAV の異常入力で、
プロセスがシグナルで異常終了せず、エラーとして扱われることを検証する。
接続先は到達しない URL にして、メディアの読み出しだけを対象にする。

読み出しを続ける系は `--http-host` / `--http-port` を指定して起動したままにし、
一定時間後に `SIGKILL` で終了させる。終了コードが `SIGKILL` の値であれば、その間は
シグナルで落ちずに動き続けたことになる。`duration` による正常終了は、メディアの
読み出しとは別の abort があるため (issues/0077)、この判定には使わない。
"""

import signal
import struct
import subprocess
import time
from pathlib import Path

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

# 起動したままにしてから SIGKILL で終了させるまでの時間 (秒)
# この間に読み出しが繰り返される
RUN_SECONDS = 4


def _y4m(w: int, h: int, fps_num: int, fps_den: int, frames: int = 8) -> bytes:
    """Y4M のヘッダとフレームを作る

    フレームごとに違う値にして、プレーンのコピーを取り違えた場合に検出できるようにする。
    フレーム数は、フレーム位置が進んで `GetFrame` の除算に到達する程度にしておく。
    """
    header = f"YUV4MPEG2 W{w} H{h} F{fps_num}:{fps_den} Ip A1:1 C420jpeg\n"
    y_size = w * h
    chroma_size = ((w + 1) // 2) * ((h + 1) // 2)
    body = b""
    for i in range(frames):
        body += b"FRAME\n" + bytes([(i * 37 + 1) % 256]) * (y_size + chroma_size * 2)
    return header.encode() + body


def _wav(data_chunk_size: int) -> bytes:
    """data チャンクのサイズを指定して WAV を作る

    16bit / 1ch / 48000Hz の PCM。data チャンクの中身は 8 バイトだけ入れる。
    """
    fmt = b"fmt " + struct.pack("<I", 16) + struct.pack("<HHIIHH", 1, 1, 48000, 96000, 2, 16)
    data = b"data" + struct.pack("<I", data_chunk_size) + b"\x00\x00" * 4
    riff_size = 4 + len(fmt) + len(data)
    return b"RIFF" + struct.pack("<I", riff_size) + b"WAVE" + fmt + data


def _instance() -> dict[str, object]:
    """接続先に到達しないインスタンス設定を返す

    `no-audio-device` は指定しない。指定すると fake audio の読み込み経路に入らないため、
    WAV の異常入力を検証できない。
    """
    instance = dict(VALID_INSTANCE)
    instance["no-video-device"] = True
    # 指定すると WAV の読み込み経路に入らない
    instance.pop("no-audio-device", None)
    return instance


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
    instance: dict[str, object], name: str, tmp_path: Path, free_port: int
) -> None:
    """起動したままにして、一定時間シグナルで落ちないことを確認する

    HTTP サーバーの起動ログを同期点にしてから `RUN_SECONDS` 待ち、`SIGKILL` で終了
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
    stdout = b""
    stderr = b""
    try:
        lines, started = wait_for_stderr_line(
            process, HTTP_SERVER_STARTED_MARKER, STARTUP_WAIT_SECONDS
        )
        assert started, f"{name}: HTTP サーバーが起動しなかった: stderr={chr(10).join(lines)!r}"
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


def test_y4m_with_zero_fps_denominator_does_not_crash(free_port: int, tmp_path: Path) -> None:
    """F フィールドの分母が 0 の Y4M は 0 除算にならず異常終了しない

    修正前は `GetFrame` の `fps_num_ / (1000 * fps_den_)` で 0 除算になり SIGFPE で
    落ちていた。修正後は `Y4MReader::Open` がエラーを返し、映像の読み出しを行わない。
    """
    y4m_path = tmp_path / "zero_den.y4m"
    y4m_path.write_bytes(_y4m(4, 2, 30, 0))
    instance = _instance()
    instance["fake-video-capture"] = str(y4m_path)

    _assert_runs_without_signal(instance, "y4m_zero_den.jsonc", tmp_path, free_port)


def test_y4m_with_odd_size_is_read_without_crash(free_port: int, tmp_path: Path) -> None:
    """奇数の幅と高さを持つ Y4M を読み出しても異常終了しない

    プレーンの幅が stride と一致しない場合の行単位コピーを通す。読み出しは繰り返される
    ため、コピーで範囲外を触ればシグナルで落ちる。
    """
    y4m_path = tmp_path / "odd_size.y4m"
    y4m_path.write_bytes(_y4m(5, 3, 30, 1))
    instance = _instance()
    instance["fake-video-capture"] = str(y4m_path)

    _assert_runs_without_signal(instance, "y4m_odd_size.jsonc", tmp_path, free_port)


def test_wav_data_chunk_size_0xffffffff_is_rejected(tmp_path: Path) -> None:
    """data チャンクのサイズが 0xFFFFFFFF の WAV は未捕捉例外にならずエラーになる

    修正前はチャンクサイズを signed int で合成していたため負値になり、サイズ検査を
    すり抜けて `data.reserve` が `std::length_error` を投げて SIGABRT で落ちていた。
    """
    wav_path = tmp_path / "huge_chunk.wav"
    wav_path.write_bytes(_wav(0xFFFFFFFF))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    result = _run_to_completion(instance, "wav_huge_chunk.jsonc", tmp_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert FAKE_AUDIO_ERROR_MARKER in result.stderr, (
        f"fake audio の読み込み失敗が stderr に出ていない: stderr={result.stderr!r}"
    )


def test_wav_data_chunk_size_0xfffffff8_is_rejected(tmp_path: Path) -> None:
    """data チャンクのサイズが 0xFFFFFFF8 の WAV もエラーになる

    修正前はサイズ検査をすり抜けてチャンクの進みが 0 になり、無限ループになり得た。
    """
    wav_path = tmp_path / "huge_chunk_f8.wav"
    wav_path.write_bytes(_wav(0xFFFFFFF8))
    instance = _instance()
    instance["fake-audio-capture"] = str(wav_path)

    result = _run_to_completion(instance, "wav_huge_chunk_f8.jsonc", tmp_path)

    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert FAKE_AUDIO_ERROR_MARKER in result.stderr, (
        f"fake audio の読み込み失敗が stderr に出ていない: stderr={result.stderr!r}"
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
