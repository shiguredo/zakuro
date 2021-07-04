#ifndef Y4M_READER_H_
#define Y4M_READER_H_

#include <stdio.h>
#include <stddef.h>

#include <chrono>
#include <memory>
#include <string>

class Y4MReader {
 public:
  int Open(std::string path);

  int GetWidth() const;
  int GetHeight() const;
  int GetChromaWidth() const;
  int GetChromaHeight() const;
  int GetSize() const;

  // ms 時点のフレームデータを取得する。
  //
  // 全体のフレームを超えてた場合はループする。
  // 直前と同じフレームだった場合は何も書き込まず *updated = false にする。
  int GetFrame(std::chrono::milliseconds ms, uint8_t* data, bool* updated);

 private:
  int ReadHeader();
  int ReadFrameHeader();
  int SkipFrame();

 private:
  std::shared_ptr<FILE> file_;
  int64_t start_pos_ = 0;
  int64_t pos_ = 0;
  int64_t frame_ = 0;
  int64_t prev_frame_ = -1;
  int64_t width_ = 0;
  int64_t height_ = 0;
  int64_t fps_num_ = 0;
  int64_t fps_den_ = 0;
  int64_t aspect_h_ = 0;
  int64_t aspect_v_ = 0;
  uintmax_t file_size_ = 0;
};

#endif
