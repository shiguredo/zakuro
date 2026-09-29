# 複数の signaling URL を指定して実 Sora に接続すると SIGABRT する

- Created: 2026-09-29
- Completed: {YYYY-MM-DD}
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

  ```
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
