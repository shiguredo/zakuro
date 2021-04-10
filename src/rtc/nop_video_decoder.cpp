#include "nop_video_decoder.h"

// WebRTC
#include <api/video/i420_buffer.h>
#include <modules/video_coding/codecs/av1/libaom_av1_encoder.h>
#include <modules/video_coding/codecs/h264/include/h264.h>
#include <modules/video_coding/codecs/vp8/include/vp8.h>
#include <modules/video_coding/codecs/vp9/include/vp9.h>

#include "h264_format.h"

int32_t NopVideoDecoder::InitDecode(const webrtc::VideoCodec* codec_settings,
                                    int32_t number_of_cores) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t NopVideoDecoder::Decode(const webrtc::EncodedImage& input_image,
                                bool missing_frames,
                                int64_t render_time_ms) {
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

std::unique_ptr<webrtc::VideoDecoder>
NopVideoDecoderFactory::CreateVideoDecoder(
    const webrtc::SdpVideoFormat& format) {
  return std::unique_ptr<webrtc::VideoDecoder>(
      absl::make_unique<NopVideoDecoder>());
}
