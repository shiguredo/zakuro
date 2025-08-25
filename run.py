import argparse
import glob
import logging
import multiprocessing
import os
import shlex
import shutil
import tarfile
from typing import List, Optional

from buildbase import (
    WebrtcInfo,
    add_path,
    build_sora,
    build_webrtc,
    cd,
    cmake_path,
    cmd,
    cmdcap,
    download,
    enum_all_files,
    get_sora_info,
    get_webrtc_info,
    install_blend2d_official,
    install_cli11,
    install_cmake,
    install_duckdb,
    install_llvm,
    install_openh264,
    install_sora_and_deps,
    install_webrtc,
    mkdir_p,
    read_version_file,
    rm_rf,
)

logging.basicConfig(level=logging.DEBUG)


def get_common_cmake_args(install_dir, platform, webrtc_info: WebrtcInfo):
    # クロスコンパイルの設定。
    # 本来は toolchain ファイルに書く内容
    if platform in ("ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64"):
        return [
            f"-DCMAKE_C_COMPILER={webrtc_info.clang_dir}/bin/clang",
            f"-DCMAKE_CXX_COMPILER={webrtc_info.clang_dir}/bin/clang++",
            "-DCMAKE_CXX_FLAGS="
            + " ".join(
                [
                    "-D_LIBCPP_ABI_NAMESPACE=Cr",
                    "-D_LIBCPP_ABI_VERSION=2",
                    "-D_LIBCPP_DISABLE_AVAILABILITY",
                    "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE",
                    "-nostdinc++",
                    f"-isystem{webrtc_info.libcxx_dir}/include",
                ]
            ),
        ]
    elif platform == "macos_arm64":
        sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
        return [
            "-DCMAKE_SYSTEM_PROCESSOR=arm64",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
            "-DCMAKE_C_COMPILER=clang",
            "-DCMAKE_C_COMPILER_TARGET=aarch64-apple-darwin",
            "-DCMAKE_CXX_COMPILER=clang++",
            "-DCMAKE_CXX_COMPILER_TARGET=aarch64-apple-darwin",
            f"-DCMAKE_SYSROOT={sysroot}",
        ]
    else:
        raise Exception(f"Unsupported platform: {platform}")


BASE_DIR = os.path.abspath(os.path.dirname(__file__))


