#include "software_video_encoder.h"

// WebRTC
#include <absl/memory/memory.h>
#include <absl/strings/match.h>
#include <absl/types/optional.h>
#include <media/base/h264_profile_level_id.h>
#include <media/base/vp9_profile.h>
#include <media/engine/simulcast_encoder_adapter.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>
#include <rtc_base/logging.h>

#include "dynamic_h264_video_encoder.h"
#include "h264_format.h"

SoftwareVideoEncoderFactory::SoftwareVideoEncoderFactory(std::string openh264,
                                                         bool simulcast)
    : openh264_(std::move(openh264)) {
  if (simulcast) {
    internal_encoder_factory_.reset(
        new SoftwareVideoEncoderFactory(openh264_, false));
  }
}

std::vector<webrtc::SdpVideoFormat>
SoftwareVideoEncoderFactory::GetSupportedFormats() const {
  std::vector<webrtc::SdpVideoFormat> supported_codecs;
  supported_codecs.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
  for (const webrtc::SdpVideoFormat& format : webrtc::SupportedVP9Codecs()) {
    supported_codecs.push_back(format);
  }
  supported_codecs.push_back(webrtc::SdpVideoFormat(cricket::kAv1CodecName));
  std::vector<webrtc::SdpVideoFormat> h264_codecs = {
      CreateH264Format(webrtc::H264::kProfileBaseline, webrtc::H264::kLevel3_1,
                       "1"),
      CreateH264Format(webrtc::H264::kProfileBaseline, webrtc::H264::kLevel3_1,
                       "0"),
      CreateH264Format(webrtc::H264::kProfileConstrainedBaseline,
                       webrtc::H264::kLevel3_1, "1"),
      CreateH264Format(webrtc::H264::kProfileConstrainedBaseline,
                       webrtc::H264::kLevel3_1, "0")};
  for (const webrtc::SdpVideoFormat& format : h264_codecs) {
    supported_codecs.push_back(format);
  }
  return supported_codecs;
}
std::unique_ptr<webrtc::VideoEncoder>
SoftwareVideoEncoderFactory::CreateVideoEncoder(
    const webrtc::SdpVideoFormat& format) {
  if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName)) {
    return WithSimulcast(format, [](const webrtc::SdpVideoFormat& format) {
      return webrtc::VP8Encoder::Create();
    });
  }

  if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName)) {
    return WithSimulcast(format, [](const webrtc::SdpVideoFormat& format) {
      return webrtc::VP9Encoder::Create(cricket::VideoCodec(format));
    });
  }

  if (absl::EqualsIgnoreCase(format.name, cricket::kAv1CodecName)) {
    return WithSimulcast(format, [](const webrtc::SdpVideoFormat& format) {
      return webrtc::CreateLibaomAv1Encoder();
    });
  }

  if (absl::EqualsIgnoreCase(format.name, cricket::kH264CodecName)) {
    return WithSimulcast(format, [this](const webrtc::SdpVideoFormat& format) {
      return webrtc::DynamicH264VideoEncoder::Create(
          cricket::VideoCodec(format), openh264_);
    });
  }

  RTC_LOG(LS_ERROR) << "Trying to created encoder of unsupported format "
                    << format.name;
  return nullptr;
}

std::unique_ptr<webrtc::VideoEncoder>
SoftwareVideoEncoderFactory::WithSimulcast(
    const webrtc::SdpVideoFormat& format,
    std::function<std::unique_ptr<webrtc::VideoEncoder>(
        const webrtc::SdpVideoFormat&)> create) {
  if (internal_encoder_factory_) {
    return std::unique_ptr<webrtc::VideoEncoder>(
        new webrtc::SimulcastEncoderAdapter(internal_encoder_factory_.get(),
                                            format));
  } else {
    return create(format);
  }
}
