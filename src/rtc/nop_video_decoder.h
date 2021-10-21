#ifndef NOP_VIDEO_DECODER_H_
#define NOP_VIDEO_DECODER_H_

// WebRTC
#include <api/video_codecs/video_codec.h>
#include <api/video_codecs/video_decoder.h>
#include <api/video_codecs/video_decoder_factory.h>

class NopVideoDecoder : public webrtc::VideoDecoder {
 public:
  bool Configure(const Settings& settings) override;
  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms) override;

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override;

  int32_t Release() override;
  const char* ImplementationName() const override;

 private:
  webrtc::DecodedImageCallback* callback_ = nullptr;
};

class NopVideoDecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;
  std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(
      const webrtc::SdpVideoFormat& format) override;
};

#endif
