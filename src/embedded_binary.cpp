#include "embedded_binary.h"

#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

#include "embedded_binary.generated.priv.h"

EmbeddedBinaryContent EmbeddedBinary::Get(int id) {
#ifdef __APPLE__
  size_t size;
  const void* ptr =
      getsectiondata(&_mh_execute_header, "__DATA",
                     EMBEDDED_BINARY_RESOURCE_NAMES_MACOS[id], &size);
#else
  const void* ptr = EMBEDDED_BINARY_RESOURCE_STARTS_LINUX[id];
  size_t size = EMBEDDED_BINARY_RESOURCE_ENDS_LINUX[id] -
                EMBEDDED_BINARY_RESOURCE_STARTS_LINUX[id];
#endif
  EmbeddedBinaryContent content;
  content.ptr = ptr;
  content.size = size;
  return content;
}
