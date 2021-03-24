# FAQ

## WebRTC Load Testing Tool Zakuro とはなんですか？

[株式会社時雨堂](https://shiguredo.jp) が開発/販売している [WebRTC SFU Sora](https://sora.shiguredo.jp) 専用の負荷試験ツールです。

## Zakuro は何に利用するのですか？

Sora を運用するサーバのサイジングの見積もりが目的です。
WebRTC は要求するビットレートや利用するネットワーク、送信者、受信者の数によって負荷が変わります。

そのため今まではサイジングがとても難しく、ある程度余裕のあるスペックを用意するしかありませんでした。

Zakuro を提供することで、 Sora を運用するサーバのサイジングを行えるようにするのが目的です。

## Zakuro は無料で利用できますか？

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0>`_ としてソースコードを公開しています。

## Zakuro のライセンスを教えて下さい

[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) です。

```
Copyright 2020-2021, Wandbox LLC (Original Author)
Copyright 2020-2021, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## Zakuro に含まれている音声ファイルについて教えてください

Zakuro のスポットライト機能検証時に利用する音声ファイルには [あみたろの声素材工房](http://www14.big.or.jp/~amiami/happy/) 様の声素材を使用しています。

## Zakuro の動作環境を教えて下さい

サーバでの用途を前提としているため Linux での動作を想定しています。

- Ubuntu 20.04 x86_64
- Ubuntu 18.04 x86_64

## Zakuro は破壊的変更を行いますか？

積極的な破壊的変更を行います。

## Zakuro を利用する際の注意点はありますか？

### ファイルディスクリプタ数

かなりの数を消費しますので、多めに設定することを推奨します。

### 負荷をかける対象

Zakuro は Sora に膨大な負荷をかけますので、利用する場合はとても注意してください。

ツール利用者が運用していない環境への利用は絶対に行わないようにしてください。

### サーバスペック

これは効率の良いシミュレータではなく力技で負荷を実現するツールです。

そのため、大量の CPU リソースを消費します。利用する場合は多くのコアを積んだサーバを用意してください。

## 実際のブラウザなどと異なりますか？

異なります。

ただし libwebrtc をベースにしているため、 WebRTC の挙動自体はブラウザとほぼ同じです。
さらに 1 接続 1 エンコーダを採用しているため、 1 接続が独立しています。

そのため再送や全画面要求に対してもそれぞれの接続ごとに処理を行います。

## 遅延を確認するにはどうすればいいですか？

`--game kuzushi` を利用してみてください。ブロック崩しゲームが始まります。

## エンコーダやデコーダに負荷をかけるにはどうすればいいですか？

砂嵐を生成する `--sandstorm` を利用してみてください。ただしこれは凄まじく CPU とネットワークを消費します。
