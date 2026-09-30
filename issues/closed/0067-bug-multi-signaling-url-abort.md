# 複数の signaling URL を指定して実 Sora に接続すると SIGABRT する

- Created: 2026-09-29
- Completed: 2026-09-29
- Branch: feature/fix-multi-signaling-url-abort
- Polished: {YYYY-MM-DD}

## 目的

`.github/workflows/ci.yml` の `pytest` ジョブで実 Sora に接続する E2E テストを実行するようにしたところ、
`test/test_zakuro.py` の `test_version` で zakuro が SIGABRT することが判明した。
zakuro は `--sora-signaling-url` を複数指定でき (`src/util.cpp` の `app.add_option("--sora-signaling-url", ...)` に
`take_all()` を指定しており、設定ファイルでは `signaling-url` に配列を書ける)、
複数指定した状態で実 Sora に接続すると必ず落ちるため修正する。

## 現状

- CI (ubuntu-24.04、組織シークレットの signaling URL 2 本と `access_token` を使用) で `test_version` が
  `RuntimeError: zakuro process exited unexpectedly with code -6` で失敗する。再実行でも同じ箇所で失敗し、2 回とも再現する
- zakuro の stderr の末尾は次のとおり (`<...>` は伏字)

  ```text
  [000:551] (sora_signaling.cpp:857): OnConnect url=wss://<1 本目の signaling URL>
  [000:566] (sora_signaling.cpp:896): Connected: url=wss://<1 本目の signaling URL>
  [000:566] (sora_signaling.cpp:478): Send type=connect: {"type":"connect","role":"sendrecv",...}
  [000:638] (sora_signaling.cpp:857): OnConnect url=wss://<2 本目の signaling URL>
  [000:638] (websocket.cpp:599): DoClose wss this=...
  [000:695] (p2p_transport_channel.cc:614): Set regather_on_failed_networks_interval to 300 s
  string:3005: libc++ Hardening assertion __n == 0 || __s != nullptr failed: string::append received nullptr
  ```

- 1 本目の WebSocket が接続済みの状態で 2 本目のハンドシェイクが完了し、Sora C++ SDK の
  `SoraSignaling::OnConnect` が負けた側を `Websocket::Close` で閉じている
- abort は `Websocket::DoClose` の直後ではなく、libwebrtc の `p2p_transport_channel.cc` のログが出た後
  (offer を受け取って PeerConnection を準備している途中) に起きている
- libc++ の hardening assertion であり、`std::string::append` に nullptr が渡されている。
  `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` は `run.py` の `get_common_cmake_args` が付与している
- zakuro の `src/` に `append()` の呼び出しは無い。Sora C++ SDK (`DEPS` の `SORA_CPP_SDK_VERSION` は `2026.2.1`) の
  WebSocket / PeerConnection 準備経路、libwebrtc、Boost のいずれかで発生している
- 手元 (macOS) で同じ 2 本の signaling URL を指定した場合は abort しない。組織シークレットの `access_token` が
  無いため接続が受理されず、offer を受け取る経路に入らない (`Websocket::OnClose code=1000` で正常に終了する)

## 設計方針

- 再現条件を確定する
  - signaling URL が 1 本の場合に abort するか (複数指定が必須条件か)
  - 接続が受理されて offer を受け取る場合に限るか
- abort 箇所を特定する
  - CI 上でのみ再現するため、`ci.yml` の `pytest` ジョブで core を取得するか gdb でスタックトレースを取り、
    `std::string::append` に nullptr を渡している呼び出し元を特定する
  - 原因が Sora C++ SDK 側か zakuro 側かを切り分ける。SDK 側なら Sora C++ SDK に issue を立てて修正版に更新し、
    zakuro 側なら zakuro で修正する
- 修正後は CI の `pytest` ジョブが緑になることを確認する (この経路は CI が毎回実行する)

## 完了条件

- signaling URL を複数指定して実 Sora に接続しても zakuro が SIGABRT しないこと
- `.github/workflows/ci.yml` の `pytest` ジョブが成功すること
- abort の原因が Sora C++ SDK 側か zakuro 側かがこの issue に記録されていること

## 解決方法

abort の原因は Sora C++ SDK 側であり、SDK の `2026.2.2` で修正されたため、
zakuro は SDK を更新して暫定措置の `xfail` を外した。

### 原因 (Sora C++ SDK 側)

`Websocket::OnClose` が `reason().reason` (`boost::beast::static_string`) を `RTC_LOG` へ
そのまま渡していた。libwebrtc のログ機構は引数を `MakeVal` で包むが、この型は
`absl::string_view` への暗黙変換が選ばれ、変換で作られた一時オブジェクトへのポインタが
`operator<<` を抜けた時点で破棄される。破棄後の領域をログが参照するため、
libc++ の hardening assertion (`std::string::append` に nullptr) で SIGABRT していた。

`Websocket::OnClose` は通常の切断でも通るため、シグナリング URL が 1 本でも同じ未定義動作が
発生しうる。zakuro で必ず再現していたのは、複数の signaling URL を指定すると接続に敗れた側の
WebSocket が必ず `OnClose` を通るためである。

修正は `reason().reason.c_str()` を渡す形に変更され、同じ問題を持つ AMF のエンコーダ /
デコーダも `c_str()` を経由するように直されている。SDK の `CHANGES.md` の `[FIX]` と
`issues/closed/0108-bug-websocket-onclose-log-abort.md` に記録がある。

### zakuro 側の変更

- `DEPS` の `SORA_CPP_SDK_VERSION` を `2026.2.1` から `2026.2.2` に上げた。
  `2026.2.2` の変更は `src/websocket.cpp` と AMF の 2 ファイル、`CHANGES.md`、`VERSION` のみで、
  `DEPS` の他の依存 (WEBRTC_BUILD_VERSION / BOOST_VERSION / OPENH264_VERSION など) は
  `2026.2.1` と同一である。zakuro 側のソース変更は不要だった
- `test/test_zakuro.py` の `test_version` から `xfail` と、暫定措置である旨のコメントを削除した。
  実 Sora に接続する経路が CI で検証されるようになる
- `CHANGES.md` の `## develop` に `[UPDATE]` を追記した

### 検証結果

- `python3 run.py build macos_arm64` が成功し、`test/` 配下の pytest が
  76 passed / 1 skipped で通ることを確認した (`test_version` は組織シークレットが無いため skip)
- CI の `pytest` ジョブ (組織シークレットの signaling URL 2 本を使用) で
  `test_zakuro.py::test_version` が `XFAIL` から `PASSED` に変わり、77 passed / skipped 0 で
  成功することを確認した。SIGABRT は発生していない
- 修正前は同じ CI で `RuntimeError: zakuro process exited unexpectedly with code -6` が
  再現していたため、この経路は CI が毎回検証する
