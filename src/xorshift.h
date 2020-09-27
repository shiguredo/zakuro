#ifndef RANDOM_H_
#define RANDOM_H_

#include <cstdint>

class Xorshift {
 public:
  Xorshift() {}
  Xorshift(uint32_t x, uint32_t y, uint32_t z, uint32_t w)
      : x(x), y(y), z(z), w(w) {}
  uint32_t Get();

 private:
  uint32_t x = 123456789;
  uint32_t y = 362436069;
  uint32_t z = 521288629;
  uint32_t w = 88675123;
};

#endif
