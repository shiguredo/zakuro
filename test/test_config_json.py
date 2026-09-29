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
    DATA_CHANNELS_ERROR_MARKER,
    VALID_INSTANCE,
    VALUE_TYPE_ERROR_MARKER,
    run_zakuro,
    write_config,
    write_config_object,
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
    config_path = write_config_object(tmp_path, "cli_validation.jsonc", config)
    result = run_zakuro(config_path)

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

    解析に失敗すると `Zakuro::Run` が 2 を返し、`main` がその戻り値を終了コードに反映する。
    """
    instance = dict(VALID_INSTANCE)
    instance["sora"] = dict(VALID_INSTANCE["sora"])
    instance["sora"]["data-channels"] = data_channels
    config_path = write_config_object(tmp_path, name, {"instances": [instance]})
    result = run_zakuro(config_path)

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
