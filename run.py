import argparse
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
    install_blend2d,
    install_boost,
    install_cli11,
    install_cmake,
    install_llvm,
    install_openh264,
    install_sora_and_deps,
    install_webrtc,
    install_yaml,
    mkdir_p,
    read_version_file,
    rm_rf,
)

logging.basicConfig(level=logging.DEBUG)


def get_common_cmake_args(install_dir, platform, webrtc_info: WebrtcInfo):
    # クロスコンパイルの設定。
    # 本来は toolchain ファイルに書く内容
    if platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
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
    webrtc_build_dir: Optional[str],
    webrtc_build_args: List[str],
    sora_dir: Optional[str],
    sora_args: List[str],
):
    with cd(BASE_DIR):
        version = read_version_file("VERSION")

        # WebRTC
        if webrtc_build_dir is None:
            install_webrtc_args = {
                "version": version["WEBRTC_BUILD_VERSION"],
                "version_file": os.path.join(install_dir, "webrtc.version"),
                "source_dir": source_dir,
                "install_dir": install_dir,
                "platform": platform,
            }

            install_webrtc(**install_webrtc_args)
        else:
            build_webrtc_args = {
                "platform": platform,
                "webrtc_build_dir": webrtc_build_dir,
                "webrtc_build_args": webrtc_build_args,
                "debug": debug,
            }

            build_webrtc(**build_webrtc_args)

        webrtc_info = get_webrtc_info(platform, webrtc_build_dir, install_dir, debug)

        if platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64") and webrtc_build_dir is None:
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

        # Boost
        install_boost_args = {
            "version": version["BOOST_VERSION"],
            "version_file": os.path.join(install_dir, "boost.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "sora_version": version["SORA_CPP_SDK_VERSION"],
            "platform": platform,
        }
        install_boost(**install_boost_args)

        # CMake
        install_cmake_args = {
            "version": version["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
            "ext": "tar.gz",
        }
        if platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
            install_cmake_args["platform"] = "linux-x86_64"
        elif platform == "macos_arm64":
            install_cmake_args["platform"] = "macos-universal"
        install_cmake(**install_cmake_args)
        if platform == "macos_arm64":
            add_path(os.path.join(install_dir, "cmake", "CMake.app", "Contents", "bin"))
        else:
            add_path(os.path.join(install_dir, "cmake", "bin"))

        # Sora C++ SDK
        if sora_dir is None:
            install_sora_and_deps(platform, source_dir, install_dir)
        else:
            build_sora(platform, sora_dir, sora_args, debug, webrtc_build_dir)

        # CLI11
        install_cli11_args = {
            "version": version["CLI11_VERSION"],
            "version_file": os.path.join(install_dir, "cli11.version"),
            "install_dir": install_dir,
        }
        install_cli11(**install_cli11_args)

        cmake_args = get_common_cmake_args(install_dir, platform, webrtc_info)

        # Blend2D
        install_blend2d_args = {
            "version": version["BLEND2D_VERSION"] + "-" + version["ASMJIT_VERSION"],
            "version_file": os.path.join(install_dir, "blend2d.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "blend2d_version": version["BLEND2D_VERSION"],
            "asmjit_version": version["ASMJIT_VERSION"],
            "cmake_args": cmake_args,
        }
        install_blend2d(**install_blend2d_args)

        # OpenH264
        install_openh264_args = {
            "version": version["OPENH264_VERSION"],
            "version_file": os.path.join(install_dir, "openh264.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
        }
        install_openh264(**install_openh264_args)

        # yaml-cpp
        install_yaml_args = {
            "version": version["YAML_CPP_VERSION"],
            "version_file": os.path.join(install_dir, "yaml.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": cmake_args,
        }
        install_yaml(**install_yaml_args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "target", choices=["macos_arm64", "ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"]
    )
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--relwithdebinfo", action="store_true")
    parser.add_argument("--webrtc-build-dir", type=os.path.abspath)
    parser.add_argument("--webrtc-build-args", default="", type=shlex.split)
    parser.add_argument("--sora-dir", type=os.path.abspath)
    parser.add_argument("--sora-args", default="", type=shlex.split)
    parser.add_argument("--package", action="store_true")

    args = parser.parse_args()

    platform = args.target
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
        args.webrtc_build_dir,
        args.webrtc_build_args,
        args.sora_dir,
        args.sora_args,
    )

    configuration = "Release"
    if args.debug:
        configuration = "Debug"
    if args.relwithdebinfo:
        configuration = "RelWithDebInfo"

    mkdir_p(os.path.join(build_dir, "zakuro"))
    with cd(os.path.join(build_dir, "zakuro")):
        webrtc_info = get_webrtc_info(platform, args.webrtc_build_dir, install_dir, args.debug)
        webrtc_version = read_version_file(webrtc_info.version_file)
        sora_info = get_sora_info(platform, args.sora_dir, install_dir, args.debug)

        with cd(BASE_DIR):
            version = read_version_file("VERSION")
            zakuro_version = version["ZAKURO_VERSION"]
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
        cmake_args.append(f"-DYAML_ROOT_DIR={cmake_path(os.path.join(install_dir, 'yaml'))}")
        cmake_args += get_common_cmake_args(install_dir, args.target, webrtc_info)

        cmd(["cmake", BASE_DIR, *cmake_args])
        cmd(
            ["cmake", "--build", ".", f"-j{multiprocessing.cpu_count()}", "--config", configuration]
        )

    if args.package:
        mkdir_p(package_dir)
        zakuro_package_dir = os.path.join(package_dir, f"zakuro-{zakuro_version}")
        rm_rf(zakuro_package_dir)
        rm_rf(os.path.join(package_dir, "zakuro.env"))

        with cd(BASE_DIR):
            version = read_version_file("VERSION")
            zakuro_version = version["ZAKURO_VERSION"]

        mkdir_p(zakuro_package_dir)
        with cd(zakuro_package_dir):
            shutil.copyfile(os.path.join(build_dir, "zakuro", "zakuro"), "zakuro")
            shutil.copyfile(os.path.join(BASE_DIR, "LICENSE"), "LICENSE")
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


if __name__ == "__main__":
    main()
