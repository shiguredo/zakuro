#include "fake_video_capturer.h"

// WebRTC
#include <modules/video_capture/video_capture_defines.h>
#include <rtc_base/logging.h>
#include <third_party/libyuv/include/libyuv.h>

#include "embedded_binary.h"

FakeVideoCapturer::FakeVideoCapturer(FakeVideoCapturerConfig config)
    : sora::ScalableVideoTrackSource(config), config_(config) {
  StartCapture();
}

FakeVideoCapturer::~FakeVideoCapturer() {
  StopCapture();
}

void FakeVideoCapturer::StartCapture() {
  StopCapture();
  stopped_ = false;
  started_at_ = std::chrono::high_resolution_clock::now();
  thread_.reset(new std::thread([this]() {
    image_.create(config_.width, config_.height, BL_FORMAT_PRGB32);
    frame_ = 0;
    {
      BLFontFace face;
      BLFontData data;
      auto content = EmbeddedBinary::Get(RESOURCE_KOSUGI_REGULAR_TTF);
      data.create_from_data((const uint8_t*)content.ptr, content.size);
      BLResult err = face.create_from_data(data, 0);

      // We must handle a possible error returned by the loader.
      if (err) {
        //printf("Failed to load a font-face (err=%u)\n", err);
        return;
      }

      base_font_.create_from_face(face, config_.height * 0.08);
      bipbop_font_.create_from_face(face, base_font_.size() * 2.5);
      stats_font_.create_from_face(face, base_font_.size() * 0.5);
    }
    if (config_.type == FakeVideoCapturerConfig::Type::Y4MFile) {
      int r = y4m_reader_.Open(config_.y4m_path);
      if (r != 0) {
        return;
      }
      y4m_buffer_ = webrtc::I420Buffer::Create(y4m_reader_.GetWidth(),
                                               y4m_reader_.GetHeight());
    }

    while (!stopped_) {
      auto now = std::chrono::high_resolution_clock::now();
      webrtc::scoped_refptr<webrtc::I420Buffer> buffer;

      if (config_.type == FakeVideoCapturerConfig::Type::Safari ||
          config_.type == FakeVideoCapturerConfig::Type::Sandstorm ||
          config_.type == FakeVideoCapturerConfig::Type::External) {
        UpdateImage(now);

        BLImageData data;
        BLResult result = image_.get_data(&data);
        if (result != BL_SUCCESS) {
          std::this_thread::sleep_until(now + std::chrono::milliseconds(16));
          continue;
        }

        buffer = webrtc::I420Buffer::Create(config_.width, config_.height);

        libyuv::ABGRToI420((const uint8_t*)data.pixel_data, data.stride,
                           buffer->MutableDataY(), buffer->StrideY(),
                           buffer->MutableDataU(), buffer->StrideU(),
                           buffer->MutableDataV(), buffer->StrideV(),
                           config_.width, config_.height);
      } else if (config_.type == FakeVideoCapturerConfig::Type::Y4MFile) {
        bool updated = false;
        int r = y4m_reader_.GetFrame(
            std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                                  started_at_),
            y4m_buffer_->MutableDataY(), &updated);
        if (r != 0) {
          RTC_LOG(LS_ERROR) << "Failed to Y4MReader::GetFrame: result=" << r;
          return;
        }
        buffer = webrtc::I420Buffer::Create(config_.width, config_.height);
        buffer->ScaleFrom(*y4m_buffer_);
      }

      int64_t timestamp_us =
          std::chrono::duration_cast<std::chrono::microseconds>(now -
                                                                started_at_)
              .count();

      bool captured =
          OnCapturedFrame(webrtc::VideoFrame::Builder()
                              .set_video_frame_buffer(buffer)
                              .set_rotation(webrtc::kVideoRotation_0)
                              .set_timestamp_us(timestamp_us)
                              .build());

      if (captured) {
        std::this_thread::sleep_until(
            now + std::chrono::milliseconds(1000 / config_.fps - 2));
        frame_ += 1;
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }));
}

void FakeVideoCapturer::StopCapture() {
  if (thread_) {
    stopped_ = true;
    thread_->join();
    thread_.reset();
  }
}

void FakeVideoCapturer::UpdateImage(
    std::chrono::high_resolution_clock::time_point now) {
  if (config_.type == FakeVideoCapturerConfig::Type::Safari) {
    BLContext ctx(image_);

    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all();

    ctx.save();
    DrawTexts(ctx, now);
    ctx.restore();

    ctx.save();
    DrawAnimations(ctx, now);
    ctx.restore();

    ctx.save();
    DrawBoxes(ctx, now);
    ctx.restore();

    ctx.end();
  } else if (config_.type == FakeVideoCapturerConfig::Type::Sandstorm) {
    //auto now = std::chrono::high_resolution_clock::now();

    // ランダムピクセル
    BLImageData data;
    image_.get_data(&data);
    for (int y = 0; y < data.size.h; y++) {
      auto p = (uint32_t*)((uint8_t*)data.pixel_data + y * data.stride);
      for (int x = 0; x < data.size.w; x++) {
        p[x] = 0xff000000 | random_.Get();
      }
    }

    //auto now2 = std::chrono::high_resolution_clock::now();
    //RTC_LOG(LS_INFO) << "sandstorm "
    //                 << std::chrono::duration_cast<std::chrono::milliseconds>(
    //                        now2 - now)
    //                        .count()
    //                 << " ms";
  } else if (config_.type == FakeVideoCapturerConfig::Type::External) {
    BLContext ctx(image_);

    config_.render(ctx, now);
  }
}
void FakeVideoCapturer::DrawTexts(
    BLContext& ctx,
    std::chrono::high_resolution_clock::time_point now) {
  auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_)
          .count();

  ctx.set_fill_style(BLRgba32(0xFFFFFFFF));

  int width = config_.width;
  int height = config_.height;
  int fps = config_.fps;

  auto pad = [](char c, int d, int v) {
    std::string s;
    for (int i = 0; i < d || v != 0; i++) {
      if (i != 0 && v == 0) {
        s = c + s;
        continue;
      }

      s = (char)('0' + (v % 10)) + s;
      v /= 10;
    }
    return s;
  };

  {
    std::string text = pad('0', 2, ms / (60 * 60 * 1000)) + ':' +
                       pad('0', 2, ms / (60 * 1000) % 60) + ':' +
                       pad('0', 2, ms / 1000 % 60) + '.' +
                       pad('0', 3, ms % 1000);
    ctx.fill_utf8_text(BLPoint(width * 0.05, height * .15), base_font_,
                       text.c_str());
  }

  {
    std::string text = pad('0', 6, frame_);
    ctx.fill_utf8_text(BLPoint(width * 0.05, height * .15 + base_font_.size()),
                       base_font_, text.c_str());
  }

  {
    std::string text = "Requested frame rate: " + std::to_string(fps) + " fps";
    ctx.fill_utf8_text(BLPoint(width * 0.45, height * 0.75), stats_font_,
                       text.c_str());
  }
  {
    std::string text =
        "Size: " + std::to_string(width) + " x " + std::to_string(height);
    ctx.fill_utf8_text(BLPoint(width * 0.45, height * 0.75 + stats_font_.size()),
                       stats_font_, text.c_str());
  }

  {
    int m = frame_ % 60;
    if (m < 15) {
      ctx.set_fill_style(BLRgba32(0, 255, 255));
      ctx.fill_utf8_text(BLPoint(width * 0.6, height * 0.6), bipbop_font_, "Bip");
    } else if (m >= 30 && m < 45) {
      ctx.set_fill_style(BLRgba32(255, 255, 0));
      ctx.fill_utf8_text(BLPoint(width * 0.6, height * 0.6), bipbop_font_, "Bop");
    }
  }
}
void FakeVideoCapturer::DrawAnimations(
    BLContext& ctx,
    std::chrono::high_resolution_clock::time_point now) {
  int width = config_.width;
  int height = config_.height;
  int fps = config_.fps;

  float pi = 3.14159;
  ctx.translate(width * 0.8, height * 0.3);
  ctx.rotate(-pi / 2);
  ctx.set_fill_style(BLRgba32(255, 255, 255));
  ctx.fill_pie(0, 0, width * 0.09, 0, 2 * pi);

  ctx.set_fill_style(BLRgba32(160, 160, 160));
  ctx.fill_pie(0, 0, width * 0.09, 0, (frame_ % fps) / (float)fps * 2 * 3.14159);
}

