#ifndef Y4M_READER_H_
#define Y4M_READER_H_

#include <stdio.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

// boost
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>

class Y4MReader {
 public:
  int Open(std::string path) {
    FILE* fp = ::fopen(path.c_str(), "rb");
    if (fp == nullptr) {
      return -1;
    }
    boost::system::error_code ec;
    file_size_ =
        boost::filesystem::file_size(boost::filesystem::path(path), ec);
    if (ec) {
      return -2;
    }

    file_.reset(fp, [](FILE* fp) { ::fclose(fp); });
    return ReadHeader();
  }

  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }
  int GetChromaWidth() const { return (width_ + 1) / 2; }
  int GetChromaHeight() const { return (height_ + 1) / 2; }
  int GetSize() const {
    return GetWidth() * GetHeight() + GetChromaWidth() * GetChromaHeight() * 2;
  }

  // ms 時点のフレームデータを取得する。
  //
  // 全体のフレームを超えてた場合はループする。
  // 直前と同じフレームだった場合は何も書き込まず *updated = false にする。
  int GetFrame(std::chrono::milliseconds ms, uint8_t* data, bool* updated) {
    // 要求されたフレーム位置を調べる
    int frame = ms.count() * fps_num_ / (1000 * fps_den_);
    if (prev_frame_ == frame) {
      // 直前と同じフレームなので何もしない
      *updated = false;
      return 0;
    }

    // 時間が巻き戻ってるのは対応しない
    if (frame < frame_) {
      return -1;
    }

    // frame の位置までフレームをスキップする
    while (frame_ < frame) {
      int r = SkipFrame();
      if (r != 0) {
        return r;
      }
    }
    int r = ReadFrameHeader();
    if (r != 0) {
      return r;
    }
    r = ::fread(data, 1, GetSize(), file_.get());
    if (r != GetSize()) {
      return -10;
    }
    pos_ += GetSize();

    // 正確にファイルの終端に来てないので何かが間違ってる
    if (pos_ > file_size_) {
      return -11;
    }

    // ファイル終端に来てたらループする
    if (pos_ == file_size_) {
      r = ::fseek(file_.get(), start_pos_, SEEK_SET);
      if (r != 0) {
        return -12;
      }
      pos_ = start_pos_;
    }

    *updated = true;
    frame_ += 1;
    prev_frame_ = frame;
    return 0;
  }

 private:
  int ReadHeader() {
    // 適当に 1KB ぐらい読んでから適切な位置にシークし直す
    std::string data;
    data.resize(1024);
    int r = ::fread(&data[0], 1, data.size(), file_.get());
    if (r <= 0) {
      return -2;
    }
    data.resize(r);
    size_t n1 = data.find('\n');
    if (n1 == std::string::npos) {
      return -3;
    }
    std::string header = data.substr(0, n1);
    std::cout << header << std::endl;

    std::vector<std::string> tokens;
    boost::split(tokens, header, boost::is_any_of(" "));
    if (tokens.empty() || tokens[0] != "YUV4MPEG2") {
      return -5;
    }
    tokens.erase(tokens.begin());
    for (const auto& token : tokens) {
      if (token.empty()) {
        return -5;
      }
      switch (token[0]) {
        case 'W':
          width_ = atoi(token.c_str() + 1);
          break;
        case 'H':
          height_ = atoi(token.c_str() + 1);
          break;
        case 'F': {
          size_t n = token.find(':');
          if (n == std::string::npos) {
            return -7;
          }
          fps_num_ = atoi(token.c_str() + 1);
          fps_den_ = atoi(token.c_str() + n + 1);
        } break;
        case 'I':
          if (token[1] != 'p') {
            return 1;
          }
          break;
        case 'A': {
          size_t n = token.find(':');
          if (n == std::string::npos) {
            return -7;
          }
          aspect_h_ = atoi(token.c_str() + 1);
          aspect_v_ = atoi(token.c_str() + n + 1);
        } break;
        case 'C':
          // 'C420jpeg' = 4:2:0 with biaxially-displaced chroma planes
          // 'C420paldv' = 4:2:0 with vertically-displaced chroma planes
          // 'C420' = 4:2:0 with coincident chroma planes
          // 'C422' = 4:2:2
          // 'C444' = 4:4:4
          if (token != "C420jpeg" && token != "C420paldv" && token != "C420") {
            return 2;
          }
          break;
        case 'X':
          break;
        default:
          return -8;
      }
    }

    if (width_ == 0 || height_ == 0 || fps_num_ == 0) {
      return -9;
    }

    r = ::fseek(file_.get(), n1 + 1, SEEK_SET);
    if (r != 0) {
      return -10;
    }

    start_pos_ = n1 + 1;
    pos_ = n1 + 1;
    frame_ = 0;
    prev_frame_ = -1;

    return 0;
  }

  int ReadFrameHeader() {
    // 最初の5バイトはFRAME
    std::string frame;
    frame.resize(5);
    int r = ::fread(&frame[0], 1, 5, file_.get());
    if (r != 5) {
      return -1;
    }
    if (frame != "FRAME") {
      return -2;
    }
    pos_ += 5;

    // 以降は \n まで読み飛ばす。
    // 1KB 読んでも \n が見つからなければエラー
    bool found = false;
    for (int i = 0; i < 1024; i++) {
      char c;
      int r = ::fread(&c, 1, 1, file_.get());
      if (r != 1) {
        return -3;
      }
      pos_ += 1;

      if (c == '\n') {
        found = true;
        break;
      }
    }
    if (!found) {
      return -4;
    }

    return 0;
  }
  int SkipFrame() {
    int r = ReadFrameHeader();
    if (r != 0) {
      return r;
    }
    // 1フレーム分のデータを読み飛ばす
    r = ::fseek(file_.get(), GetSize(), SEEK_CUR);
    if (r != 0) {
      return -5;
    }
    pos_ += GetSize();

    // 正確にファイルの終端に来てないので何かが間違ってる
    if (pos_ > file_size_) {
      return -6;
    }

    // ファイル終端に来てたらループする
    if (pos_ == file_size_) {
      r = ::fseek(file_.get(), start_pos_, SEEK_SET);
      if (r != 0) {
        return -7;
      }
      pos_ = start_pos_;
    }
    frame_ += 1;
    return 0;
  }

 private:
  std::shared_ptr<FILE> file_;
  int start_pos_ = 0;
  int pos_ = 0;
  int frame_ = 0;
  int prev_frame_ = -1;
  int width_ = 0;
  int height_ = 0;
  int fps_num_ = 0;
  int fps_den_ = 0;
  int aspect_h_ = 0;
  int aspect_v_ = 0;
  uintmax_t file_size_ = 0;
};

#endif
