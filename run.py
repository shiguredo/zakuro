import subprocess
import logging
import os
import urllib.parse
import zipfile
import tarfile
import shutil
import platform
import multiprocessing
import argparse
from typing import Callable, NamedTuple, Optional, List, Union, Dict


logging.basicConfig(level=logging.DEBUG)


class ChangeDirectory(object):
    def __init__(self, cwd):
        self._cwd = cwd

    def __enter__(self):
        self._old_cwd = os.getcwd()
        logging.debug(f'pushd {self._old_cwd} --> {self._cwd}')
        os.chdir(self._cwd)

    def __exit__(self, exctype, excvalue, trace):
        logging.debug(f'popd {self._old_cwd} <-- {self._cwd}')
        os.chdir(self._old_cwd)
        return False


def cd(cwd):
    return ChangeDirectory(cwd)


def cmd(args, **kwargs):
    logging.debug(f'+{args} {kwargs}')
    if 'check' not in kwargs:
        kwargs['check'] = True
    if 'resolve' in kwargs:
        resolve = kwargs['resolve']
        del kwargs['resolve']
    else:
        resolve = True
    if resolve:
        args = [shutil.which(args[0]), *args[1:]]
    return subprocess.run(args, **kwargs)


# 標準出力をキャプチャするコマンド実行。シェルの `cmd ...` や $(cmd ...) と同じ
def cmdcap(args, **kwargs):
    # 3.7 でしか使えない
    # kwargs['capture_output'] = True
    kwargs['stdout'] = subprocess.PIPE
    kwargs['stderr'] = subprocess.PIPE
    kwargs['encoding'] = 'utf-8'
    return cmd(args, **kwargs).stdout.strip()


def rm_rf(path: str):
    if not os.path.exists(path):
        logging.debug(f'rm -rf {path} => path not found')
        return
    if os.path.isfile(path) or os.path.islink(path):
        os.remove(path)
        logging.debug(f'rm -rf {path} => file removed')
    if os.path.isdir(path):
        shutil.rmtree(path)
        logging.debug(f'rm -rf {path} => directory removed')


def mkdir_p(path: str):
    if os.path.exists(path):
        logging.debug(f'mkdir -p {path} => already exists')
        return
    os.makedirs(path, exist_ok=True)
    logging.debug(f'mkdir -p {path} => directory created')


if platform.system() == 'Windows':
    PATH_SEPARATOR = ';'
else:
    PATH_SEPARATOR = ':'


def add_path(path: str, is_after=False):
    logging.debug(f'add_path: {path}')
    if 'PATH' not in os.environ:
        os.environ['PATH'] = path
        return

    if is_after:
        os.environ['PATH'] = os.environ['PATH'] + PATH_SEPARATOR + path
    else:
        os.environ['PATH'] = path + PATH_SEPARATOR + os.environ['PATH']


def download(url: str, output_dir: Optional[str] = None, filename: Optional[str] = None) -> str:
    if filename is None:
        output_path = urllib.parse.urlparse(url).path.split('/')[-1]
    else:
        output_path = filename

    if output_dir is not None:
        output_path = os.path.join(output_dir, output_path)

    if os.path.exists(output_path):
        return output_path

    try:
        if shutil.which('curl') is not None:
            cmd(["curl", "-fLo", output_path, url])
        else:
            cmd(["wget", "-cO", output_path, url])
    except Exception:
        # ゴミを残さないようにする
        if os.path.exists(output_path):
            os.remove(output_path)
        raise

    return output_path


def read_version_file(path: str) -> Dict[str, str]:
    versions = {}

    lines = open(path).readlines()
    for line in lines:
        line = line.strip()

        # コメント行
        if line[:1] == '#':
            continue

        # 空行
        if len(line) == 0:
            continue

        [a, b] = map(lambda x: x.strip(), line.split('=', 2))
        versions[a] = b.strip('"')

    return versions


# dir 以下にある全てのファイルパスを、dir2 からの相対パスで返す
def enum_all_files(dir, dir2):
    for root, _, files in os.walk(dir):
        for file in files:
            yield os.path.relpath(os.path.join(root, file), dir2)