void FakeVideoCapturer::DrawBoxes(
    BLContext& ctx,
    std::chrono::high_resolution_clock::time_point now) {
  int width = config_.width;
  int height = config_.height;

  float size = width * 0.035;
  float top = height * 0.6;

  ctx.set_fill_style(BLRgba32(255, 255, 255));
  ctx.set_stroke_style(BLRgba32(255, 255, 255));

  ctx.set_stroke_width(2);
  BLArray<double> dash;
  // 本当はこれで点線になるはずだが、現在動かない
  // https://github.com/blend2d/blend2d/issues/48
  dash.resize(2, 6);
  ctx.set_stroke_dash_array(dash);
  ctx.stroke_rect(2, 2, width - 4, height - 4);

  ctx.set_stroke_dash_array(BLArray<double>());
  ctx.stroke_line(0, top + size, width, top + size);

  ctx.set_stroke_width(2);
  float left = size;
  for (int i = 0; i < size / 4; i++) {
    ctx.stroke_line(left + 4 * i, top, left + 4 * i, top + size);
  }
  left += size + 2;
  for (int i = 0; i < size / 4; i++) {
    ctx.stroke_line(left, top + 4 * i, left + size, top + 4 * i);
  }

  ctx.set_stroke_width(3);
  left += size + 2;
  for (int i = 0; i < size / 8; i++) {
    ctx.stroke_line(left + 8 * i, top, left + 8 * i, top + size);
  }
  left += size + 2;
  for (int i = 0; i < size / 8; i++) {
    ctx.stroke_line(left, top + 8 * i, left + size, top + 8 * i);
  }

  const BLRgba32 colors[] = {
      BLRgba32(255, 255, 255), BLRgba32(255, 255, 0), BLRgba32(0, 255, 255),
      BLRgba32(0, 128, 0),     BLRgba32(255, 0, 255), BLRgba32(255, 0, 0),
      BLRgba32(0, 0, 255),
  };
  left = size;
  for (const auto& color : colors) {
    ctx.set_fill_style(color);
    ctx.fill_rect(left, top + size + 2, size + 1, size + 1);
    left += size + 1;
  }
}
