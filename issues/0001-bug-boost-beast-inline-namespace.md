# Boost.Beast と Boost.Asio の inline namespace 競合でビルドが通らない

- Priority: High
- Created: 2026-06-29
- Completed: {YYYY-MM-DD}
- Model: deepseek-v4-flash
- Branch: feature/fix-boost-beast-inline-namespace
- Polished: 2026-06-29

## 目的

Sora C++ SDK 2026.2.0-canary.15 以降で
`BOOST_ASIO_ENABLE_VERSION_NAMESPACE` が有効化されたことにより、
Boost.Beast の basic_stream.hpp にある ssl::stream の前方宣言と
Boost.Asio の inline namespace が競合し、ビルドエラーが発生する。
これを修正する。

## 優先度根拠

現在の Sora C++ SDK では zakuro のビルドが通らない。
開発の妨げになるため High。

## 現状

- Sora C++ SDK 2026.2.0-canary.15 で `BOOST_ASIO_ENABLE_VERSION_NAMESPACE`
  が追加された（Unity Editor 6000.3 とのシンボル衝突回避のため）
- この定義により Boost.Asio が `inline namespace v103801_kmn` を使用する
- Boost.Beast 1.91 (DEPS の BOOST_VERSION で管理) の basic_stream.hpp
  には ssl::stream の前方宣言があり、
  inline namespace に対応していないため名前解決が曖昧になる
- HTTP プロキシヘッダーが `<boost/beast/ssl.hpp>` を include しているため、
  zakuro がこの影響を受ける

### エラー内容

basic_stream.hpp の前方宣言（candidate A）と
Asio 本体の inline namespace 内の定義（candidate B）が衝突し、
`reference to 'ssl' is ambiguous` が発生する。

代表的なエラー（注釈は実際のコンパイラ出力に追記）:

```
error: reference to 'ssl' is ambiguous
  boost::asio::ssl::detail::openssl_init<> init_;
  ~~~~~~~~~~~~~^
note: candidate found by name lookup is 'boost::asio::ssl'
      // Boost.Beast の basic_stream.hpp にある forward declaration
note: candidate found by name lookup is 'boost::asio::v103801_kmn::ssl'
      // Boost.Asio 本体の定義。inline namespace により
      // boost::asio::ssl としても見える
```

同様のエラーが ssl_stream.hpp, websocket/ssl.hpp などでも合計 20 件発生する。
実測値であり、basic_stream.hpp 内の一箇所の前方宣言に起因するため、
basic_stream.hpp の修正で全件解消される。

### 原因

- Boost.Asio の変更履歴によると、`BOOST_ASIO_ENABLE_VERSION_NAMESPACE` は
  前方宣言を破壊する可能性があるためデフォルト無効になっている
- Beast の basic_stream.hpp が asio::ssl::stream の前方宣言を
  inline namespace 非対応のまま持っている
- 両者の組み合わせが衝突し、コンパイラが名前解決できず ambiguous になる
- Boost としては既知の制約に相当し、バグではない

### 影響範囲

- `<boost/beast/ssl.hpp>` を直接 include しているファイル:
  - `src/http_proxy.h` (ssl_stream 利用のため)
- `<boost/beast/core.hpp>` を include しているファイル:
  - `src/http_proxy.h` (`<boost/beast/core.hpp>` 経由で basic_stream.hpp が引き込まれる)
  - `src/http_server.h` (`<boost/beast/core.hpp>` を直接 include)
- 上記ヘッダを間接的に include する全翻訳単位が影響を受ける
- エラーは basic_stream.hpp 内の一箇所の前方宣言に起因する
  （ssl_stream.hpp, websocket/ssl.hpp に独立した前方宣言はなく、
  basic_stream.hpp を include することで問題が伝播している）

## 設計方針

cmake 実行前に basic_stream.hpp の forward declaration を
`BOOST_ASIO_INLINE_NAMESPACE_BEGIN` / `END` でラップするパッチを適用する。

version namespace が有効な環境では inline namespace 内に宣言が入り、
無効な環境ではマクロが空展開されるため、どちらでも正しく動作する。