def versioned(func):
    def wrapper(version, version_file, *args, **kwargs):
        if 'ignore_version' in kwargs:
            if kwargs.get('ignore_version'):
                rm_rf(version_file)
            del kwargs['ignore_version']

        if os.path.exists(version_file):
            ver = open(version_file).read()
            if ver.strip() == version.strip():
                return

        r = func(version=version, *args, **kwargs)

        with open(version_file, 'w') as f:
            f.write(version)

        return r

    return wrapper


# アーカイブが単一のディレクトリに全て格納されているかどうかを調べる。
#
# 単一のディレクトリに格納されている場合はそのディレクトリ名を返す。
# そうでない場合は None を返す。
def _is_single_dir(infos: List[Union[zipfile.ZipInfo, tarfile.TarInfo]],
                   get_name: Callable[[Union[zipfile.ZipInfo, tarfile.TarInfo]], str],
                   is_dir: Callable[[Union[zipfile.ZipInfo, tarfile.TarInfo]], bool]) -> Optional[str]:
    # tarfile: ['path', 'path/to', 'path/to/file.txt']
    # zipfile: ['path/', 'path/to/', 'path/to/file.txt']
    # どちらも / 区切りだが、ディレクトリの場合、後ろに / が付くかどうかが違う
    dirname = None
    for info in infos:
        name = get_name(info)
        n = name.rstrip('/').find('/')
        if n == -1:
            # ルートディレクトリにファイルが存在している
            if not is_dir(info):
                return None
            dir = name.rstrip('/')
        else:
            dir = name[0:n]
        # ルートディレクトリに２個以上のディレクトリが存在している
        if dirname is not None and dirname != dir:
            return None
        dirname = dir

    return dirname


def is_single_dir_tar(tar: tarfile.TarFile) -> Optional[str]:
    return _is_single_dir(tar.getmembers(), lambda t: t.name, lambda t: t.isdir())


def is_single_dir_zip(zip: zipfile.ZipFile) -> Optional[str]:
    return _is_single_dir(zip.infolist(), lambda z: z.filename, lambda z: z.is_dir())


# 解凍した上でファイル属性を付与する
def _extractzip(z: zipfile.ZipFile, path: str):
    z.extractall(path)
    if platform.system() == 'Windows':
        return
    for info in z.infolist():
        if info.is_dir():
            continue
        filepath = os.path.join(path, info.filename)
        mod = info.external_attr >> 16
        if (mod & 0o120000) == 0o120000:
            # シンボリックリンク
            with open(filepath, 'r') as f:
                src = f.read()
            os.remove(filepath)
            with cd(os.path.dirname(filepath)):
                if os.path.exists(src):
                    os.symlink(src, filepath)
        if os.path.exists(filepath):
            # 普通のファイル
            os.chmod(filepath, mod & 0o777)