def install_deps(
    platform: str,
    source_dir: str,
    build_dir: str,
    install_dir: str,
    debug: bool,
    local_webrtc_build_dir: Optional[str],
    local_webrtc_build_args: List[str],
    local_sora_cpp_sdk_dir: Optional[str],
    local_sora_cpp_sdk_args: List[str],
):
    with cd(BASE_DIR):
        deps = read_version_file("DEPS")

        # WebRTC
        if local_webrtc_build_dir is None:
            install_webrtc_args = {
                "version": deps["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": platform,
            }

            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": platform,
                "local_webrtc_build_dir": local_webrtc_build_dir,
                "local_webrtc_build_args": local_webrtc_build_args,
                "debug": debug,
            }

            build_webrtc(**build_webrtc_args)

        webrtc_info = get_webrtc_info(platform, local_webrtc_build_dir, install_dir, debug)

        if (
            platform in ("ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64")
            and local_webrtc_build_dir is None
        ):
            webrtc_version = read_version_file(webrtc_info.version_file)

            # LLVM
            tools_url = webrtc_version["WEBRTC_SRC_TOOLS_URL"]
            tools_commit = webrtc_version["WEBRTC_SRC_TOOLS_COMMIT"]
            libcxx_url = webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_URL"]
            libcxx_commit = webrtc_version["WEBRTC_SRC_THIRD_PARTY_LIBCXX_SRC_COMMIT"]
            buildtools_url = webrtc_version["WEBRTC_SRC_BUILDTOOLS_URL"]
            buildtools_commit = webrtc_version["WEBRTC_SRC_BUILDTOOLS_COMMIT"]
            install_llvm_args = {
                "version": f"{tools_url}.{tools_commit}."
                f"{libcxx_url}.{libcxx_commit}."
                f"{buildtools_url}.{buildtools_commit}",
                "version_file": os.path.join(install_dir, "llvm.version"),
                "install_dir": install_dir,
                "tools_url": tools_url,
                "tools_commit": tools_commit,
                "libcxx_url": libcxx_url,
                "libcxx_commit": libcxx_commit,
                "buildtools_url": buildtools_url,
                "buildtools_commit": buildtools_commit,
            }
            install_llvm(**install_llvm_args)

        # CMake
        install_cmake_args = {
            "version": deps["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
            "ext": "tar.gz",
        }
        if platform in ("ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64"):
            install_cmake_args["platform"] = "linux-x86_64"
        elif platform == "macos_arm64":
            install_cmake_args["platform"] = "macos-universal"
        install_cmake(**install_cmake_args)
        if platform == "macos_arm64":
            add_path(os.path.join(install_dir, "cmake", "CMake.app", "Contents", "bin"))
        else:
            add_path(os.path.join(install_dir, "cmake", "bin"))

        # Sora C++ SDK
        if local_sora_cpp_sdk_dir is None:
            install_sora_and_deps(
                deps["SORA_CPP_SDK_VERSION"],
                deps["BOOST_VERSION"],
                platform,
                source_dir,
                install_dir,
            )
        else:
            build_sora(
                platform,
                local_sora_cpp_sdk_dir,
                local_sora_cpp_sdk_args,
                debug,
                local_webrtc_build_dir,
            )

        # CLI11
        install_cli11_args = {
            "version": deps["CLI11_VERSION"],
            "version_file": os.path.join(install_dir, "cli11.version"),
            "install_dir": install_dir,
        }
        install_cli11(**install_cli11_args)

        cmake_args = get_common_cmake_args(install_dir, platform, webrtc_info)

        # Blend2D
        install_blend2d_args = {
            "version": deps["BLEND2D_VERSION"],
            "version_file": os.path.join(install_dir, "blend2d.version"),
            "configuration": "Debug" if debug else "Release",
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "ios": False,
            "cmake_args": cmake_args,
            "expected_sha256": deps["BLEND2D_SHA256_HASH"],
        }
        install_blend2d_official(**install_blend2d_args)

        # OpenH264
        install_openh264_args = {
            "version": deps["OPENH264_VERSION"],
            "version_file": os.path.join(install_dir, "openh264.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "is_windows": False,
        }
        install_openh264(**install_openh264_args)

        # DuckDB
        install_duckdb_args = {
            "version": deps["DUCKDB_VERSION"],
            "version_file": os.path.join(install_dir, "duckdb.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": platform,
        }
        install_duckdb(**install_duckdb_args)


def _find_clang_binary(name: str) -> Optional[str]:
    if shutil.which(name) is not None:
        return name
    else:
        for n in range(50, 14, -1):
            if shutil.which(f"{name}-{n}") is not None:
                return f"{name}-{n}"
    return None


def _format(
    clang_format_path: Optional[str] = None,
):
    if clang_format_path is None:
        clang_format_path = _find_clang_binary("clang-format")
    if clang_format_path is None:
        raise Exception("clang-format not found. Please install it or specify the path.")
    patterns = [
        "src/**/*.h",
        "src/**/*.cpp",
    ]
    target_files = []
    for pattern in patterns:
        files = glob.glob(pattern, recursive=True)
        target_files.extend(files)
    if target_files:
        cmd([clang_format_path, "-i"] + target_files)


def _build(args):

    target = args.target
    platform = target
    configuration_dir = "debug" if args.debug else "release"
    source_dir = os.path.join(BASE_DIR, "_source", platform, configuration_dir)
    build_dir = os.path.join(BASE_DIR, "_build", platform, configuration_dir)
    install_dir = os.path.join(BASE_DIR, "_install", platform, configuration_dir)
    package_dir = os.path.join(BASE_DIR, "_package", platform, configuration_dir)
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(
        platform,
        source_dir,
        build_dir,
        install_dir,
        args.debug,
        args.local_webrtc_build_dir,
        args.local_webrtc_build_args,
        args.local_sora_cpp_sdk_dir,
        args.local_sora_cpp_sdk_args,
    )

    configuration = "Release"
    if args.debug:
        configuration = "Debug"
    if args.relwithdebinfo:
        configuration = "RelWithDebInfo"

    mkdir_p(os.path.join(build_dir, "zakuro"))
    with cd(os.path.join(build_dir, "zakuro")):
        webrtc_info = get_webrtc_info(
            platform, args.local_webrtc_build_dir, install_dir, args.debug
        )
        webrtc_version = read_version_file(webrtc_info.version_file)
        sora_info = get_sora_info(platform, args.local_sora_cpp_sdk_dir, install_dir, args.debug)

        with cd(BASE_DIR):
            zakuro_version = open("VERSION", encoding="utf-8").read().strip()
            zakuro_commit = cmdcap(["git", "rev-parse", "HEAD"])

        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append(f"-DZAKURO_PLATFORM={args.target}")
        cmake_args.append(f"-DZAKURO_VERSION={zakuro_version}")
        cmake_args.append(f"-DZAKURO_COMMIT={zakuro_commit}")
        cmake_args.append(f"-DWEBRTC_BUILD_VERSION={webrtc_version['WEBRTC_BUILD_VERSION']}")
        cmake_args.append(f"-DWEBRTC_READABLE_VERSION={webrtc_version['WEBRTC_READABLE_VERSION']}")
        cmake_args.append(f"-DWEBRTC_COMMIT={webrtc_version['WEBRTC_COMMIT']}")
        cmake_args.append(f"-DSORA_DIR={cmake_path(sora_info.sora_install_dir)}")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(sora_info.boost_install_dir)}")
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DCLI11_ROOT_DIR={cmake_path(os.path.join(install_dir, 'cli11'))}")
        cmake_args.append(f"-DBLEND2D_ROOT_DIR={cmake_path(os.path.join(install_dir, 'blend2d'))}")
        cmake_args.append(
            f"-DOPENH264_ROOT_DIR={cmake_path(os.path.join(install_dir, 'openh264'))}"
        )
        cmake_args.append(f"-DDUCKDB_ROOT_DIR={cmake_path(os.path.join(install_dir, 'duckdb'))}")
        cmake_args += get_common_cmake_args(install_dir, args.target, webrtc_info)

        cmd(["cmake", BASE_DIR, *cmake_args])
        cmd(
            ["cmake", "--build", ".", f"-j{multiprocessing.cpu_count()}", "--config", configuration]
        )

        # DuckDBのダイナミックライブラリをビルドディレクトリにコピー
        if platform == "macos_arm64":
            shutil.copyfile(
                os.path.join(install_dir, "duckdb", "lib", "libduckdb.dylib"),
                os.path.join(build_dir, "zakuro", "libduckdb.dylib"),
            )
        elif platform in ("ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64"):
            shutil.copyfile(
                os.path.join(install_dir, "duckdb", "lib", "libduckdb.so"),
                os.path.join(build_dir, "zakuro", "libduckdb.so"),
            )

    if args.package:
        mkdir_p(package_dir)
        zakuro_package_dir = os.path.join(package_dir, f"zakuro-{zakuro_version}")
        rm_rf(zakuro_package_dir)
        rm_rf(os.path.join(package_dir, "zakuro.env"))

        with cd(BASE_DIR):
            zakuro_version = open("VERSION", encoding="utf-8").read().strip()

        mkdir_p(zakuro_package_dir)
        with cd(zakuro_package_dir):
            shutil.copyfile(os.path.join(build_dir, "zakuro", "zakuro"), "zakuro")
            shutil.copyfile(os.path.join(BASE_DIR, "LICENSE"), "LICENSE")

            # DuckDBのダイナミックライブラリをコピー
            if platform == "macos_arm64":
                shutil.copyfile(
                    os.path.join(install_dir, "duckdb", "lib", "libduckdb.dylib"), "libduckdb.dylib"
                )
            elif platform in ("ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64"):
                shutil.copyfile(
                    os.path.join(install_dir, "duckdb", "lib", "libduckdb.so"), "libduckdb.so"
                )

            with open("NOTICE", "w") as f:
                f.write(open(os.path.join(BASE_DIR, "NOTICE")).read())
                f.write(open(os.path.join(install_dir, "webrtc", "NOTICE")).read())
                download("http://www.openh264.org/BINARY_LICENSE.txt")
                f.write("# OpenH264 Binary License\n")
                f.write("```\n")
                f.write(open("BINARY_LICENSE.txt").read())
                f.write("```\n")
                rm_rf("BINARY_LICENSE.txt")

        with cd(package_dir):
            archive_name = f"zakuro-{zakuro_version}_{args.target}.tar.gz"
            archive_path = os.path.join(package_dir, archive_name)
            with tarfile.open(archive_path, "w:gz") as f:
                for file in enum_all_files(f"zakuro-{zakuro_version}", "."):
                    f.add(name=file, arcname=file)
            with open(os.path.join(package_dir, "zakuro.env"), "w") as f:
                f.write("CONTENT_TYPE=application/gzip\n")
                f.write(f"PACKAGE_NAME={archive_name}\n")


def main():
    parser = argparse.ArgumentParser()
    sp = parser.add_subparsers(dest="command")

    # build コマンド
    bp = sp.add_parser("build")
    bp.add_argument(
        "target", choices=["macos_arm64", "ubuntu-22.04_x86_64", "ubuntu-24.04_x86_64"]
    )
    bp.add_argument("--debug", action="store_true")
    bp.add_argument("--relwithdebinfo", action="store_true")
    bp.add_argument("--local-webrtc-build-dir", type=os.path.abspath)
    bp.add_argument("--local-webrtc-build-args", default="", type=shlex.split)
    bp.add_argument("--local-sora-cpp-sdk-dir", type=os.path.abspath)
    bp.add_argument("--local-sora-cpp-sdk-args", default="", type=shlex.split)
    bp.add_argument("--package", action="store_true")

    # format コマンド
    fp = sp.add_parser("format")
    fp.add_argument("--clang-format-path", type=str, default=None)

    args = parser.parse_args()

    if args.command == "build":
        _build(args)
    elif args.command == "format":
        _format(clang_format_path=args.clang_format_path)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
