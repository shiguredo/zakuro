"""Hatchling build hook for including platform-specific binaries."""

from __future__ import annotations

import platform
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version: str, build_data: dict) -> None:
        system = platform.system().lower()
        machine = platform.machine().lower()

        if system == "darwin" and machine in ("arm64", "aarch64"):
            target = "macos_arm64"
            lib_name = "libduckdb.dylib"
        elif system == "linux" and machine == "x86_64":
            target = self._detect_ubuntu_target()
            lib_name = "libduckdb.so"
        else:
            raise RuntimeError(f"Unsupported platform: {system}_{machine}")

        base_dir = Path(self.root) / "_build" / target / "release" / "zakuro"
        binary_path = base_dir / "zakuro"
        lib_path = base_dir / lib_name

        if not binary_path.exists():
            raise RuntimeError(
                f"Binary not found: {binary_path}\n"
                f"Please build first: python3 run.py build {target}"
            )

        if not lib_path.exists():
            raise RuntimeError(
                f"Shared library not found: {lib_path}\n"
                f"Please build first: python3 run.py build {target}"
            )

        # バイナリを scripts (bin/) に配置 - CLI として実行可能に
        build_data["shared_scripts"] = {
            str(binary_path): "zakuro",
        }

        # 共有ライブラリをパッケージに含める
        build_data["force_include"] = {
            str(lib_path): f"zakuro/{lib_name}",
        }

        # プラットフォームタグを自動推論
        build_data["infer_tag"] = True

    def _detect_ubuntu_target(self) -> str:
        """Ubuntu バージョンを検出して適切なターゲットを返す"""
        try:
            with open("/etc/os-release") as f:
                content = f.read()
                if "24.04" in content:
                    return "ubuntu-24.04_x86_64"
                elif "22.04" in content:
                    return "ubuntu-22.04_x86_64"
        except FileNotFoundError:
            pass

        # デフォルトは ubuntu-24.04
        return "ubuntu-24.04_x86_64"
