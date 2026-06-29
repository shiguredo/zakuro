# Boost.Beast と Boost.Asio の inline namespace 競合でビルドが通らない

- Priority: High
- Created: 2026-06-29
- Completed: {YYYY-MM-DD}
- Model: deepseek-v4-flash
- Branch: feature/fix-boost-beast-inline-namespace
- Polished: {YYYY-MM-DD}

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
- Boost.Beast 1.91 の basic_stream.hpp には ssl::stream の前方宣言があり、
  inline namespace に対応していないため名前解決が曖昧になる
- HTTP プロキシヘッダーが `<boost/beast/ssl.hpp>` を include しているため、
  zakuro がこの影響を受ける

### エラー内容

basic_stream.hpp の前方宣言（candidate A）と
Asio 本体の inline namespace 内の定義（candidate B）が衝突し、
`reference to 'ssl' is ambiguous` が発生する。

代表的なエラー:

```
error: reference to 'ssl' is ambiguous
  boost::asio::ssl::detail::openssl_init<> init_;
  ~~~~~~~~~~~~~^
note: candidate found by name lookup is 'boost::asio::ssl'
      (Boost.Beast の basic_stream.hpp にある forward declaration)
note: candidate found by name lookup is 'boost::asio::v103801_kmn::ssl'
      (Boost.Asio 本体の定義。inline namespace により
       boost::asio::ssl としても見える)
```

同様のエラーが ssl_stream.hpp, websocket/ssl.hpp などでも合計 20 件発生する。

### 原因

- Boost.Asio の変更履歴によると、`BOOST_ASIO_ENABLE_VERSION_NAMESPACE` は
  前方宣言を破壊する可能性があるためデフォルト無効になっている
- Beast の basic_stream.hpp が asio::ssl::stream の前方宣言を
  inline namespace 非対応のまま持っている
- 両者の組み合わせが衝突し、コンパイラが名前解決できず ambiguous になる
- Boost としては既知の制約に相当し、バグではない

## 設計方針

cmake configure 時に basic_stream.hpp の forward declaration を
`BOOST_ASIO_INLINE_NAMESPACE_BEGIN` / `END` でラップするパッチを適用する。
version namespace が有効な環境では inline namespace 内に宣言が入り、
無効な環境ではマクロが空展開されるため、どちらでも正しく動作する。

## 完了条件

`python run.py build macos_arm64` がエラーなく完了すること。

## 解決方法

run.py の `_build()` 内、`install_deps()` 直後にパッチを適用する:

```python
basic_stream_hpp = os.path.join(
    install_dir, "boost", "include",
    "boost", "beast", "core", "basic_stream.hpp"
)
if os.path.exists(basic_stream_hpp):
    s = open(basic_stream_hpp).read()
    if "BOOST_ASIO_INLINE_NAMESPACE_BEGIN" not in s:
        s = s.replace(
            "namespace boost {\nnamespace asio {\nnamespace ssl {"
            "\ntemplate<typename> class stream;\n} // ssl\n}"
            " // asio\n} // boost",
            "namespace boost {\nnamespace asio {\n"
            "BOOST_ASIO_INLINE_NAMESPACE_BEGIN\nnamespace ssl {"
            "\ntemplate<typename> class stream;\n} // ssl\n"
            "BOOST_ASIO_INLINE_NAMESPACE_END\n} // asio\n} // boost"
        )
        open(basic_stream_hpp, "w").write(s)
```

CMakeLists.txt のパッチコードは削除する。
