"""--config で渡す JSONC 設定ファイルの異常入力の E2E テスト

想定外の JSON 入力 (型不一致・フィールド欠落・不正な JSON・数値の範囲外) を渡しても、
未捕捉例外でプロセスが落ちず、英語のエラーメッセージを出してエラー終了することを確認する。

終了コードの期待値は経路によって異なる。

- 設定ファイルの読み込みと引数の組み立てで `main` が検出した設定エラーは 1
- 組み立てた引数を CLI11 が検証で弾いた場合は CLI11 の終了コード (105 など)
- DataChannels の解析エラーは `Zakuro::Run` の戻り値が `main` に伝わらないため 0

いずれの場合もシグナルによる強制終了 (負値) は未捕捉例外の退行として失敗させる。
"""

import json
import select
import shlex
import signal
import subprocess
import time
from pathlib import Path
from typing import Any

import pytest

from zakuro import get_zakuro_executable_path

# 設定ファイルを読み込むだけで終了することを期待する待ち時間 (秒)
# 終了しない場合はハングの退行とみなして失敗させる
CONFIG_ERROR_TIMEOUT_SECONDS = 10

# 正常な設定で起動が継続することを確認するときに待つ時間 (秒)
# 接続は成功しないため、コマンドラインの出力と HTTP サーバーの起動を
# 確認できるだけの時間だけ待つ
STARTUP_WAIT_SECONDS = 3

# HTTP サーバーが起動したことを示すログの断片
# CLI11 の検証はコマンドラインの出力後に実行されるため、
# このログを同期点にして検証が完了したことを確認する
HTTP_SERVER_STARTED_MARKER = "HTTP server started on"

# 設定ファイルの読み込みに失敗したときのエラーメッセージの断片
CONFIG_ERROR_MARKER = "failed to load config file"

# DataChannels の解析に失敗したときのエラーメッセージの断片
# 実際の出力は "[<name>] failed to parse DataChannels" であり、
# インスタンス名は設定によって変わるため共通部分だけを検査する
DATA_CHANNELS_ERROR_MARKER = "failed to parse DataChannels"

# 値の型が想定と異なるときのエラーメッセージの断片
VALUE_TYPE_ERROR_MARKER = "has an unexpected value type"

# フラグオプションの値が真偽値でないときのエラーメッセージの断片
BOOLEAN_ERROR_MARKER = "must be a boolean"

# DataChannels の解析まで到達させるための有効なインスタンス設定
# 実際には接続しないため、到達しない signaling URL を入れておく
# 値はキーごとに文字列・真偽値・数値・ネストしたオブジェクトと型が異なるため Any を使う
VALID_INSTANCE: dict[str, Any] = {
    "no-video-device": True,
    "no-audio-device": True,
    "sora": {
        "signaling-url": "wss://127.0.0.1:1/signaling",
        "channel-id": "config-json-test",
        "role": "sendrecv",
    },
}


def _write_config(tmp_path: Path, name: str, content: str) -> Path:
    """設定ファイルを書き出す"""
    path = tmp_path / name
    path.write_text(content, encoding="utf-8")
    return path


def _write_config_object(tmp_path: Path, name: str, obj: object) -> Path:
    """オブジェクトを JSONC として書き出す"""
    return _write_config(tmp_path, name, json.dumps(obj, ensure_ascii=False, indent=2))


def _run_zakuro(config_path: Path) -> subprocess.CompletedProcess[str]:
    """zakuro 実バイナリを設定ファイル付きで実行する"""
    args = [get_zakuro_executable_path(), "--config", str(config_path)]
    try:
        return subprocess.run(
            args, capture_output=True, text=True, timeout=CONFIG_ERROR_TIMEOUT_SECONDS
        )
    except subprocess.TimeoutExpired:
        pytest.fail(
            f"zakuro が {CONFIG_ERROR_TIMEOUT_SECONDS} 秒以内に終了しなかった: config={config_path.name}"
        )


