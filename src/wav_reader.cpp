#include "wav_reader.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

int WavReader::Load(std::string path) {
  std::string buf;
  {
    std::stringstream ss;
    // WAV はバイナリ形式のため、テキストモードで開くと Windows で
    // CRLF 変換によりデータが壊れる
    std::ifstream fin(path, std::ios::binary);
    ss << fin.rdbuf();
    buf = ss.str();
  }
  // 実ファイルサイズと整合する巨大なチャンクサイズの WAV では、Load(ptr, size) が
  // サンプル用の領域を確保しようとして失敗しうる。未捕捉のまま外へ出すと
  // std::terminate に至るため、ここで捕捉して読み込み失敗として扱う
  try {
    return Load(buf.c_str(), buf.size());
  } catch (const std::exception& e) {
    std::cerr << "failed to load WAV: path=" << path << " what=" << e.what()
              << std::endl;
    return -1;
  }
}

static bool ReadChunk(const void* p,
                      size_t size,
                      std::string& name,
                      size_t& chunk_size,
                      const void*& chunk_data) {
  if (size < 8) {
    return false;
  }
  name = std::string((const char*)p, (const char*)p + 4);

  const uint8_t* buf = (const uint8_t*)p;
  // チャンクサイズはリトルエンディアンの符号なし 32bit として読む。
  // signed で合成して負値になると size_t への変換で巨大値になり大半はエラー返却されるが、
  // csize + 8 が 0〜7 になる 0xFFFFFFF8〜0xFFFFFFFF は比較が偽になって素通りしてしまう
  uint32_t csize = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
  // uint32_t のまま加算すると 2^32 でラップするため、size_t へ拡張してから比較する。
  // 実ファイルサイズを超えるチャンクサイズはここで弾く
  if (size < (size_t)csize + 8) {
    return false;
  }
  chunk_size = csize;
  chunk_data = buf + 8;
  return true;
}

int WavReader::Load(const void* ptr, size_t size) {
  if (size < 20) {
    return -1;
  }

  const char* cbuf = (const char*)ptr;
  if (std::string(cbuf, cbuf + 4) != "RIFF") {
    return -5;
  }
  if (std::string(cbuf + 8, cbuf + 8 + 4) != "WAVE") {
    return -6;
  }

  cbuf += 12;
  size -= 12;

  std::string chunk_name;
  size_t chunk_size;
  const void* chunk_data;
  if (!ReadChunk(cbuf, size, chunk_name, chunk_size, chunk_data)) {
    return -7;
  }
  cbuf += 8 + chunk_size;
  size -= 8 + chunk_size;

  if (chunk_name != "fmt ") {
    return -8;
  }
  const uint8_t* p = (const uint8_t*)chunk_data;

  int format_code = (int)p[0] | ((int)p[1] << 8);
  int channels = (int)p[2] | ((int)p[3] << 8);
  int sample_rate =
      (int)p[4] | ((int)p[5] << 8) | ((int)p[6] << 16) | ((int)p[7] << 24);
  int bits = (int)p[14] | ((int)p[15] << 8);

  if (format_code != 1) {
    return -9;
  }

  if (channels != 1 && channels != 2) {
    return 1;
  }

  if (bits != 16) {
    return -4;
  }
  this->channels = channels;
  this->sample_rate = sample_rate;

  while (true) {
    if (!ReadChunk(cbuf, size, chunk_name, chunk_size, chunk_data)) {
      return -10;
    }
    cbuf += 8 + chunk_size;
    size -= 8 + chunk_size;

    if (chunk_name != "data") {
      continue;
    }

    int n = chunk_size / 2;
    data.reserve(n);
    p = (const uint8_t*)chunk_data;
    for (int i = 0; i < n; i++) {
      // 16bit signed PCM として読む。unsigned の合成式から暗黙変換すると
      // signed として扱う意図が読めなくなるため、明示的に変換する
      int16_t s = static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                                       (static_cast<uint16_t>(p[1]) << 8));
      data.push_back(s);
      p += 2;
    }
    return 0;
  }
}
