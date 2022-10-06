#include "zakuro_version.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sora/version.h>

// バージョンやコミットハッシュ情報
// 通常は外から渡すが、渡されていなかった場合の対応
#ifndef ZAKURO_VERSION
#define ZAKURO_VERSION "internal-build"
#endif

#ifndef ZAKURO_COMMIT_SHORT
#define ZAKURO_COMMIT_SHORT "unknown"
#endif

#define ZAKURO_NAME \
  "WebRTC Load Testing Tool Zakuro " ZAKURO_VERSION " (" ZAKURO_COMMIT_SHORT ")"

#if defined(WEBRTC_READABLE_VERSION) && defined(WEBRTC_COMMIT_SHORT) && \
    defined(WEBRTC_BUILD_VERSION)

#define LIBWEBRTC_NAME                                                 \
  "Shiguredo-Build " WEBRTC_READABLE_VERSION " (" WEBRTC_BUILD_VERSION \
  " " WEBRTC_COMMIT_SHORT ")"

#else

#define LIBWEBRTC_NAME "WebRTC custom build"

#endif

std::string ZakuroVersion::GetClientName() {
  return ZAKURO_NAME;
}

std::string ZakuroVersion::GetLibwebrtcName() {
  return LIBWEBRTC_NAME;
}

std::string ZakuroVersion::GetEnvironmentName() {
  return sora::Version::GetEnvironmentName();
}
