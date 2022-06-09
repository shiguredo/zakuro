#ifndef FAKE_VIDEO_CAPTURER_H_
#define FAKE_VIDEO_CAPTURER_H_

#include <memory>
#include <thread>

// WebRTC
#include <api/video/i420_buffer.h>
#include <rtc_base/ref_counted_object.h>

// Blend2D
#include <blend2d.h>

#include "rtc/scalable_track_source.h"
#include "xorshift.h"
#include "y4m_reader.h"

struct FakeVideoCapturerConfig {
  int width;
  int height;
  int fps;
  enum class Type {
    Safari,
    Sandstorm,
    Y4MFile,
    External,
  };
  Type type = Type::Safari;
  std::string y4m_path;
  std::function<void(BLContext&,
                     std::chrono::high_resolution_clock::time_point)>
      render;
};

class FakeVideoCapturer : public ScalableVideoTrackSource {
  FakeVideoCapturer(FakeVideoCapturerConfig config);
  friend class rtc::RefCountedObject<FakeVideoCapturer>;

 public:
  static rtc::scoped_refptr<FakeVideoCapturer> Create(
      FakeVideoCapturerConfig config) {
    return rtc::make_ref_counted<FakeVideoCapturer>(std::move(config));
  }

  ~FakeVideoCapturer();

  void StartCapture();
  void StopCapture();

 private:
  void UpdateImage(std::chrono::high_resolution_clock::time_point now);
  void DrawTexts(BLContext& ctx,
                 std::chrono::high_resolution_clock::time_point now);
  void DrawAnimations(BLContext& ctx,
                      std::chrono::high_resolution_clock::time_point now);

  void DrawBoxes(BLContext& ctx,
                 std::chrono::high_resolution_clock::time_point now);

 private:
  std::unique_ptr<std::thread> thread_;
  FakeVideoCapturerConfig config_;
  std::atomic_bool stopped_{false};
  std::chrono::high_resolution_clock::time_point started_at_;

  BLImage image_;
  BLFont base_font_;
  BLFont bipbop_font_;
  BLFont stats_font_;
  uint32_t frame_;
  //Random<uint32_t> random_{0, 256 * 256 * 256 - 1};
  Xorshift random_;
  Y4MReader y4m_reader_;
  rtc::scoped_refptr<webrtc::I420Buffer> y4m_buffer_;
};

#endif
