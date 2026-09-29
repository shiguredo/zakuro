"""--config で渡す JSONC 設定ファイルの異常入力の E2E テスト

想定外の JSON 入力 (型不一致・フィールド欠落・不正な JSON・数値の範囲外) を渡しても、
未捕捉例外でプロセスが落ちず、英語のエラーメッセージを出してエラー終了することを確認する。

終了コードの期待値は経路によって異なる。

- 設定ファイルの読み込みと引数の組み立てで `main` が検出した設定エラーは 1
- 組み立てた引数を CLI11 が検証で弾いた場合は CLI11 の終了コード (105 など)
- `Zakuro::Run` の失敗 (DataChannels の解析失敗など) は `main` が戻り値を集約して 1

いずれの場合もシグナルによる強制終了 (負値) は未捕捉例外の退行として失敗させる。
main.cpp の資源管理 (stats ファイルの書き出し・ファイルディスクリプタの必要数・
引数の解析で終了する経路の終了コード) は test_main_resource.py が検証する。
"""

import subprocess
from pathlib import Path

import pytest

from test_helpers import (
    BOOLEAN_ERROR_MARKER,
    CONFIG_ERROR_MARKER,
    CONFIG_ERROR_TIMEOUT_SECONDS,
    DATA_CHANNELS_ERROR_MARKER,
    DATA_CHANNELS_MESSAGE_PREFIX,
    DATA_CHANNELS_SENDING_MARKER,
    STARTUP_WAIT_SECONDS,
    VALID_INSTANCE,
    VALUE_TYPE_ERROR_MARKER,
    run_zakuro,
    terminate_zakuro,
    wait_for_stderr_line,
    write_config,
    write_config_object,
)
from zakuro import get_zakuro_executable_path


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


def _assert_stdout_is_cli_dump_only(stdout: str) -> None:
    """標準出力が組み立てたコマンドラインの出力だけであることを検証する

    zakuro は設定ファイルから組み立てた引数をインスタンスごとに標準出力へ 1 行ずつ
    出力する。行番号を出すデバッグ出力が標準出力に戻ると、この行以外の行が現れるため、
    その退行を検出する。インスタンスが 1 件の設定でのみ使える。
    """
    lines = stdout.splitlines()
    assert lines, f"組み立てたコマンドラインの出力が無い: stdout={stdout!r}"
    assert lines[0].startswith(get_zakuro_executable_path()), (
        f"組み立てたコマンドラインの出力ではない: stdout={stdout!r}"
    )
    assert len(lines) == 1, f"コマンドラインの出力以外が標準出力に出ている: stdout={stdout!r}"


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
    config_path = write_config(tmp_path, name, content)
    result = run_zakuro(config_path)
    _assert_config_error(result, marker)


@pytest.mark.parametrize("extension", ["txt", "yaml"], ids=["txt", "yaml"])
def test_config_unsupported_extension(extension: str, tmp_path: Path) -> None:
    """拡張子が .json / .jsonc 以外の場合は、例外で落ちずに終了コード 1 で終了する"""
    config_path = write_config(tmp_path, f"config.{extension}", "{}")
    result = run_zakuro(config_path)
    _assert_config_error(result, CONFIG_ERROR_MARKER)


def _assert_cli11_validation_error(result: subprocess.CompletedProcess[str]) -> None:
    """CLI11 の検証で弾かれたときの終了コードとメッセージを検証する

    この経路は `main` ではなく CLI11 が終了させるため、終了コードは 1 ではなく
    CLI11 が返す 105 (ValidationError) になる。
    """
    _assert_no_signal_exit(result)
    assert result.returncode == 105, (
        f"CLI11 の終了コード 105 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert "Run with --help for more information." in result.stderr, (
        f"CLI11 のエラーメッセージが stderr に出ていない: stderr={result.stderr!r}"
    )


