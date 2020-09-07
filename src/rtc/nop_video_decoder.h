#ifndef NOP_VIDEO_DECODER_H_
#define NOP_VIDEO_DECODER_H_

// WebRTC
#include <api/video/i420_buffer.h>
#include <api/video_codecs/video_decoder.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

class NopVideoDecoder : public webrtc::VideoDecoder {
 public:
  NopVideoDecoder() {}
  ~NopVideoDecoder() override {}

  int32_t InitDecode(const webrtc::VideoCodec* codec_settings,
                     int32_t number_of_cores) override {
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Decode(const webrtc::EncodedImage& input_image,
                 bool missing_frames,
                 int64_t render_time_ms) override {
    if (callback_ == nullptr) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }

    // 適当に小さいフレームをデコーダに渡す
    rtc::scoped_refptr<webrtc::I420Buffer> i420_buffer =
        webrtc::I420Buffer::Create(320, 240);

    webrtc::VideoFrame decoded_image =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(i420_buffer)
            .set_timestamp_rtp(input_image.Timestamp())
            .build();
    callback_->Decoded(decoded_image, absl::nullopt, absl::nullopt);

    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override { return WEBRTC_VIDEO_CODEC_OK; }
  bool PrefersLateDecoding() const override { return false; }
  const char* ImplementationName() const override { return "NOP Decoder"; }

 private:
  webrtc::DecodedImageCallback* callback_ = nullptr;
};

class NopVideoDecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  NopVideoDecoderFactory() {}
  ~NopVideoDecoderFactory() override {}

  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    std::vector<webrtc::SdpVideoFormat> supported_codecs;
    // VP8 と VP9
    supported_codecs.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
    for (const webrtc::SdpVideoFormat& format : webrtc::SupportedVP9Codecs()) {
      supported_codecs.push_back(format);
    }
    return supported_codecs;
  }

  std::unique_ptr<webrtc::VideoDecoder> CreateVideoDecoder(
      const webrtc::SdpVideoFormat& format) override {
    return std::unique_ptr<webrtc::VideoDecoder>(
        absl::make_unique<NopVideoDecoder>());
  }
};

#endif
