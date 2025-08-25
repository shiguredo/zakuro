#ifndef DUCKDB_UTILS_H_
#define DUCKDB_UTILS_H_

#include <duckdb.h>
#include <functional>
#include <memory>
#include <string>

namespace duckdb_utils {

// DuckDB文字列値のRAIIラッパー
class StringValue {
 public:
  explicit StringValue(char* value) : value_(value) {}
  ~StringValue() {
    if (value_) {
      duckdb_free(value_);
    }
  }

  // コピー禁止
  StringValue(const StringValue&) = delete;
  StringValue& operator=(const StringValue&) = delete;

  // ムーブコンストラクタ
  StringValue(StringValue&& other) noexcept : value_(other.value_) {
    other.value_ = nullptr;
  }

  // ムーブ代入演算子
  StringValue& operator=(StringValue&& other) noexcept {
    if (this != &other) {
      if (value_) {
        duckdb_free(value_);
      }
      value_ = other.value_;
      other.value_ = nullptr;
    }
    return *this;
  }

  const char* get() const { return value_; }
  operator const char*() const { return value_; }

 private:
  char* value_;
};

// DuckDB結果のRAIIラッパー
class Result {
 public:
  Result() : result_{} {}
  ~Result() {
    if (valid_) {
      duckdb_destroy_result(&result_);
    }
  }

  // コピー・ムーブ禁止
  Result(const Result&) = delete;
  Result& operator=(const Result&) = delete;
  Result(Result&&) = delete;
  Result& operator=(Result&&) = delete;

  duckdb_result* get() { return &result_; }
  const duckdb_result* get() const { return &result_; }

  void set_valid() { valid_ = true; }
  bool is_valid() const { return valid_; }

  // エラーメッセージを取得
  std::string error() const {
    if (!valid_)
      return "";
    const char* err = duckdb_result_error(const_cast<duckdb_result*>(&result_));
    return err ? err : "";
  }

  // 行数を取得
  idx_t row_count() const {
    return valid_ ? duckdb_row_count(const_cast<duckdb_result*>(&result_)) : 0;
  }

  // カラム数を取得
  idx_t column_count() const {
    return valid_ ? duckdb_column_count(const_cast<duckdb_result*>(&result_))
                  : 0;
  }

 private:
  duckdb_result result_;
  bool valid_ = false;
};

// DuckDBトランザクションのRAIIラッパー
class Transaction {
 public:
  explicit Transaction(duckdb_connection conn);
  ~Transaction();

  // コピー・ムーブ禁止
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  Transaction(Transaction&&) = delete;
  Transaction& operator=(Transaction&&) = delete;

  void Commit();
  void Rollback();

 private:
  duckdb_connection conn_;
  bool committed_;
  bool active_;
};

// DuckDBプリペアドステートメントのRAIIラッパー
class PreparedStatement {
 public:
  PreparedStatement() : stmt_(nullptr) {}
  ~PreparedStatement() {
    if (stmt_) {
      duckdb_destroy_prepare(&stmt_);
    }
  }

  // コピー禁止
  PreparedStatement(const PreparedStatement&) = delete;
  PreparedStatement& operator=(const PreparedStatement&) = delete;

  // ムーブコンストラクタ
  PreparedStatement(PreparedStatement&& other) noexcept : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
  }

  // ムーブ代入演算子
  PreparedStatement& operator=(PreparedStatement&& other) noexcept {
    if (this != &other) {
      if (stmt_) {
        duckdb_destroy_prepare(&stmt_);
      }
      stmt_ = other.stmt_;
      other.stmt_ = nullptr;
    }
    return *this;
  }

  duckdb_prepared_statement* get() { return &stmt_; }
  duckdb_prepared_statement get_raw() const { return stmt_; }

  // エラーメッセージを取得
  std::string error() const {
    if (!stmt_)
      return "";
    const char* err = duckdb_prepare_error(stmt_);
    return err ? err : "";
  }

 private:
  duckdb_prepared_statement stmt_;
};

// クエリ実行ヘルパー関数
inline bool ExecuteQuery(duckdb_connection conn,
                         const std::string& query,
                         Result& result) {
  result.set_valid();
  return duckdb_query(conn, query.c_str(), result.get()) == DuckDBSuccess;
}

// クエリ実行ヘルパー関数（結果を破棄）
inline bool ExecuteQuery(duckdb_connection conn, const std::string& query) {
  Result result;
  return ExecuteQuery(conn, query, result);
}

// プリペアドステートメント作成ヘルパー
inline bool Prepare(duckdb_connection conn,
                    const std::string& query,
                    PreparedStatement& stmt) {
  return duckdb_prepare(conn, query.c_str(), stmt.get()) == DuckDBSuccess;
}

// プリペアドステートメント実行ヘルパー
inline bool ExecutePrepared(duckdb_prepared_statement stmt, Result& result) {
  result.set_valid();
  return duckdb_execute_prepared(stmt, result.get()) == DuckDBSuccess;
}

}  // namespace duckdb_utils

#endif  // DUCKDB_UTILS_H_