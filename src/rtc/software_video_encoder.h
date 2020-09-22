#ifndef RTC_SOFTWARE_VIDEO_ENCODER_H_
#define RTC_SOFTWARE_VIDEO_ENCODER_H_

// WebRTC
#include <api/video_codecs/sdp_video_format.h>
#include <api/video_codecs/video_encoder.h>
#include <api/video_codecs/video_encoder_factory.h>
#include <media/base/codec.h>
#include <media/base/media_constants.h>

class SoftwareVideoEncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  explicit SoftwareVideoEncoderFactory(std::string openh264, bool simulcast);

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(
      const webrtc::SdpVideoFormat& format) override;

 private:
  std::unique_ptr<webrtc::VideoEncoder> WithSimulcast(
      const webrtc::SdpVideoFormat& format,
      std::function<std::unique_ptr<webrtc::VideoEncoder>(
          const webrtc::SdpVideoFormat&)> create);

 private:
  std::unique_ptr<SoftwareVideoEncoderFactory> internal_encoder_factory_;
  std::string openh264_;
};

#endif
