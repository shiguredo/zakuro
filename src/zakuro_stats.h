#ifndef ZAKURO_STATS_H_
#define ZAKURO_STATS_H_

#include <chrono>
#include <string>
#include <thread>

#include "virtual_client.h"

class ZakuroStats {
 public:
  void Set(int id,
           const std::string& name,
           const std::vector<VirtualClientStats>& stats) {
    std::lock_guard<std::mutex> guard(m_);
    auto& d = data_[id];
    d.id = id;
    d.name = name;
    d.stats = stats;
    d.last_updated_at = std::chrono::steady_clock::now();
  }

  struct Data {
    int id;
    std::string name;
    std::vector<VirtualClientStats> stats;
    std::chrono::steady_clock::time_point last_updated_at;
  };

  std::map<int, Data> Get() const {
    std::lock_guard<std::mutex> guard(m_);
    return data_;
  }

 private:
  std::map<int, Data> data_;
  mutable std::mutex m_;
};

#endif