def _assert_no_signal_exit(result: subprocess.CompletedProcess[str]) -> None:
    """シグナルで強制終了していないことを検証する

    終了コードが負値の場合はシグナルによって強制終了されたことを意味する。
    未捕捉例外による SIGABRT はここで検出する。
    macOS と Linux の既定のハンドラでは SIGABRT のメッセージが stderr に出ないことが
    あるため、終了コードで判定する。
    """
    assert result.returncode >= 0, (
        f"シグナルで強制終了した: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )


def _read_first_line(process: subprocess.Popen[bytes], timeout: int) -> str | None:
    """実行中のプロセスの標準出力から 1 行を、待ち時間の上限つきで読み出す

    行が読み出せるまで待ち、上限を超えた場合は None を返す。
    固定時間の待機にすると、低速な環境で出力を取りこぼすため。

    パイプはバイナリモードで開く。テキストモードのファイルオブジェクトは
    内部で先読みするため、select で読み出し可能かを判定できなくなる。
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        # 読み出せるデータがあるかを確認してから読む
        # readline() を直接呼ぶと、出力が無い場合に上限を超えて待ち続ける
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if not ready:
            continue
        line = process.stdout.readline()
        if line == b"":
            # 標準出力が閉じられた (プロセスが終了した) 場合は待たない
            return None
        return line.decode("utf-8", errors="replace").rstrip("\n")
    return None


def _wait_for_stderr_line(
    process: subprocess.Popen[bytes], marker: str, timeout: int
) -> tuple[list[str], bool]:
    """実行中のプロセスの標準エラー出力を marker を含む行が現れるまで読み続ける

    戻り値は読み出した行のリストと、marker が見つかったかどうか。
    上限時間を超えた場合は、その時点までに読み出した行と False を返す。
    marker を同期点にすることで、CLI11 の検証が完了したことを確認できる。
    """
    lines: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stderr], [], [], 0.1)
        if not ready:
            # プロセスが終了した場合は読み出すものが無くなる
            if process.poll() is not None:
                break
            continue
        line = process.stderr.readline()
        if line == b"":
            break
        text = line.decode("utf-8", errors="replace").rstrip("\n")
        lines.append(text)
        if marker in text:
            return lines, True
    return lines, False


def _assert_config_error(result: subprocess.CompletedProcess[str], marker: str) -> None:
    """`main` が検出した設定エラーで、例外で落ちずに終了コード 1 で終了したことを検証する

    設定ファイルの読み込みと引数の組み立てで `main` がエラーを検出した場合は 1 を返す。
    この経路では組み立てたコマンドラインの出力より前に終了するため stdout は空になる。
    """
    _assert_no_signal_exit(result)
    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert marker in result.stderr, (
        f"エラーメッセージが stderr に出ていない: {marker!r}\nstderr: {result.stderr!r}"
    )
    assert result.stdout == "", f"エラー時に stdout へ出力されている: stdout: {result.stdout!r}"


@pytest.mark.parametrize(
    ("name", "content", "marker"),
    [
        # JSON として壊れている場合はロードの段階でエラーになる
        ("broken_syntax.jsonc", "{ invalid", CONFIG_ERROR_MARKER),
        # ルートがオブジェクトでない場合は as_object() が例外を投げていた
        ("root_array.jsonc", "[]", "config file must be a JSON object"),
        # instances キーが無い場合は設定エラーになる
        ("instances_missing.jsonc", "{}", "instances キーがありません。"),
        # instances が空配列の場合は設定エラーになる
        (
            "instances_empty.jsonc",
            '{"instances": []}',
            "instances の下に設定がありません。",
        ),
        # instances が配列でない場合は as_array() が例外を投げていた
        (
            "instances_not_array.jsonc",
            '{"instances": "not-an-array"}',
            "instances must be an array",
        ),
        # instance の要素がオブジェクトでない場合は as_object() が例外を投げていた
        ("instance_not_object.jsonc", '{"instances": [1]}', "instance must be an object"),
        # instance-num が数値でない場合は value_to<int> が例外を投げていた
        (
            "instance_num_not_number.jsonc",
            '{"instances": [{"instance-num": "1"}]}',
            "instance-num must be a number",
        ),
        # instance-num が整数でない場合は value_to<int> が例外を投げていた
        (
            "instance_num_not_integer.jsonc",
            '{"instances": [{"instance-num": 1.5}]}',
            "instance-num must be an integer in range",
        ),
        # instance-num が int の範囲外の場合は value_to<int> が例外を投げていた
        (
            "instance_num_out_of_range.jsonc",
            '{"instances": [{"instance-num": 1e30}]}',
            "instance-num must be an integer in range",
        ),
        # instance-num が 0 以下の場合はインスタンスが 1 つも起動しないため設定エラーにする
        (
            "instance_num_not_positive.jsonc",
            '{"instances": [{"instance-num": 0}]}',
            "instance-num must be positive",
        ),
        # instance-num が上限を超える場合はメモリを大量に確保するため設定エラーにする
        (
            "instance_num_too_large.jsonc",
            '{"instances": [{"instance-num": 1001}]}',
            "instance-num must be 1000 or less",
        ),
        # sora がオブジェクトでない場合は as_object() が例外を投げていた
        ("sora_not_object.jsonc", '{"instances": [{"sora": []}]}', "sora must be an object"),
        # signaling-url が文字列でも配列でもない場合は例外を投げていた
        (
            "signaling_url_not_string.jsonc",
            '{"instances": [{"sora": {"signaling-url": 1}}]}',
            "sora.signaling-url must be string or string[]",
        ),
        # signaling-url の配列要素が文字列でない場合も設定エラーにする
        # 型を検査しないと空文字列に潰れて設定ミスに気付けない
        (
            "signaling_url_element_not_string.jsonc",
            '{"instances": [{"sora": {"signaling-url": [{"url": "wss://example.com"}]}}]}',
            "sora.signaling-url must be string or string[]",
        ),
        # signaling-url が空配列の場合は値の無い引数を組み立ててしまうため設定エラーにする
        (
            "signaling_url_empty.jsonc",
            '{"instances": [{"sora": {"signaling-url": []}}]}',
            "sora.signaling-url must not be empty",
        ),
        # インスタンス直下の文字列オプションがオブジェクトの場合は設定エラーにする
        # 型を検査しないと空文字列に潰れて設定ミスに気付けない
        (
            "instance_string_option_object.jsonc",
            '{"instances": [{"name": {"value": "test"}}]}',
            VALUE_TYPE_ERROR_MARKER,
        ),
        # インスタンス直下の数値オプションが文字列の場合は設定エラーにする
        (
            "instance_number_option_string.jsonc",
            '{"instances": [{"vcs": "1"}]}',
            VALUE_TYPE_ERROR_MARKER,
        ),
        # インスタンス直下の数値オプションが配列の場合は設定エラーにする
        (
            "instance_number_option_array.jsonc",
            '{"instances": [{"framerate": [30]}]}',
            VALUE_TYPE_ERROR_MARKER,
        ),
        # フラグオプションが真偽値でない場合は設定エラーにする
        # 型を検査しないとフラグが無言で落ちる
        (
            "instance_flag_string.jsonc",
            '{"instances": [{"no-video-device": "true"}]}',
            BOOLEAN_ERROR_MARKER,
        ),
        # sora 配下の真偽値オプションがオブジェクトの場合は設定エラーにする
        (
            "sora_flag_object.jsonc",
            '{"instances": [{"sora": {"video": {"enabled": true}}}]}',
            VALUE_TYPE_ERROR_MARKER,
        ),
        # sora 配下の数値オプションが文字列の場合は設定エラーにする
        (
            "sora_number_option_string.jsonc",
            '{"instances": [{"sora": {"video-bit-rate": "1000"}}]}',
            VALUE_TYPE_ERROR_MARKER,
        ),
        # トップレベルの共通オプションがオブジェクトの場合は設定エラーにする
        # 型を検査しないとキーごと無視され、設定ミスが無言で通ってしまう
        (
            "common_option_object.jsonc",
            '{"log-level": {"level": "info"}, "instances": [{"sora": {}}]}',
            "log-level must be a string, a number, or a boolean",
        ),
        # トップレベルの共通オプションが配列の場合も同じく設定エラーにする
        (
            "common_option_array.jsonc",
            '{"http-port": [8080], "instances": [{"sora": {}}]}',
            "http-port must be a string, a number, or a boolean",
        ),
    ],
    ids=[
        "broken-syntax",
        "root-array",
        "instances-missing",
        "instances-empty",
        "instances-not-array",
        "instance-not-object",
        "instance-num-not-number",
        "instance-num-not-integer",
        "instance-num-out-of-range",
        "instance-num-not-positive",
        "instance-num-too-large",
        "sora-not-object",
        "signaling-url-not-string",
        "signaling-url-element-not-string",
        "signaling-url-empty",
        "instance-string-option-object",
        "instance-number-option-string",
        "instance-number-option-array",
        "instance-flag-string",
        "sora-flag-object",
        "sora-number-option-string",
        "common-option-object",
        "common-option-array",
    ],
)
def test_config_parse_error_exits_without_crash(
    name: str, content: str, marker: str, tmp_path: Path
) -> None:
    """不正な設定ファイルでは、例外で落ちずに終了コード 1 で終了する"""
    config_path = _write_config(tmp_path, name, content)
    result = _run_zakuro(config_path)
    _assert_config_error(result, marker)


@pytest.mark.parametrize("extension", ["txt", "yaml"], ids=["txt", "yaml"])
def test_config_unsupported_extension(extension: str, tmp_path: Path) -> None:
    """拡張子が .json / .jsonc 以外の場合は、例外で落ちずに終了コード 1 で終了する"""
    config_path = _write_config(tmp_path, f"config.{extension}", "{}")
    result = _run_zakuro(config_path)
    _assert_config_error(result, CONFIG_ERROR_MARKER)


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
    config_path = _write_config_object(tmp_path, "valid.jsonc", config)

    args = [get_zakuro_executable_path(), "--config", str(config_path)]
    process = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp_path,
    )
    stderr_lines: list[str] = []
    try:
        # 組み立てたコマンドラインが出力されるまで待つ
        # 固定時間の待機にすると低速な環境で取りこぼすため、行が読めるまで待つ
        command_line = _read_first_line(process, STARTUP_WAIT_SECONDS)

        # HTTP サーバーの起動ログを同期点にして、CLI11 の検証が完了するまで待つ。
        # コマンドラインの出力は引数の組み立て直後で、CLI11 の検証はその後に実行される。
        # 出力を読んだだけで終了させると、検証を通ったことを確認できない
        stderr_lines, started = _wait_for_stderr_line(
            process, HTTP_SERVER_STARTED_MARKER, STARTUP_WAIT_SECONDS
        )
    finally:
        process.terminate()
        try:
            stdout_bytes, stderr_bytes = process.communicate(timeout=10)
        except subprocess.TimeoutExpired:
            # 終了しない場合はプロセスを残さないようにしてから失敗させる
            process.kill()
            stdout_bytes, stderr_bytes = process.communicate()
            pytest.fail(f"zakuro が終了しなかった: stdout={stdout_bytes!r} stderr={stderr_bytes!r}")

    # 読み出し済みの行と、終了時に残っていた出力を合わせる
    stdout = stdout_bytes.decode("utf-8", errors="replace")
    stderr = "".join(stderr_lines) + stderr_bytes.decode("utf-8", errors="replace")
    assert command_line is not None, (
        f"組み立てたコマンドラインが出力されていない: stdout={stdout!r} stderr={stderr!r}"
    )

    # CLI11 の検証を通って HTTP サーバーが起動したこと
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


def test_cli_validation_error_exits_with_cli11_code(tmp_path: Path) -> None:
    """CLI11 の検証で弾かれる値は、CLI11 の終了コードで終了する

    設定ファイルの値が型としては正しくても、CLI11 の検証 (列挙値・範囲) で
    弾かれる場合がある。この経路は `main` ではなく CLI11 が終了させるため、
    終了コードは 1 ではなく CLI11 が返す値になる。
    """
    config = {
        "instances": [
            {
                "no-video-device": True,
                "no-audio-device": True,
                "sora": {
                    "signaling-url": "wss://127.0.0.1:1/signaling",
                    "channel-id": "config-json-cli11",
                    "role": "sendrecv",
                    # 列挙値ではないため CLI11 の IsMember で弾かれる
                    "video-codec-type": "UNKNOWN",
                },
            }
        ]
    }
    config_path = _write_config_object(tmp_path, "cli_validation.jsonc", config)
    result = _run_zakuro(config_path)

    _assert_no_signal_exit(result)
    # CLI11 は検証エラーで 105 (ValidationError) を返す
    assert result.returncode == 105, (
        f"CLI11 の終了コード 105 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert "Run with --help for more information." in result.stderr, (
        f"CLI11 のエラーメッセージが stderr に出ていない: stderr={result.stderr!r}"
    )


@pytest.mark.parametrize(
    ("name", "data_channels"),
    [
        # data-channels が配列でない場合は型検査で false を返す (例外ではない)
        ("data_channels_not_array.jsonc", {"label": "test"}),
        # label が無い場合はエラーになる
        ("data_channels_missing_label.jsonc", [{"direction": "sendrecv"}]),
        # label が文字列でない場合は型検査で false を返す
        ("data_channels_bad_label.jsonc", [{"label": 1, "direction": "sendrecv"}]),
        # direction が無い場合はエラーになる
        ("data_channels_missing_direction.jsonc", [{"label": "test"}]),
        # direction が文字列でない場合は型検査で false を返す
        ("data_channels_bad_direction.jsonc", [{"label": "test", "direction": 1}]),
        # ordered が真偽値でない場合は value_to<bool> が例外を投げていた
        (
            "data_channels_bad_ordered.jsonc",
            [{"label": "test", "direction": "sendrecv", "ordered": "yes"}],
        ),
        # interval が数値でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_bad_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": "500"}],
        ),
        # interval が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": 1.5}],
        ),
        # size-min が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_size_min.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-min": 48.5}],
        ),
        # size-max が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_size_max.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-max": 48.5}],
        ),
        # max_packet_life_time が数値でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_bad_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": "10"}],
        ),
        # max_packet_life_time が整数でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_not_integer_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": 1.5}],
        ),
        # max_packet_life_time が int32_t の範囲外の場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_out_of_range_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": 1e30}],
        ),
        # max_retransmits が数値でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_bad_max_retransmits.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_retransmits": "3"}],
        ),
        # max_retransmits が整数でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_not_integer_max_retransmits.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_retransmits": 1.5}],
        ),
        # protocol が文字列でない場合は value_to<std::string> が例外を投げていた
        (
            "data_channels_bad_protocol.jsonc",
            [{"label": "test", "direction": "sendrecv", "protocol": 1}],
        ),
        # compress が真偽値でない場合は value_to<bool> が例外を投げていた
        (
            "data_channels_bad_compress.jsonc",
            [{"label": "test", "direction": "sendrecv", "compress": "true"}],
        ),
    ],
    ids=[
        "data-channels-not-array",
        "label-missing",
        "label-not-string",
        "direction-missing",
        "direction-not-string",
        "ordered-not-bool",
        "interval-not-number",
        "interval-not-integer",
        "size-min-not-integer",
        "size-max-not-integer",
        "max-packet-life-time-not-number",
        "max-packet-life-time-not-integer",
        "max-packet-life-time-out-of-range",
        "max-retransmits-not-number",
        "max-retransmits-not-integer",
        "protocol-not-string",
        "compress-not-bool",
    ],
)
def test_data_channels_type_error_exits_without_crash(
    name: str, data_channels: object, tmp_path: Path
) -> None:
    """DataChannels の設定が想定と異なる場合も、例外で落ちずにエラー終了する

    解析に失敗した場合は `Zakuro::Run` の戻り値が `main` に伝わらないため終了コードは 0 になる。
    この期待値は Run の戻り値を main が反映するようになった時点で見直す必要がある。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["sora"]["data-channels"] = data_channels
    config_path = _write_config_object(tmp_path, name, {"instances": [instance]})
    result = _run_zakuro(config_path)

    # DataChannels の解析は接続前に行われるため、解析に失敗した場合は接続処理に進まない
    _assert_no_signal_exit(result)
    assert result.returncode == 0, (
        f"終了コードが 0 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert DATA_CHANNELS_ERROR_MARKER in result.stderr, (
        f"エラーメッセージが stderr に出ていない: "
        f"{DATA_CHANNELS_ERROR_MARKER!r}\nstderr: {result.stderr!r}"
    )
