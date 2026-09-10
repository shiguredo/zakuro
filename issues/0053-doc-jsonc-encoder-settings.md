# JSONC で h264-encoder と h265-encoder を指定する方法をドキュメントに書く

- Created: 2026-09-10
- Completed: {YYYY-MM-DD}
- Branch: feature/add-jsonc-encoder-settings
- Polished: {YYYY-MM-DD}

## 目的

Ubuntu で HWA Encoder (ハードウェアエンコーダー) を JSONC 設定ファイルで利用するときの指定方法を動作確認のうえドキュメントにまとめる。H.264 / H.265 はハードウェアエンコーダーを明示的に指定する必要があるが、現状のドキュメントは CLI オプションでの指定しか説明しておらず、JSONC 設定ファイルでの書き方が分からない。

## 現状

- `doc/USE.md` の「利用するエンコーダーの指定」節に `--h264-encoder` / `--h265-encoder` を含む CLI オプションと受付値 (`internal`, `cisco_openh264`, `intel_vpl`, `nvidia_video_codec`, `amd_amf`) の記載はあるが、JSONC 設定ファイルでの指定方法は書かれていない。
- `doc/USE.md` の「JSONC 設定」節の例にエンコーダー指定のキーが無い。
- `doc/FAQ.md` の H.264 / H.265 の項目は CLI オプション (`--h264-encoder intel_vpl` など) での指定のみを案内している。
- 実装は `src/util.cpp` の `Util::ParseInstanceToArgs` が JSONC の instance オブジェクトを CLI 引数に変換する。`h264-encoder` / `h265-encoder` は `sora` の下ではなく instance 直下のキーとして処理される。
- H.265 の送信にはハードウェアエンコーダーが必須 (`doc/FAQ.md`)。
- JSONC の instance 直下キーが CLI 引数へ変換される仕組みの説明はドキュメントに無い。

## 設計方針

- 主に Ubuntu で HWA Encoder (Intel VPL / NVIDIA Video Codec / AMD AMF) を JSONC 設定ファイルで指定して動作確認する。`--show-video-codec-capability` で利用可能なエンコーダーを確認し、指定したエンコーダーで送信できることを確かめる。
- 確認結果を基に `doc/USE.md` の JSONC 設定節へ `h264-encoder` / `h265-encoder` の指定例を追加する。キーが `sora` の下ではなく instance 直下であることを明記する。

```jsonc
{
  "instances": [
    {
      "h264-encoder": "intel_vpl",
      "h265-encoder": "intel_vpl",
      "sora": {
        // ...
      }
    }
  ]
}
```

- `doc/FAQ.md` の H.264 / H.265 の項目に JSONC での指定方法への言及を追加するかは、USE.md の記載内容を見て判断する。
- ハードウェアエンコーダーごとの前提条件 (ドライバー / ライブラリ) や同時ストリーム数の制限は、確認できた範囲で記載する。未確認の内容は書かない。
- `doc/USE.md` は issues/0019 も JSONC 設定例とランタイム依存の更新を行うため、実装時に issues/0019 の反映状況を確認する。

## 完了条件

- Ubuntu で JSONC 設定ファイルに `h264-encoder` / `h265-encoder` を指定したときの動作確認が完了していること
- `doc/USE.md` に JSONC での `h264-encoder` / `h265-encoder` の指定方法 (キーの位置を含む) が記載されていること
- 記載内容が実装 (`src/util.cpp` の `Util::ParseInstanceToArgs`) と動作確認の結果に一致していること

## 解決方法

(実装時に記入)
