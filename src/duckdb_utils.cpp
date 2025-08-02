#include "duckdb_utils.h"
#include <stdexcept>

namespace duckdb_utils {

Transaction::Transaction(duckdb_connection conn)
    : conn_(conn), committed_(false), active_(false) {
  Result result;
  result.set_valid();
  if (duckdb_query(conn_, "BEGIN TRANSACTION", result.get()) == DuckDBError) {
    std::string error_msg = result.error();
    // resultは自動的に破棄される
    throw std::runtime_error("Failed to begin transaction: " + error_msg);
  }
  active_ = true;
}

Transaction::~Transaction() {
  if (active_ && !committed_) {
    Rollback();
  }
}

void Transaction::Commit() {
  if (!active_) {
    throw std::runtime_error("Transaction is not active");
  }

  Result result;
  result.set_valid();
  if (duckdb_query(conn_, "COMMIT", result.get()) == DuckDBError) {
    std::string error_msg = result.error();
    // resultは自動的に破棄される
    active_ = false;  // トランザクションは無効になる
    throw std::runtime_error("Failed to commit transaction: " + error_msg);
  }
  committed_ = true;
  active_ = false;
}

void Transaction::Rollback() {
  if (!active_) {
    return;
  }

  Result result;
  result.set_valid();
  duckdb_query(conn_, "ROLLBACK", result.get());
  active_ = false;
}

}  // namespace duckdb_utils