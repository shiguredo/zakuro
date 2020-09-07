#ifndef RTC_SOFTWARE_VIDEO_ENCODER_H_
#define RTC_SOFTWARE_VIDEO_ENCODER_H_

// WebRTC
#include <absl/memory/memory.h>
#include <absl/strings/match.h>
#include <absl/types/optional.h>
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <media/base/codec.h>
#include <media/base/h264_profile_level_id.h>
#include <media/base/media_constants.h>
#include <media/base/vp9_profile.h>
#include <media/engine/simulcast_encoder_adapter.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>
#include <rtc_base/logging.h>

#include "dynamic_h264_video_encoder.h"

class SoftwareVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  explicit SoftwareVideoEncoderFactory(std::string openh264)
      : openh264_(std::move(openh264)) {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> supported_codecs;
    supported_codecs.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
    for (const webrtc::SdpVideoFormat& format : webrtc::SupportedVP9Codecs()) {
      supported_codecs.push_back(format);
    }
    supported_codecs.push_back(webrtc::SdpVideoFormat(cricket::kAv1CodecName));
    std::vector<webrtc::SdpVideoFormat> h264_codecs = {
        CreateH264Format(webrtc::H264::kProfileBaseline,
                         webrtc::H264::kLevel3_1, "1"),
        CreateH264Format(webrtc::H264::kProfileBaseline,
                         webrtc::H264::kLevel3_1, "0"),
        CreateH264Format(webrtc::H264::kProfileConstrainedBaseline,
                         webrtc::H264::kLevel3_1, "1"),
        CreateH264Format(webrtc::H264::kProfileConstrainedBaseline,
                         webrtc::H264::kLevel3_1, "0")};
    for (const webrtc::SdpVideoFormat& format : h264_codecs) {
      supported_codecs.push_back(format);
    }
    return supported_codecs;
  }
  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override {
    if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName)) {
      return webrtc::VP8Encoder::Create();
    }

    if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName)) {
      return webrtc::VP9Encoder::Create(cricket::VideoCodec(format));
    }

    if (absl::EqualsIgnoreCase(format.name, cricket::kAv1CodecName)) {
      return webrtc::CreateLibaomAv1Encoder();
    }

    if (absl::EqualsIgnoreCase(format.name, cricket::kH264CodecName)) {
      return webrtc::DynamicH264VideoEncoder::Create(
          cricket::VideoCodec(format), openh264_);
    }

    RTC_LOG(LS_ERROR) << "Trying to created encoder of unsupported format "
                      << format.name;
    return nullptr;
  }

 private:
  static webrtc::SdpVideoFormat CreateH264Format(
      webrtc::H264::Profile profile,
      webrtc::H264::Level level,
      const std::string& packetization_mode) {
    const absl::optional<std::string> profile_string =
        webrtc::H264::ProfileLevelIdToString(
            webrtc::H264::ProfileLevelId(profile, level));
    return webrtc::SdpVideoFormat(
        cricket::kH264CodecName,
        {{cricket::kH264FmtpProfileLevelId, *profile_string},
         {cricket::kH264FmtpLevelAsymmetryAllowed, "1"},
         {cricket::kH264FmtpPacketizationMode, packetization_mode}});
  }

 private:
  std::string openh264_;
};

#endif