# zip または tar.gz ファイルを展開する。
#
# 展開先のディレクトリは {output_dir}/{output_dirname} となり、
# 展開先のディレクトリが既に存在していた場合は削除される。
#
# もしアーカイブの内容が単一のディレクトリであった場合、
# そのディレクトリは無いものとして展開される。
#
# つまりアーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/file1
# - out/libsora/file2
# が出力される。
#
# また、アーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2', 'LICENSE']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/libsora-1.23/file1
# - out/libsora/libsora-1.23/file2
# - out/libsora/LICENSE
# が出力される。
def extract(file: str, output_dir: str, output_dirname: str, filetype: Optional[str] = None):
    path = os.path.join(output_dir, output_dirname)
    logging.info(f"Extract {file} to {path}")
    if filetype == 'gzip' or file.endswith('.tar.gz'):
        rm_rf(path)
        with tarfile.open(file) as t:
            dir = is_single_dir_tar(t)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                t.extractall(path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                t.extractall(output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    elif filetype == 'zip' or file.endswith('.zip'):
        rm_rf(path)
        with zipfile.ZipFile(file) as z:
            dir = is_single_dir_zip(z)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                # z.extractall(path)
                _extractzip(z, path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                # z.extractall(output_dir)
                _extractzip(z, output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    else:
        raise Exception('file should end with .tar.gz or .zip')


def clone_and_checkout(url, version, dir, fetch, fetch_force):
    if fetch_force:
        rm_rf(dir)

    if not os.path.exists(os.path.join(dir, '.git')):
        cmd(['git', 'clone', url, dir])
        fetch = True

    if fetch:
        with cd(dir):
            cmd(['git', 'fetch'])
            cmd(['git', 'reset', '--hard'])
            cmd(['git', 'clean', '-df'])
            cmd(['git', 'checkout', '-f', version])


def git_clone_shallow(url, hash, dir):
    rm_rf(dir)
    mkdir_p(dir)
    with cd(dir):
        cmd(['git', 'init'])
        cmd(['git', 'remote', 'add', 'origin', url])
        cmd(['git', 'fetch', '--depth=1', 'origin', hash])
        cmd(['git', 'reset', '--hard', 'FETCH_HEAD'])


@versioned
def install_rootfs(version, install_dir, conf):
    rootfs_dir = os.path.join(install_dir, 'rootfs')
    rm_rf(rootfs_dir)
    cmd(['multistrap', '--no-auth', '-a', 'arm64', '-d', rootfs_dir, '-f', conf])
    # 絶対パスのシンボリックリンクを相対パスに置き換えていく
    for dir, _, filenames in os.walk(rootfs_dir):
        for filename in filenames:
            linkpath = os.path.join(dir, filename)
            # symlink かどうか
            if not os.path.islink(linkpath):
                continue
            target = os.readlink(linkpath)
            # 絶対パスかどうか
            if not os.path.isabs(target):
                continue
            # rootfs_dir を先頭に付けることで、
            # rootfs の外から見て正しい絶対パスにする
            targetpath = rootfs_dir + target
            # 参照先の絶対パスが存在するかどうか
            if not os.path.exists(targetpath):
                continue
            # 相対パスに置き換える
            relpath = os.path.relpath(targetpath, dir)
            logging.debug(f'{linkpath[len(rootfs_dir):]} targets {target} to {relpath}')
            os.remove(linkpath)
            os.symlink(relpath, linkpath)

    # なぜかシンボリックリンクが登録されていないので作っておく
    link = os.path.join(rootfs_dir, 'usr', 'lib', 'aarch64-linux-gnu', 'tegra', 'libnvbuf_fdmap.so')
    file = os.path.join(rootfs_dir, 'usr', 'lib', 'aarch64-linux-gnu', 'tegra', 'libnvbuf_fdmap.so.1.0.0')
    if os.path.exists(file) and not os.path.exists(link):
        os.symlink(os.path.basename(file), link)


@versioned
def install_webrtc(version, source_dir, install_dir, platform: str):
    win = platform.startswith("windows_")
    filename = f'webrtc.{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/{version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'webrtc'))
    extract(archive, output_dir=install_dir, output_dirname='webrtc')


class WebrtcInfo(NamedTuple):
    version_file: str
    webrtc_include_dir: str
    webrtc_library_dir: str
    clang_dir: str
    libcxx_dir: str


def get_webrtc_info(webrtcbuild: bool, source_dir: str, build_dir: str, install_dir: str) -> WebrtcInfo:
    webrtc_source_dir = os.path.join(source_dir, 'webrtc')
    webrtc_build_dir = os.path.join(build_dir, 'webrtc')
    webrtc_install_dir = os.path.join(install_dir, 'webrtc')

    if webrtcbuild:
        return WebrtcInfo(
            version_file=os.path.join(source_dir, 'webrtc-build', 'VERSION'),
            webrtc_include_dir=os.path.join(webrtc_source_dir, 'src'),
            webrtc_library_dir=os.path.join(webrtc_build_dir, 'obj')
            if platform.system() == 'Windows' else webrtc_build_dir, clang_dir=os.path.join(
                webrtc_source_dir, 'src', 'third_party', 'llvm-build', 'Release+Asserts'),
            libcxx_dir=os.path.join(webrtc_source_dir, 'src', 'buildtools', 'third_party', 'libc++', 'trunk'),)
    else:
        return WebrtcInfo(
            version_file=os.path.join(webrtc_install_dir, 'VERSIONS'),
            webrtc_include_dir=os.path.join(webrtc_install_dir, 'include'),
            webrtc_library_dir=os.path.join(install_dir, 'webrtc', 'lib'),
            clang_dir=os.path.join(install_dir, 'llvm', 'clang'),
            libcxx_dir=os.path.join(install_dir, 'llvm', 'libcxx'),
        )


@versioned
def install_llvm(version, install_dir,
                 tools_url, tools_commit,
                 libcxx_url, libcxx_commit,
                 buildtools_url, buildtools_commit):
    llvm_dir = os.path.join(install_dir, 'llvm')
    rm_rf(llvm_dir)
    mkdir_p(llvm_dir)
    with cd(llvm_dir):
        # tools の update.py を叩いて特定バージョンの clang バイナリを拾う
        git_clone_shallow(tools_url, tools_commit, 'tools')
        with cd('tools'):
            cmd(['python3',
                os.path.join('clang', 'scripts', 'update.py'),
                '--output-dir', os.path.join(llvm_dir, 'clang')])

        # 特定バージョンの libcxx を利用する
        git_clone_shallow(libcxx_url, libcxx_commit, 'libcxx')

        # __config_site のために特定バージョンの buildtools を取得する
        git_clone_shallow(buildtools_url, buildtools_commit, 'buildtools')
        shutil.copyfile(os.path.join(llvm_dir, 'buildtools', 'third_party', 'libc++', '__config_site'),
                        os.path.join(llvm_dir, 'libcxx', 'include', '__config_site'))


@versioned
def install_boost(version, source_dir, install_dir, sora_version, platform: str):
    win = platform.startswith("windows_")
    filename = f'boost-{version}_sora-cpp-sdk-{sora_version}_{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo/sora-cpp-sdk/releases/download/{sora_version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'boost'))
    extract(archive, output_dir=install_dir, output_dirname='boost')


def cmake_path(path: str) -> str:
    return path.replace('\\', '/')


@versioned
def install_cmake(version, source_dir, install_dir, platform: str, ext):
    url = f'https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-{platform}.{ext}'
    path = download(url, source_dir)
    extract(path, install_dir, 'cmake')
    # Android で自前の CMake を利用する場合、ninja へのパスが見つけられない問題があるので、同じディレクトリに symlink を貼る
    # https://issuetracker.google.com/issues/206099937
    if platform.startswith('linux'):
        with cd(os.path.join(install_dir, 'cmake', 'bin')):
            cmd(['ln', '-s', '/usr/bin/ninja', 'ninja'])


@versioned
def install_sora(version, source_dir, install_dir, platform: str):
    win = platform.startswith("windows_")
    filename = f'sora-cpp-sdk-{version}_{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo/sora-cpp-sdk/releases/download/{version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'sora'))
    extract(archive, output_dir=install_dir, output_dirname='sora')


@versioned
def install_cli11(version, install_dir):
    cli11_install_dir = os.path.join(install_dir, 'cli11')
    rm_rf(cli11_install_dir)
    git_clone_shallow('https://github.com/CLIUtils/CLI11.git', version, cli11_install_dir)


@versioned
def install_blend2d(version, source_dir, build_dir, install_dir, blend2d_version, asmjit_version, cmake_args):
    rm_rf(os.path.join(source_dir, 'blend2d'))
    rm_rf(os.path.join(build_dir, 'blend2d'))
    rm_rf(os.path.join(install_dir, 'blend2d'))

    git_clone_shallow('https://github.com/blend2d/blend2d', blend2d_version, os.path.join(source_dir, 'blend2d'))
    mkdir_p(os.path.join(source_dir, 'blend2d', '3rdparty'))
    git_clone_shallow('https://github.com/asmjit/asmjit', asmjit_version, os.path.join(source_dir, 'blend2d', '3rdparty', 'asmjit'))

    mkdir_p(os.path.join(build_dir, 'blend2d'))
    with cd(os.path.join(build_dir, 'blend2d')):
        cmd(['cmake', os.path.join(source_dir, 'blend2d'),
             '-DCMAKE_BUILD_TYPE=Release',
             f'-DCMAKE_INSTALL_PREFIX={install_dir}/blend2d',
             '-DBLEND2D_STATIC=ON',
             *cmake_args])
        cmd(['cmake', '--build', '.', f'-j{multiprocessing.cpu_count()}'])
        cmd(['cmake', '--build', '.', '--target', 'install'])


@versioned
def install_openh264(version, source_dir, install_dir):
    rm_rf(os.path.join(source_dir, 'openh264'))
    rm_rf(os.path.join(install_dir, 'openh264'))
    git_clone_shallow('https://github.com/cisco/openh264.git', version, os.path.join(source_dir, 'openh264'))
    with cd(os.path.join(source_dir, 'openh264')):
        cmd(['make', f'PREFIX={os.path.join(install_dir, "openh264")}', 'install-headers'])


@versioned
def install_yaml(version, source_dir, build_dir, install_dir, cmake_args):
    rm_rf(os.path.join(source_dir, 'yaml'))
    rm_rf(os.path.join(install_dir, 'yaml'))
    rm_rf(os.path.join(build_dir, 'yaml'))
    git_clone_shallow('https://github.com/jbeder/yaml-cpp.git', version, os.path.join(source_dir, 'yaml'))

    mkdir_p(os.path.join(build_dir, 'yaml'))
    with cd(os.path.join(build_dir, 'yaml')):
        cmd(['cmake', os.path.join(source_dir, 'yaml'),
             '-DCMAKE_BUILD_TYPE=Release',
             f'-DCMAKE_INSTALL_PREFIX={install_dir}/yaml',
             '-DYAML_CPP_BUILD_TESTS=OFF',
             '-DYAML_CPP_BUILD_TOOLS=OFF',
             *cmake_args])
        cmd(['cmake', '--build', '.', f'-j{multiprocessing.cpu_count()}'])
        cmd(['cmake', '--build', '.', '--target', 'install'])


def get_common_cmake_args(install_dir, platform):
    # クロスコンパイルの設定。
    # 本来は toolchain ファイルに書く内容
    if platform in ('ubuntu-20.04_x86_64', 'ubuntu-22.04_x86_64'):
        return [
            f'-DCMAKE_C_COMPILER={install_dir}/llvm/clang/bin/clang',
            f'-DCMAKE_CXX_COMPILER={install_dir}/llvm/clang/bin/clang++',
            '-DCMAKE_CXX_FLAGS=' + ' '.join([
                '-D_LIBCPP_ABI_NAMESPACE=Cr',
                '-D_LIBCPP_ABI_VERSION=2',
                '-D_LIBCPP_DISABLE_AVAILABILITY',
                '-nostdinc++',
                f'-isystem{install_dir}/llvm/libcxx/include',
            ])
        ]
    elif platform == 'macos_arm64':
        sysroot = cmdcap(['xcrun', '--sdk', 'macosx', '--show-sdk-path'])
        return [
            '-DCMAKE_SYSTEM_PROCESSOR=arm64',
            '-DCMAKE_OSX_ARCHITECTURES=arm64',
            "-DCMAKE_C_COMPILER=clang",
            '-DCMAKE_C_COMPILER_TARGET=aarch64-apple-darwin',
            "-DCMAKE_CXX_COMPILER=clang++",
            '-DCMAKE_CXX_COMPILER_TARGET=aarch64-apple-darwin',
            f'-DCMAKE_SYSROOT={sysroot}',
        ]
    else:
        raise Exception(f'Unsupported platform: {platform}')


BASE_DIR = os.path.abspath(os.path.dirname(__file__))


def install_deps(source_dir, build_dir, install_dir, debug, platform):
    with cd(BASE_DIR):
        version = read_version_file('VERSION')

        # WebRTC
        install_webrtc_args = {
            'version': version['WEBRTC_BUILD_VERSION'],
            'version_file': os.path.join(install_dir, 'webrtc.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
            'platform': platform,
        }
        install_webrtc(**install_webrtc_args)

        if platform in ('ubuntu-20.04_x86_64', 'ubuntu-22.04_x86_64'):
            webrtc_info = get_webrtc_info(False, source_dir, build_dir, install_dir)
            webrtc_version = read_version_file(webrtc_info.version_file)

            # LLVM
            tools_url = webrtc_version['WEBRTC_SRC_TOOLS_URL']
            tools_commit = webrtc_version['WEBRTC_SRC_TOOLS_COMMIT']
            libcxx_url = webrtc_version['WEBRTC_SRC_BUILDTOOLS_THIRD_PARTY_LIBCXX_TRUNK_URL']
            libcxx_commit = webrtc_version['WEBRTC_SRC_BUILDTOOLS_THIRD_PARTY_LIBCXX_TRUNK_COMMIT']
            buildtools_url = webrtc_version['WEBRTC_SRC_BUILDTOOLS_URL']
            buildtools_commit = webrtc_version['WEBRTC_SRC_BUILDTOOLS_COMMIT']
            install_llvm_args = {
                'version':
                    f'{tools_url}.{tools_commit}.'
                    f'{libcxx_url}.{libcxx_commit}.'
                    f'{buildtools_url}.{buildtools_commit}',
                'version_file': os.path.join(install_dir, 'llvm.version'),
                'install_dir': install_dir,
                'tools_url': tools_url,
                'tools_commit': tools_commit,
                'libcxx_url': libcxx_url,
                'libcxx_commit': libcxx_commit,
                'buildtools_url': buildtools_url,
                'buildtools_commit': buildtools_commit,
            }
            install_llvm(**install_llvm_args)

        # Boost
        install_boost_args = {
            'version': version['BOOST_VERSION'],
            'version_file': os.path.join(install_dir, 'boost.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
            'sora_version': version['SORA_CPP_SDK_VERSION'],
            'platform': platform,
        }
        install_boost(**install_boost_args)

        # CMake
        install_cmake_args = {
            'version': version['CMAKE_VERSION'],
            'version_file': os.path.join(install_dir, 'cmake.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
            'platform': '',
            'ext': 'tar.gz'
        }
        if platform in ('ubuntu-20.04_x86_64', 'ubuntu-22.04_x86_64'):
            install_cmake_args['platform'] = 'linux-x86_64'
        elif platform == 'macos_arm64':
            install_cmake_args['platform'] = 'macos-universal'
        install_cmake(**install_cmake_args)
        if platform == 'macos_arm64':
            add_path(os.path.join(install_dir, 'cmake', 'CMake.app', 'Contents', 'bin'))
        else:
            add_path(os.path.join(install_dir, 'cmake', 'bin'))

        # Sora C++ SDK
        install_sora_args = {
            'version': version['SORA_CPP_SDK_VERSION'],
            'version_file': os.path.join(install_dir, 'sora.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
            'platform': platform,
        }
        install_sora(**install_sora_args)

        # CLI11
        install_cli11_args = {
            'version': version['CLI11_VERSION'],
            'version_file': os.path.join(install_dir, 'cli11.version'),
            'install_dir': install_dir,
        }
        install_cli11(**install_cli11_args)

        cmake_args = get_common_cmake_args(install_dir, platform)

        # Blend2D
        install_blend2d_args = {
            'version': version['BLEND2D_VERSION'] + '-' + version['ASMJIT_VERSION'],
            'version_file': os.path.join(install_dir, 'blend2d.version'),
            'source_dir': source_dir,
            'build_dir': build_dir,
            'install_dir': install_dir,
            'blend2d_version': version['BLEND2D_VERSION'],
            'asmjit_version': version['ASMJIT_VERSION'],
            'cmake_args': cmake_args,
        }
        install_blend2d(**install_blend2d_args)

        # OpenH264
        install_openh264_args = {
            'version': version['OPENH264_VERSION'],
            'version_file': os.path.join(install_dir, 'openh264.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
        }
        install_openh264(**install_openh264_args)

        # yaml-cpp
        install_yaml_args = {
            'version': version['YAML_CPP_VERSION'],
            'version_file': os.path.join(install_dir, 'yaml.version'),
            'source_dir': source_dir,
            'build_dir': build_dir,
            'install_dir': install_dir,
            'cmake_args': cmake_args,
        }
        install_yaml(**install_yaml_args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=['macos_arm64', 'ubuntu-20.04_x86_64', 'ubuntu-22.04_x86_64'])
    parser.add_argument("--debug", action='store_true')
    parser.add_argument("--package", action='store_true')

    args = parser.parse_args()

    configuration_dir = 'debug' if args.debug else 'release'
    source_dir = os.path.join(BASE_DIR, '_source', args.target, configuration_dir)
    build_dir = os.path.join(BASE_DIR, '_build', args.target, configuration_dir)
    install_dir = os.path.join(BASE_DIR, '_install', args.target, configuration_dir)
    package_dir = os.path.join(BASE_DIR, '_package', args.target, configuration_dir)
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(source_dir, build_dir, install_dir, args.debug, args.target)

    configuration = 'Debug' if args.debug else 'Release'

    mkdir_p(os.path.join(build_dir, 'zakuro'))
    with cd(os.path.join(build_dir, 'zakuro')):
        webrtc_info = get_webrtc_info(False, source_dir, build_dir, install_dir)

        with cd(BASE_DIR):
            version = read_version_file('VERSION')
            zakuro_version = version['ZAKURO_VERSION']
            zakuro_commit = cmdcap(['git', 'rev-parse', 'HEAD'])

        cmake_args = []
        cmake_args.append(f'-DCMAKE_BUILD_TYPE={configuration}')
        cmake_args.append(f'-DZAKURO_PLATFORM={args.target}')
        cmake_args.append(f'-DZAKURO_VERSION={zakuro_version}')
        cmake_args.append(f'-DZAKURO_COMMIT={zakuro_commit}')
        cmake_args.append(f"-DSORA_DIR={cmake_path(os.path.join(install_dir, 'sora'))}")
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(webrtc_info.webrtc_include_dir)}")
        cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(webrtc_info.webrtc_library_dir)}")
        cmake_args.append(f"-DCLI11_ROOT_DIR={cmake_path(os.path.join(install_dir, 'cli11'))}")
        cmake_args.append(f"-DBLEND2D_ROOT_DIR={cmake_path(os.path.join(install_dir, 'blend2d'))}")
        cmake_args.append(f"-DOPENH264_ROOT_DIR={cmake_path(os.path.join(install_dir, 'openh264'))}")
        cmake_args.append(f"-DYAML_ROOT_DIR={cmake_path(os.path.join(install_dir, 'yaml'))}")
        cmake_args += get_common_cmake_args(install_dir, args.target)

        cmd(['cmake', BASE_DIR, *cmake_args])
        cmd(['cmake', '--build', '.', f'-j{multiprocessing.cpu_count()}', '--config', configuration])

    if args.package:
        mkdir_p(package_dir)
        zakuro_package_dir = os.path.join(package_dir, f'zakuro-{zakuro_version}_{args.target}')
        rm_rf(zakuro_package_dir)
        rm_rf(os.path.join(package_dir, 'zakuro.env'))

        with cd(BASE_DIR):
            version = read_version_file('VERSION')
            zakuro_version = version['ZAKURO_VERSION']

        mkdir_p(zakuro_package_dir)
        with cd(zakuro_package_dir):
            shutil.copyfile(os.path.join(build_dir, 'zakuro', 'zakuro'), 'zakuro')
            shutil.copyfile(os.path.join(BASE_DIR, 'LICENSE'), 'LICENSE')
            with open('NOTICE', 'w') as f:
                f.write(open(os.path.join(BASE_DIR, 'NOTICE')).read())
                f.write(open(os.path.join(install_dir, 'webrtc', 'NOTICE')).read())
                download('http://www.openh264.org/BINARY_LICENSE.txt')
                f.write('# OpenH264 Binary License\n')
                f.write('```\n')
                f.write(open('BINARY_LICENSE.txt').read())
                f.write('```\n')
                rm_rf('BINARY_LICENSE.txt')

        with cd(package_dir):
            archive_name = f'zakuro-{zakuro_version}_{args.target}.tar.gz'
            archive_path = os.path.join(package_dir, archive_name)
            with tarfile.open(archive_path, 'w:gz') as f:
                for file in enum_all_files(f'zakuro-{zakuro_version}_{args.target}', '.'):
                    f.add(name=file, arcname=file)
            with open(os.path.join(package_dir, 'zakuro.env'), 'w') as f:
                f.write("CONTENT_TYPE=application/gzip\n")
                f.write(f'PACKAGE_NAME={archive_name}\n')


if __name__ == '__main__':
    main()
