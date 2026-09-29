# ParseDataChannels の interval バリデーションに到達不能コードがある

- Created: 2026-08-27
- Completed: 2026-09-29
- Branch: feature/fix-parse-data-channels-unreachable-code
- Polished: 2026-09-07
- Milestone: 2026.1.0

## 目的

`Zakuro::Run` から呼ばれる `ParseDataChannels` (`src/zakuro.cpp`) の `interval` バリデーション分岐に、
無条件の `return false;` の後に置かれた `obj.erase(it);` という到達不能コードが残っている。これを削除する。

## 現状

`src/zakuro.cpp` の `ParseDataChannels` の `interval` ブロックは次のようになっている。

```cpp
// interval
{
  auto it = obj.find("interval");
  if (it != obj.end()) {
    if (!it->value().is_number()) {
      std::cout << __LINE__ << std::endl;
      return false;
    }
    auto interval = boost::json::try_value_to<int>(it->value());
    if (interval.has_error()) {
      std::cout << __LINE__ << std::endl;
      return false;
    }
    ch.interval = *interval;
    if (ch.interval <= 0) {
      std::cout << __LINE__ << std::endl;
      return false;
      obj.erase(it);
    }
  }
}
```

`obj.erase(it);` は無条件の `return false;` の直後にあるため、永久に実行されない。
到達不能コードを報告するコンパイラ警告 (clang の `-Wunreachable-code`、MSVC の `/W4` の C4702 など) の対象になる。

### 確認した事実

- `size-min` / `size-max` は範囲チェックの後に `obj.erase(it);` を呼んでおり、値が正常なら到達する。
- `label` / `direction` / `ordered` / `max_packet_life_time` / `max_retransmits` / `protocol` / `compress` は
  `obj.erase` を呼んでいない。erase を呼ぶのは `size-min` / `size-max` だけである。
- `obj.erase(it)` は現行実装ではどの項目でも観測可能な効果を持たない。`ParseDataChannels` は
  `boost::json::value` を値渡しで受け取り、解析後にこの JSON を参照する箇所は存在しない。
  未解析キーを引き回していた `m.remain` は "Sora C++ SDK 化した" (b9b1548、2022-10-05) で廃止済みであり、
  erase はその名残として残ったもの。
- したがって「interval だけ挙動が違う」わけではなく、問題は「到達不能コードが残っている」ことだけであり、
  修正しても挙動は一切変わらない。

なお、同ブロックの `std::cout << __LINE__` の置換は issue 0014 (ParseDataChannels の std::cout デバッグ痕跡を除去する)
で扱う。0014 とは同一ブロックを編集するため、実装時に 1 つのブランチで 2 件まとめて対応してよい。

## 設計方針

到達不能な `obj.erase(it);` を削除し、`interval` の `if (ch.interval <= 0)` ブロックを
`return false;` だけにする。

```cpp
if (ch.interval <= 0) {
  std::cout << __LINE__ << std::endl;
  return false;
}
```

- 現行実装で `obj.erase(it)` は無意味なため、「erase を生かすために `return false;` を外す」案は採らない。
- 削除後は `label` / `ordered` など erase を持たない多数の項目と同じ構造になる。
- `size-min` / `size-max` の `obj.erase(it);` も同根の残骸だが、到達可能であり警告対象ではないため
  本 issue では変更しない。

## 完了条件

- `ParseDataChannels` に到達不能コードが 0 件であること (`interval` ブロックの `return false;` の後に
  `obj.erase(it);` が残っていないこと)。
- `interval` の挙動が仕様として説明できること: 0 以下ならこのインスタンスの data-channels を
  拒否して `false` を返し (`Zakuro::Run` が 2 を返し、`main` が集約してプロセスは終了コード 1 で
  終了する)、指定なしなら既定値 500、正の値ならその値を使用する。
- 到達不能コードを検出するコンパイラ警告 (clang の `-Wunreachable-code`、MSVC の `/W4` の C4702) を
  有効にしても `ParseDataChannels` で警告が出ないこと (clang で同形のコードに警告が出ることを確認済み)。

## 解決方法

`src/zakuro.cpp` の `ParseDataChannels` の `interval` ブロックから、無条件の `return false;` の
直後にあった到達不能な `obj.erase(it);` を削除した。削除後は `label` / `ordered` など
erase を持たない多数の項目と同じ構造になっている。

- 確認したこと: 削除前の `src/zakuro.cpp` を libwebrtc 提供の clang で `-Wunreachable-code` を
  付けてコンパイルすると `warning: code will never be executed [-Wunreachable-code]` が
  `obj.erase(it);` の行に出る。削除後は同じ条件で警告が出ない
- 確認したこと: `data_channels` は値渡しのコピーで、解析後にこの JSON を参照する箇所が無いため、
  erase の削除による挙動の変化は無い
- 確認したこと: `size-min` / `size-max` の `obj.erase(it);` は到達可能であり、本 issue の
  設計方針どおり変更していない

`interval` の 0 以下を拒否する経路は `test/test_config_json.py` の
`test_data_channels_error_exits_without_crash` が `interval-zero` / `interval-negative` の
2 ケースで検証している。解析に失敗すると `Zakuro::Run` が 2 を返し、`main` が集約して
プロセスは終了コード 1 で終了する。

なお「コンパイラ警告を有効にしても警告が出ないこと」は、リポジトリのビルドに警告フラグが
無いため自動検証していない (実装時に clang で手動確認した)。
