#include "wav_reader.h"

#include <fstream>
#include <sstream>

int WavReader::Load(std::string path) {
  std::string buf;
  {
    std::stringstream ss;
    // WAV はバイナリファイル。text mode で開くと Windows で CRLF 変換により壊れる
    std::ifstream fin(path, std::ios::binary);
    ss << fin.rdbuf();
    buf = ss.str();
  }
  return Load(buf.c_str(), buf.size());
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
  // チャンクサイズは符号なしの 4 バイト値。signed で合成すると MSB が立つ値が
  // 負値になり、巨大なチャンクサイズを取りこぼす
  uint32_t csize = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
  // 先に size_t へ拡張してから加算する。uint32_t のまま加算すると 2^32 で
  // ラップして巨大なチャンクサイズが小さな値に化ける
  if (size < (size_t)csize + 8) {
    return false;
  }
  chunk_size = (size_t)csize;
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
      // 16bit signed PCM として読み出す。unsigned の合成式のままだと
      // int16_t への暗黙変換に意図が隠れる
      uint16_t u = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
      data.push_back(static_cast<int16_t>(u));
      p += 2;
    }
    return 0;
  }
}