def test_cli_validation_error_exits_with_cli11_code(tmp_path: Path) -> None:
    """CLI11 の検証で弾かれる値は、CLI11 の終了コードで終了する

    設定ファイルの値が型としては正しくても、CLI11 の検証 (列挙値・範囲) で
    弾かれる場合がある。ここでは sora 配下の列挙値オプションで確認する。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    # 列挙値ではないため CLI11 の IsMember で弾かれる
    instance["sora"]["video-codec-type"] = "UNKNOWN"
    config_path = write_config_object(
        tmp_path, "cli_validation_video_codec_type.jsonc", {"instances": [instance]}
    )
    result = run_zakuro(config_path)

    _assert_cli11_validation_error(result)
    assert "video-codec-type" in result.stderr, (
        f"弾かれた項目がエラーメッセージに出ていない: stderr={result.stderr!r}"
    )


def test_unsupported_scenario_exits_with_cli11_code(tmp_path: Path) -> None:
    """許容値以外の scenario は CLI11 の検証で弾かれる

    ここで弾かれることが、`Zakuro::Run` に届く scenario が許容値だけになる前提を
    守っている。`Zakuro::Run` 側の検証は CLI 以外で設定を構築した場合の防御である。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["scenario"] = "unknown"
    config_path = write_config_object(
        tmp_path, "cli_validation_scenario.jsonc", {"instances": [instance]}
    )
    result = run_zakuro(config_path)

    _assert_cli11_validation_error(result)
    assert "scenario" in result.stderr, (
        f"弾かれた項目がエラーメッセージに出ていない: stderr={result.stderr!r}"
    )


