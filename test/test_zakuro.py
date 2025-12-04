"""Zakuro の基本的なテスト"""

from conftest import SoraConfig, get_deps_versions, get_zakuro_version
from zakuro import Zakuro


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
