#include "nop_video_decoder.h"

// WebRTC
#include <api/video/i420_buffer.h>
#include <media/base/media_constants.h>
#include <modules/video_coding/include/video_error_codes.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

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

std::vector<webrtc::SdpVideoFormat>
NopVideoDecoderFactory::GetSupportedFormats() const {
  std::vector<webrtc::SdpVideoFormat> supported_codecs;
  supported_codecs.push_back(webrtc::SdpVideoFormat(webrtc::kVp8CodecName));
  for (const webrtc::SdpVideoFormat& format : webrtc::SupportedVP9Codecs()) {
    supported_codecs.push_back(format);
  }
  supported_codecs.push_back(webrtc::SdpVideoFormat(webrtc::kAv1CodecName));
  std::vector<webrtc::SdpVideoFormat> h264_codecs = {
      CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                       webrtc::H264Level::kLevel3_1, "1"),
      CreateH264Format(webrtc::H264Profile::kProfileBaseline,
                       webrtc::H264Level::kLevel3_1, "0"),
      CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                       webrtc::H264Level::kLevel3_1, "1"),
      CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline,
                       webrtc::H264Level::kLevel3_1, "0")};
  for (const webrtc::SdpVideoFormat& format : h264_codecs) {
    supported_codecs.push_back(format);
  }
  supported_codecs.push_back(webrtc::SdpVideoFormat(webrtc::kH265CodecName));
  return supported_codecs;
}

std::unique_ptr<webrtc::VideoDecoder> NopVideoDecoderFactory::Create(
    const webrtc::Environment& env,
    const webrtc::SdpVideoFormat& format) {
  return std::unique_ptr<webrtc::VideoDecoder>(
      absl::make_unique<NopVideoDecoder>());
}