@pytest.mark.parametrize(
    ("name", "data_channels", "reason"),
    [
        # data-channels が配列でない場合は配列であることを求めて失敗する
        ("data_channels_not_array.jsonc", {"label": "test"}, "data channels must be an array"),
        # 配列の要素がオブジェクトでない場合は要素の型として失敗する
        (
            "data_channels_not_object.jsonc",
            ["test"],
            "data channel must be an object",
        ),
        # label が無い場合は欠落として失敗する
        (
            "data_channels_missing_label.jsonc",
            [{"direction": "sendrecv"}],
            "label is missing",
        ),
        # label が文字列でない場合は文字列であることを求めて失敗する
        (
            "data_channels_bad_label.jsonc",
            [{"label": 1, "direction": "sendrecv"}],
            "label must be a string",
        ),
        # direction が無い場合は欠落として失敗する
        (
            "data_channels_missing_direction.jsonc",
            [{"label": "test"}],
            "direction is missing",
        ),
        # direction が文字列でない場合は型検査で false を返す
        (
            "data_channels_bad_direction.jsonc",
            [{"label": "test", "direction": 1}],
            "direction must be a string",
        ),
        # ordered が真偽値でない場合は value_to<bool> が例外を投げていた
        (
            "data_channels_bad_ordered.jsonc",
            [{"label": "test", "direction": "sendrecv", "ordered": "yes"}],
            "ordered must be a boolean",
        ),
        # interval が数値でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_bad_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": "500"}],
            "interval must be a number",
        ),
        # interval が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": 1.5}],
            "interval must be an integer",
        ),
        # interval が 0 の場合は送信間隔として成立しないため拒否する
        (
            "data_channels_zero_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": 0}],
            "interval must be positive",
        ),
        # interval が負の値の場合も 0 と同じく拒否する
        (
            "data_channels_negative_interval.jsonc",
            [{"label": "test", "direction": "sendrecv", "interval": -1}],
            "interval must be positive",
        ),
        # size-min が数値でない場合は数値であることを求めて失敗する
        (
            "data_channels_bad_size_min.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-min": "48"}],
            "size-min must be a number",
        ),
        # size-min が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_size_min.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-min": 48.5}],
            "size-min must be an integer",
        ),
        # size-min が下限 (48) 未満の場合は範囲外として失敗する
        (
            "data_channels_below_lower_bound_size_min.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-min": 1}],
            "size-min out of range: 1",
        ),
        # size-min が上限 (256000) を超える場合も範囲外として失敗する
        (
            "data_channels_above_upper_bound_size_min.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-min": 256001}],
            "size-min out of range: 256001",
        ),
        # 別名キーの size_min でも型検査の失敗を size-min の名前で報告する
        (
            "data_channels_bad_size_min_alias.jsonc",
            [{"label": "test", "direction": "sendrecv", "size_min": "48"}],
            "size-min must be a number",
        ),
        # 正規キーと別名キーを同時に指定した場合は正規キーの値を採用する
        (
            "data_channels_canonical_size_min_wins.jsonc",
            [
                {
                    "label": "test",
                    "direction": "sendrecv",
                    "size-min": 1,
                    "size_min": 48,
                }
            ],
            "size-min out of range: 1",
        ),
        # size-max が数値でない場合は数値であることを求めて失敗する
        (
            "data_channels_bad_size_max.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-max": "48"}],
            "size-max must be a number",
        ),
        # size-max が整数でない場合は value_to<int> が例外を投げていた
        (
            "data_channels_not_integer_size_max.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-max": 48.5}],
            "size-max must be an integer",
        ),
        # size-max が下限 (48) 未満の場合は範囲外として失敗する
        (
            "data_channels_below_lower_bound_size_max.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-max": 1}],
            "size-max out of range: 1",
        ),
        # size-max が上限 (256000) を超える場合も範囲外として失敗する
        (
            "data_channels_above_upper_bound_size_max.jsonc",
            [{"label": "test", "direction": "sendrecv", "size-max": 256001}],
            "size-max out of range: 256001",
        ),
        # 別名キーの size_max でも型検査の失敗を size-max の名前で報告する
        (
            "data_channels_bad_size_max_alias.jsonc",
            [{"label": "test", "direction": "sendrecv", "size_max": "48"}],
            "size-max must be a number",
        ),
        # 正規キーと別名キーを同時に指定した場合は正規キーの値を採用する
        (
            "data_channels_canonical_size_max_wins.jsonc",
            [
                {
                    "label": "test",
                    "direction": "sendrecv",
                    "size-max": 1,
                    "size_max": 256000,
                }
            ],
            "size-max out of range: 1",
        ),
        # max_packet_life_time が数値でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_bad_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": "10"}],
            "max_packet_life_time must be a number",
        ),
        # max_packet_life_time が整数でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_not_integer_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": 1.5}],
            "max_packet_life_time must be an integer",
        ),
        # max_packet_life_time が int32_t の範囲外の場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_out_of_range_max_packet_life_time.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_packet_life_time": 1e30}],
            "max_packet_life_time must be an integer",
        ),
        # max_retransmits が数値でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_bad_max_retransmits.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_retransmits": "3"}],
            "max_retransmits must be a number",
        ),
        # max_retransmits が整数でない場合は value_to<int32_t> が例外を投げていた
        (
            "data_channels_not_integer_max_retransmits.jsonc",
            [{"label": "test", "direction": "sendrecv", "max_retransmits": 1.5}],
            "max_retransmits must be an integer",
        ),
        # protocol が文字列でない場合は value_to<std::string> が例外を投げていた
        (
            "data_channels_bad_protocol.jsonc",
            [{"label": "test", "direction": "sendrecv", "protocol": 1}],
            "protocol must be a string",
        ),
        # compress が真偽値でない場合は value_to<bool> が例外を投げていた
        (
            "data_channels_bad_compress.jsonc",
            [{"label": "test", "direction": "sendrecv", "compress": "true"}],
            "compress must be a boolean",
        ),
    ],
    ids=[
        "data-channels-not-array",
        "data-channel-not-object",
        "label-missing",
        "label-not-string",
        "direction-missing",
        "direction-not-string",
        "ordered-not-bool",
        "interval-not-number",
        "interval-not-integer",
        "interval-zero",
        "interval-negative",
        "size-min-not-number",
        "size-min-not-integer",
        "size-min-below-lower-bound",
        "size-min-above-upper-bound",
        "size-min-alias-not-number",
        "size-min-canonical-wins",
        "size-max-not-number",
        "size-max-not-integer",
        "size-max-below-lower-bound",
        "size-max-above-upper-bound",
        "size-max-alias-not-number",
        "size-max-canonical-wins",
        "max-packet-life-time-not-number",
        "max-packet-life-time-not-integer",
        "max-packet-life-time-out-of-range",
        "max-retransmits-not-number",
        "max-retransmits-not-integer",
        "protocol-not-string",
        "compress-not-bool",
    ],
)
def test_data_channels_error_exits_without_crash(
    name: str, data_channels: object, reason: str, tmp_path: Path
) -> None:
    """DataChannels の設定が想定と異なる場合は、例外で落ちずに理由付きでエラー終了する

    解析に失敗すると `Zakuro::Run` が 2 を返し、`main` がその戻り値を集約して 1 で終了する。
    理由は `ParseDataChannels` がログ経路 (RTC_LOG) に出力する。ログレベルの指定は既定の
    ままにして、利用者が実際に受け取る出力で理由が確認できることを検証する。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["sora"]["data-channels"] = data_channels
    config_path = write_config_object(tmp_path, name, {"instances": [instance]})
    # 正規キー優先を検証するケースは、優先順が崩れると解析を通過して接続を試み続ける。
    # その場合はタイムアウトで失敗させるため、他のケースより待ち時間を短くする
    timeout_seconds = CONFIG_ERROR_TIMEOUT_SECONDS if "canonical-wins" not in name else 3
    result = run_zakuro(config_path, timeout_seconds)

    # DataChannels の解析は接続前に行われるため、解析に失敗した場合は接続処理に進まない
    _assert_no_signal_exit(result)
    assert result.returncode == 1, (
        f"終了コードが 1 ではない: returncode={result.returncode}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )
    assert DATA_CHANNELS_ERROR_MARKER in result.stderr, (
        f"エラーメッセージが stderr に出ていない: "
        f"{DATA_CHANNELS_ERROR_MARKER!r}\nstderr: {result.stderr!r}"
    )
    # 解析の失敗理由はログだけに出す。標準出力に行番号の出力が戻る退行を検出する
    _assert_stdout_is_cli_dump_only(result.stdout)
    message = f"{DATA_CHANNELS_MESSAGE_PREFIX} {reason}"
    assert message in result.stderr, (
        f"失敗の理由がログに出ていない: {reason!r}\n"
        f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
    )


def test_valid_data_channels_are_accepted(tmp_path: Path) -> None:
    """有効な data-channels は拒否されず、解析を通過して送信が始まる

    異常系だけを検証していると「常に失敗を返す」退行を検出できないため、境界値を含む
    有効な設定が受理されることを確認する。接続先は到達しない URL なので、解析の後に
    始まる DataChannel の送信を同期点にして、解析の通過を確認する。
    """
    # 省略した場合 (既定値) と、指定した場合の両方を受理することを確認する。size-min /
    # size-max は正規キー、size_min / size_max は別名キーで、それぞれ境界値の 48 と
    # 256000 を受理する。ordered など任意キーも有効値を受理する。受理した個々の値は
    # 接続後にしか観測できないため、ここでは解析が通過したことだけを確認する。
    # 別名キーが実際に読まれることは、異常系の size-min-alias-not-number /
    # size-max-alias-not-number が担う
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["sora"]["data-channels"] = [
        {"label": "default", "direction": "sendrecv"},
        {
            "label": "canonical",
            "direction": "sendrecv",
            "size-min": 48,
            "size-max": 256000,
        },
        {
            "label": "alias",
            "direction": "sendrecv",
            "interval": 1000,
            "size_min": 48,
            "size_max": 256000,
            "ordered": True,
            "max_packet_life_time": 10,
            "max_retransmits": 3,
            "protocol": "test",
            "compress": True,
        },
    ]
    config_path = write_config_object(
        tmp_path, "data_channels_valid.jsonc", {"instances": [instance]}
    )

    process = subprocess.Popen(
        [get_zakuro_executable_path(), "--config", str(config_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=tmp_path,
    )
    stderr_lines: list[str] = []
    try:
        # DataChannel の送信が始まるまで待つ
        stderr_lines, sent = wait_for_stderr_line(
            process, DATA_CHANNELS_SENDING_MARKER, STARTUP_WAIT_SECONDS
        )
    finally:
        stdout, stderr_tail = terminate_zakuro(process)

    stderr = "\n".join(stderr_lines) + stderr_tail
    # 解析に失敗した場合は送信が始まる前に終了する
    assert sent, f"有効な data-channels が使われなかった: stderr={stderr!r}"
    assert DATA_CHANNELS_ERROR_MARKER not in stderr, (
        f"有効な data-channels が拒否された: stderr={stderr!r}"
    )
    # 標準出力は組み立てたコマンドラインの 1 行だけである (行番号の出力が戻る退行を検出する)
    _assert_stdout_is_cli_dump_only(stdout)
