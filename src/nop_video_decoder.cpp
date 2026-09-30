#include "nop_video_decoder.h"

// WebRTC
#include <api/video/i420_buffer.h>
#include <modules/video_coding/include/video_error_codes.h>

bool NopVideoDecoder::Configure(const Settings& settings) {
  return true;
}

int32_t NopVideoDecoder::Decode(const webrtc::EncodedImage& input_image,
                                bool missing_frames,
                                int64_t render_time_ms) {
  if (callback_ == nullptr) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  // 適当に小さいフレームをデコーダに渡す
  webrtc::scoped_refptr<webrtc::I420Buffer> i420_buffer =
      webrtc::I420Buffer::Create(320, 240);

  webrtc::VideoFrame decoded_image =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(i420_buffer)
          .set_timestamp_rtp(input_image.RtpTimestamp())
          .build();
  callback_->Decoded(decoded_image, std::nullopt, std::nullopt);

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NopVideoDecoder::RegisterDecodeCompleteCallback(
    webrtc::DecodedImageCallback* callback) {
  callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NopVideoDecoder::Release() {
  return WEBRTC_VIDEO_CODEC_OK;
}
const char* NopVideoDecoder::ImplementationName() const {
  return "NOP Decoder";
}
