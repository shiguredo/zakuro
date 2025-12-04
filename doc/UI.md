# UI

## 概要

`--ui` オプションを指定すると、Zakuro UI が利用できます。

## 使い方

```bash
./zakuro --ui ...
```

ブラウザで `http://127.0.0.1:3960/` にアクセスすると Zakuro UI が表示されます。

## カスタム URL

`--ui-remote-url` で Zakuro UI の URL を変更して、開発時や自前の Zakuro UI を利用できます。

```bash
./zakuro --ui --ui-remote-url http://localhost:5173 ...
```
