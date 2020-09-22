#include "embedded_binary.h"

#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

EmbeddedBinaryContent EmbeddedBinary::kosugi_ttf() {
#ifdef __APPLE__
  size_t size;
  const void* ptr =
      getsectiondata(&_mh_execute_header, "__DATA", "__kosugi_ttf", &size);
#else
  extern const unsigned char _binary_Kosugi_Regular_ttf_start[];
  extern const unsigned char _binary_Kosugi_Regular_ttf_end[];
  const void* ptr = _binary_Kosugi_Regular_ttf_start;
  size_t size =
      _binary_Kosugi_Regular_ttf_end - _binary_Kosugi_Regular_ttf_start;
#endif
  EmbeddedBinaryContent content;
  content.ptr = ptr;
  content.size = size;
  return content;
}
