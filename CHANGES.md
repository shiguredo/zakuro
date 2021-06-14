# 変更履歴

- CHANGE
    - 下位互換のない変更
- UPDATE
    - 下位互換がある変更
- ADD
    - 下位互換がある追加
- FIX
    - バグ修正

## develop

- [UPDATE] WebRTC のバージョンを M91 (4472@{#9}) に上げる
    - @melpon
- [UPDATE] `cmake` を `3.20.3` に上げる
    - @voluntas
- [UPDATE] asmjit を `78de7d9c81a6ad1b0f732b52666960d9be1c6461` に上げる
    - @voluntas
- [FIX] DataChannel 周りの新しい仕様への追従
    - @melpon

## 2021.4

- [UPDATE] DataChannel の type: switch に対応
    - @melpon

## 2021.3

- [UPDATE] `cmake` を `3.20.2` に上げる
    - @voluntas
- [UPDATE] DataChannel シグナリングに対応
    - @melpon
- [UPDATE] WebSocket シグナリングの re-offer に対応
    - @melpon
- [FIX] サイマルキャストの active: false に対応
    - @melpon

## 2021.2

- [UPDATE] `cmake` を `3.20.1` に上げる
    - @voluntas
- [UPDATE] WebRTC のバージョンを M90 (4430@{#3}) に上げる
    - @melpon
- [UPDATE] Boost 1.76.0 に上げる
    - @voluntas
- [UPDATE] blend2d を `41e4f9f440ed2cef9f3d19699a14e9e7b11144e4` に上げる
    - @voluntas
- [UPDATE] asmjit を `0dd16b0a98ae1da48563c9cc62f757a9e6bbe9b6` に上げる
    - @voluntas

## 2021.1.2

- [UPDATE] 設定ファイルの内容を出力する際に引数をエスケープする
    - @melpon
- [FIX] 設定ファイルの `metadata` や `signaling-notify-metadata` などが引数に反映されていないのを修正
    - @melpon

## 2021.1.1

- [UPDATE] `cmake` を `3.20.0` に上げる
    - @voluntas
- [FIX] `--metadata` と `--signaling-notify-metadata` 引数に `--sora-` プリフィックスが付いてなかったのを修正
    - @melpon

## 2021.1

- [CHANGE] Sora 関連の引数には `--sora-` の prefix を追加する
    - @melpon
- [CHANGE] CentOS 8 を削除する
    - @voluntas
- [UPDATE] blend2d を `92ba4eaa2f22331bc9823ddb47f53dd8ce683c8b` に上げる
    - @voluntas
- [UPDATE] asmjit を `a4dd0b2d8b0fdbcda777e4d6dae0e76636080113` に上げる
    - @voluntas
- [UPDATE] `libwebrtc` を `M89.4389@{#7}` に上げる
    - @melpon
- [UPDATE] `cmake` を `3.19.4` に上げる
    - @voluntas
- [ADD] サイマルキャストの active と adaptivePtime に対応する
    - @melpon
- [UPDATE] nlohmann/json を Boost.JSON に変更
    - @melpon
- [ADD] 実験的機能として YAML ファイルへ対応する
    - @melpon
- [ADD] 実験的機能としてスポットライト利用時に音声ファイルを利用して好きな数字を発生できる機能を追加する
    - @melpon

## 2020.2

- [ADD] `--game=kuzushi` を実装
    - @melpon
- [UPDATE] `libwebrtc` を `M86.4240@{#10}` に上げる
    - @voluntas
- [UPDATE] `cmake` を `3.18.3` に上げる
    - @voluntas

## 2020.1

**祝リリース**
