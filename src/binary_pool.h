#ifndef BINARY_POOL_H_
#define BINARY_POOL_H_

#include <algorithm>
#include <array>
#include <memory>
#include <string>

class BinaryPool {
 public:
  BinaryPool(int pool_size) {
    std::array<int, 2 * std::mt19937_64::state_size> seed;
    std::random_device r;
    std::generate(seed.begin(), seed.end(), std::ref(r));
    std::seed_seq seq(seed.begin(), seed.end());
    engine_.seed(seq);

    const int n = (pool_size + sizeof(uint64_t) - 1) / sizeof(uint64_t);
    size_ = n * sizeof(uint64_t);
    bin_.reset(new uint8_t[size_]);
    uint8_t* p = bin_.get();
    for (int i = 0; i < n; i++) {
      *(uint64_t*)p = engine_();
      p += sizeof(uint64_t);
    }
  }
  std::string Get(int min_size, int max_size) {
    int size = std::uniform_int_distribution<int>(min_size, max_size)(engine_);
    int pos = std::uniform_int_distribution<int>(0, size_ - 1)(engine_);
    std::string r;
    r.reserve(size);
    auto p = (const char*)bin_.get();
    // pos から pos + size (size_ を超えると 0 からループ) までのバイナリを返す
    while (size > 0) {
      const int start = pos % size_;
      const int n = std::min(size, size_ - start);
      r.insert(r.end(), p + start, p + start + n);
      pos += n;
      size -= n;
    }
    return r;
  }

 private:
  std::unique_ptr<uint8_t[]> bin_;
  int size_;
  std::mt19937_64 engine_;
};

#endif