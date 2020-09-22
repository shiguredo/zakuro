#ifndef EMBEDDED_BINARY_H_
#define EMBEDDED_BINARY_H_

#include <cstddef>

struct EmbeddedBinaryContent {
  const void* ptr;
  size_t size;
};

class EmbeddedBinary {
 public:
  static EmbeddedBinaryContent kosugi_ttf();
};

#endif
