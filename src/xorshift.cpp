#include "xorshift.h"

#include <algorithm>
#include <array>
#include <random>
#include <utility>

uint32_t Xorshift::Get() {
  uint32_t t;
  t = x ^ (x << 11);
  x = y;
  y = z;
  z = w;
  return w = (w ^ (w >> 19)) ^ (t ^ (t >> 8));
}
