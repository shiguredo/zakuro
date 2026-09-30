#ifndef ZAKURO_VERSION_H_
#define ZAKURO_VERSION_H_

#include <string>

class ZakuroVersion {
 public:
  static std::string GetClientName();
  static std::string GetLibwebrtcName();
  static std::string GetEnvironmentName();
  static std::string GetVersion();
  static std::string GetSoraCppSdkVersion();
  static std::string GetWebRTCVersion();
};

#endif  // ZAKURO_VERSION_H_
