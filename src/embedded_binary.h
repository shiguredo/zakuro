#ifndef EMBEDDED_BINARY_H_
#define EMBEDDED_BINARY_H_

#include <cstddef>

#include "embedded_binary.generated.h"

struct EmbeddedBinaryContent {
  const void* ptr;
  size_t size;
};

class EmbeddedBinary {
 public:
  static EmbeddedBinaryContent Get(int id);
};

#endif
