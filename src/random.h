#ifndef RANDOM_H_
#define RANDOM_H_

#include <algorithm>
#include <array>
#include <random>
#include <utility>

// ランダムな値を生成するテンプレートクラス
template <class T>
class Random {
  T min_;
  T max_;
  std::mt19937_64 engine_;

 public:
  Random(T min, T max) : min_(min), max_(max) {
    // 擬似乱数生成器の状態シーケンスのサイズ分、
    // シードを用意する
    std::array<std::seed_seq::result_type, std::mt19937_64::state_size>
        seed_data;

    // 非決定的な乱数でシード列を構築する
    std::random_device seed_gen;
    std::generate(seed_data.begin(), seed_data.end(), std::ref(seed_gen));

    std::seed_seq seq(seed_data.begin(), seed_data.end());
    engine_.seed(seq);
  }

  T GetRandomValue() {
    return std::uniform_int_distribution<T>(min_, max_)(engine_);
  }
};

class Xorshift {
 public:
  uint32_t Get(void) {
    uint32_t t;
    t = x ^ (x << 11);
    x = y;
    y = z;
    z = w;
    return w = (w ^ (w >> 19)) ^ (t ^ (t >> 8));
  }

 private:
  uint32_t x = 123456789;
  uint32_t y = 362436069;
  uint32_t z = 521288629;
  uint32_t w = 88675123;
};

#endif
