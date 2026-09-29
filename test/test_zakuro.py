"""Zakuro の基本的なテスト"""

import pytest

from conftest import SoraConfig, get_deps_versions, get_zakuro_version
from zakuro import Zakuro


# Sora C++ SDK の Websocket::OnClose がログに Boost.Beast の static_string を流しており、
# libwebrtc のログ機構がこれを保持できないため、Linux ではシグナリング URL を複数指定して
# 実 Sora に接続したときに zakuro が SIGABRT する。SDK 側の修正が入るまでの暫定措置として
# xfail にし、macOS など再現しない環境で成功した場合 (xpass) も失敗にしない
@pytest.mark.xfail(
    reason="Sora C++ SDK の Websocket::OnClose のログが原因で Linux では zakuro が SIGABRT するため",
    strict=False,
)
def test_version(sora_config: SoraConfig, free_port: int) -> None:
    """バージョン情報を取得できることを確認"""
    # 期待されるバージョンを取得
    expected_zakuro_version = get_zakuro_version()
    deps = get_deps_versions()

    with Zakuro(
        instances=[sora_config.build_instance(channel_name="version", role="sendrecv", vcs=1)],
        http_port=free_port,
    ) as z:
        version = z.rpc.get_version()

        # バージョン値を検証
        assert version["zakuro"] == expected_zakuro_version
        assert version["sora_cpp_sdk"] == deps["SORA_CPP_SDK_VERSION"]
        # DEPS の WEBRTC_BUILD_VERSION は "m" プレフィックス付きだが、
        # webrtc-build が生成する VERSIONS ファイルには "m" が含まれないため除去して比較
        expected_libwebrtc = deps["WEBRTC_BUILD_VERSION"].removeprefix("m")
        assert version["libwebrtc"] == expected_libwebrtc
        assert version["boost"] == deps["BOOST_VERSION"]