パッチを run.py の `_build()` 内で行う理由:

- version namespace の有効/無効は Sora C++ SDK のバンドルする Boost の
  ビルド設定に依存し、CMake の configure 時点では判断できない
- `install_deps()` で Boost がインストールされた直後、
  cmake を実行する前にパッチすることで、
  インストール済みのヘッダファイルを確実に修正できる
- CMakeLists.txt でファイル操作を行うより run.py の Python コードの方が
  条件分岐・エラーハンドリング・ログ出力が容易

## 完了条件

以下のすべてのプラットフォームで `python run.py build <target>` が
エラーなく完了すること:

- `macos_arm64`
- `ubuntu-22.04_x86_64`
- `ubuntu-24.04_x86_64`

また、パッチが正しく適用されたことを確認するため、ビルドログまたは
以下のコマンドで `basic_stream.hpp` 内に
`BOOST_ASIO_INLINE_NAMESPACE_BEGIN` が含まれていることを確認する:

```
grep -c "BOOST_ASIO_INLINE_NAMESPACE_BEGIN" \
  _install/<platform>/<config>/boost/include/boost/beast/core/basic_stream.hpp
```

zakuro の公開 API や ABI には影響しない。

## 解決方法

2026-07-01 追記: 本対応は zakuro の buildbase.py ではなく
sora-cpp-sdk の buildbase.py で行うこととし、
zakuro 側のパッチ追加は revert した。
sora-cpp-sdk PR #341 で以下の対応を行った:
- buildbase.py の `build_and_install_boost()` 内で basic_stream.hpp の
  前方宣言を `BOOST_ASIO_INLINE_NAMESPACE_BEGIN` / `END` でラップする
  パッチを追加
- CHANGES.md に追記

## 以下の内容は全て採用しない方針とした

run.py の `_build()` 内、`install_deps()` 呼び出し直後にパッチを追加する。

パッチの置換文字列は、Sora C++ SDK 2026.2.0-canary.18 / Boost 1.91
(DEPS で管理) の `basic_stream.hpp` を確認し、
インデントや改行位置が一致するよう調整すること。

`--local-sora-cpp-sdk-dir` 使用時はローカルの Sora C++ SDK が
バンドルする Boost のパスが異なる可能性があるため、
その場合は boost_install_dir を動的に解決してから
パッチを適用すること。未対応の場合は既知の制限として明記する。

```python
# get_sora_info() の取得後（300行目前後）に移動する場合は
# sora_info.boost_install_dir を利用してパスを構成する:
#   basic_stream_hpp = os.path.join(
#       sora_info.boost_install_dir, "include",
#       "boost", "beast", "core", "basic_stream.hpp"
#   )
basic_stream_hpp = os.path.join(
    install_dir, "boost", "include",
    "boost", "beast", "core", "basic_stream.hpp"
)
if not os.path.exists(basic_stream_hpp):
    logging.warning(f"basic_stream.hpp not found: {basic_stream_hpp}")
else:
    s = open(basic_stream_hpp).read()
    if "BOOST_ASIO_INLINE_NAMESPACE_BEGIN" in s:
        logging.info(
            "basic_stream.hpp already patched, skipping")
    else:
        old_fwd = (
            "namespace boost {\n"
            "namespace asio {\n"
            "namespace ssl {"
            "\ntemplate<typename> class stream;\n"
            "} // ssl\n"
            "} // asio\n"
            "} // boost"
        )
        new_fwd = (
            "namespace boost {\n"
            "namespace asio {\n"
            "BOOST_ASIO_INLINE_NAMESPACE_BEGIN\n"
            "namespace ssl {"
            "\ntemplate<typename> class stream;\n"
            "} // ssl\n"
            "BOOST_ASIO_INLINE_NAMESPACE_END\n"
            "} // asio\n"
            "} // boost"
        )
        if old_fwd not in s:
            logging.warning(
                "basic_stream.hpp: forward declaration pattern "
                "not found, patch skipped")
        else:
            s = s.replace(old_fwd, new_fwd)
            open(basic_stream_hpp, "w").write(s)
            logging.info(
                "basic_stream.hpp: patched forward declaration")
```
