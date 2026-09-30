"""`--duration` を指定したときの終了経路のテスト

シグナリングに到達できない設定で `--duration` の時間切れにすると、シナリオは
Disconnect の op を実行する。このとき接続は既に切れており `VirtualClient` の
signaling は破棄済みなので、Disconnect は「既に閉じている」経路に入る。
この経路で終了コードが 0 以外になったり SIGABRT したりする退行を検出する。

実 Sora を必要としないため、`sora_config` フィクスチャは使わない。
"""

from typing import Any

import pytest

from zakuro import Zakuro

# 到達できないシグナリング URL。接続は成立しないが zakuro は起動して
# HTTP サーバーを立てるため、`--duration` の時間切れによる終了経路を検証できる
UNREACHABLE_SIGNALING_URL = "wss://127.0.0.1:1/signaling"


def _unreachable_instance(
    duration: int,
    *,
    sora_options: dict[str, Any] | None = None,
    vcs: int | None = None,
) -> dict[str, Any]:
    """到達できないシグナリング URL を持つインスタンス設定を返す

    Args:
        duration: `--duration` に指定する秒数
        sora_options: `sora` 配下に追加するオプション
        vcs: 仮想クライアント数 (指定したときだけ設定に含める)
    """
    instance: dict[str, Any] = {
        "sora": {
            "signaling-url": UNREACHABLE_SIGNALING_URL,
            "channel-id": "duration-exit-test",
            "role": "sendrecv",
            **(sora_options or {}),
        },
        "duration": duration,
        "no-video-device": True,
        "no-audio-device": True,
    }
    if vcs is not None:
        instance["vcs"] = vcs
    return instance


def test_duration_exits_cleanly_without_connection(free_port: int) -> None:
    """接続できないまま `--duration` の時間切れになった場合も正常終了する

    シナリオの Disconnect は結果を受け取らない呼び出しであり、
    `VirtualClient::Close` に空の `std::function` が渡る。これを呼ぶと
    `std::bad_function_call` が未捕捉になって SIGABRT で落ちる退行があった。
    """
    with Zakuro(instances=[_unreachable_instance(2)], http_port=free_port) as z:
        returncode = z.wait(timeout=30)

    assert returncode == 0, (
        f"終了コードが 0 ではない: returncode={returncode} stderr={z.stderr_output!r}"
    )


@pytest.mark.parametrize(
    ("name", "sora_options", "vcs"),
    [
        # 複数の仮想クライアントで同時に Disconnect の経路に入る
        ("vcs2", {}, 2),
        # DataChannel シグナリングは 2026.2.1 が直した切断経路を通る
        ("data-channel-signaling", {"data-channel-signaling": True}, None),
    ],
    ids=["vcs2", "data-channel-signaling"],
)
def test_duration_exits_cleanly_on_other_paths(
    name: str, sora_options: dict[str, Any], vcs: int | None, free_port: int
) -> None:
    """`--duration` の終了経路が他の設定でも正常終了する"""
    instance = _unreachable_instance(2, sora_options=sora_options, vcs=vcs)

    with Zakuro(instances=[instance], http_port=free_port) as z:
        returncode = z.wait(timeout=30)

    assert returncode == 0, (
        f"{name}: 終了コードが 0 ではない: returncode={returncode} stderr={z.stderr_output!r}"
    )
