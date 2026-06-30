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
  （[boost_asio/history.html](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/history.html):
  "The inline namespace is disabled by default to avoid breaking existing code
  that forward declares Asio names."）
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

パッチを buildbase.py の `install_boost()` 内で行う理由:

- version namespace の有効/無効は Sora C++ SDK のバンドルする Boost の
   ビルド設定に依存し、CMake の configure 時点では判断できない
- `install_boost()` で Boost がインストールされた直後、
   cmake を実行する前にパッチすることで、
   インストール済みのヘッダファイルを確実に修正できる
- `@versioned` デコレータのキャッシュ機構により、
   一度パッチが適用されれば次回以降のビルドで重複処理が発生しない
- CMakeLists.txt でファイル操作を行うより buildbase.py の Python コードの方が
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

buildbase.py の `install_boost()` 内、`extract()` 直後に
`BOOST_PATCH_BEAST_INLINE_NAMESPACE` 定数で定義した unified diff を
`apply_patch_text()` で適用する。

パッチの内容は、前方宣言の `namespace asio {` 直後に
`BOOST_ASIO_INLINE_NAMESPACE_BEGIN` を、`} // ssl` 直後に
`BOOST_ASIO_INLINE_NAMESPACE_END` を挿入するだけの最小限の diff とする。

`--local-sora-cpp-sdk-dir` 使用時はローカルの Sora C++ SDK が
バンドルする Boost のパスが異なる可能性があるため、
未対応の場合は既知の制限として明記する。

```python
# buildbase.py

# Sora C++ SDK 2026.2.0-canary.15 以降で BOOST_ASIO_ENABLE_VERSION_NAMESPACE
# が有効になると Boost.Asio が inline namespace (例: v103801_kmn) を使用する。
# これは異なるバージョンの Asio が同一プロセス内で共存できるようにするための
# 機能で、Unity Editor 6000.3 とのシンボル衝突回避のために有効化された。
#
# しかし Boost.Beast 1.91 の basic_stream.hpp には
# boost::asio::ssl::stream の前方宣言があり、inline namespace に対応していない。
# そのため version namespace が有効な環境では名前解決が曖昧になり
# ビルドエラーが発生する。
#
# Boost.Asio 側でも「前方宣言を壊す可能性があるためデフォルト無効」としており、
# unofficial な設定と Beast 側の未対応の組み合わせで顕在化した問題。
#
# 対応: 前方宣言を BOOST_ASIO_INLINE_NAMESPACE_BEGIN / END でラップする。
# 有効時は inline namespace 内に宣言が入り、無効時はマクロが空展開されるため、
# どちらの設定でも正しく動作する。
BOOST_PATCH_BEAST_INLINE_NAMESPACE = r"""
diff --git a/include/boost/beast/core/basic_stream.hpp b/include/boost/beast/core/basic_stream.hpp
--- a/include/boost/beast/core/basic_stream.hpp
+++ b/include/boost/beast/core/basic_stream.hpp
@@ -1,7 +1,9 @@
 namespace boost {
 namespace asio {
+BOOST_ASIO_INLINE_NAMESPACE_BEGIN
 namespace ssl {
 template<typename> class stream;
 } // ssl
+BOOST_ASIO_INLINE_NAMESPACE_END
 } // asio
 } // boost
"""

@versioned
def install_boost(
    ...
):
    ...
    # basic_stream.hpp の inline namespace 競合を修正するパッチ
    apply_patch_text(
        BOOST_PATCH_BEAST_INLINE_NAMESPACE,
        os.path.join(install_dir, "boost"), 1
    )
```
