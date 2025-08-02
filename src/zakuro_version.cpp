#include "zakuro_version.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sora/version.h>

#include "zakuro_version.gen.h"

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

std::string ZakuroVersion::GetVersion() {
  return ZAKURO_VERSION;
}

std::string ZakuroVersion::GetSoraCppSdkVersion() {
  std::string full_version = sora::Version::GetClientName();
  // "Sora C++ SDK 2025.3.1 (7e86e6e5)" から "2025.3.1" を抽出
  size_t start = full_version.find("SDK ");
  if (start != std::string::npos) {
    start += 4;  // "SDK " の長さ
    size_t end = full_version.find(" ", start);
    if (end != std::string::npos) {
      return full_version.substr(start, end - start);
    }
  }
  return full_version;  // 抽出に失敗した場合は元の文字列を返す
}

std::string ZakuroVersion::GetWebRTCVersion() {
  // WEBRTC_BUILD_VERSION が定義されていればそれを返す
#ifdef WEBRTC_BUILD_VERSION
  return WEBRTC_BUILD_VERSION;
#else
  // 定義されていない場合は GetLibwebrtcName() から抽出を試みる
  std::string full_version = GetLibwebrtcName();
  // "Shiguredo-Build M136.7103@{#0} (136.7103.0.0 2c8f5be6)" から "136.7103.0.0" を抽出
  size_t start = full_version.find("(");
  if (start != std::string::npos) {
    start += 1;
    size_t end = full_version.find(" ", start);
    if (end != std::string::npos) {
      return full_version.substr(start, end - start);
    }
  }
  return full_version;  // 抽出に失敗した場合は元の文字列を返す
#endif
}
