#include <duckdb.hpp>
#include <iostream>

using namespace duckdb;
using namespace std;

int main() {
    DuckDB db(nullptr);
    Connection conn(db);
    
    // テーブル作成
    conn.Query("CREATE TABLE test_json (id INTEGER, data JSON)");
    
    // テスト用のJSON文字列
    string json_str = R"({"type":"codec","id":"123"})";
    
    cout << "Original JSON: " << json_str << endl;
    
    // 1. 直接SQLで挿入
    conn.Query("INSERT INTO test_json VALUES (1, '" + json_str + "')");
    
    // 2. プリペアドステートメントで挿入
    auto prepared = conn.Prepare("INSERT INTO test_json VALUES ($1, $2)");
    auto result = prepared->Execute(2, json_str);
    
    if (result->HasError()) {
        cout << "Error: " << result->GetError() << endl;
    }
    
    // 結果を確認
    auto query_result = conn.Query("SELECT id, data FROM test_json ORDER BY id");
    query_result->Print();
    
    // JSON型の内部表現を確認
    auto type_result = conn.Query("SELECT id, typeof(data) FROM test_json ORDER BY id");
    type_result->Print();
    
    return 0;
}