#ifndef WAV_READER_H_
#define WAV_READER_H_

#include <fstream>
#include <string>
#include <vector>

class WavReader {
 public:
  int channels;
  int sample_rate;
  std::vector<int16_t> data;

  int Load(std::string path) {
    std::string buf;
    {
      std::stringstream ss;
      std::ifstream fin(path);
      ss << fin.rdbuf();
      buf = ss.str();
    }
    if (buf.size() < 20) {
      return -1;
    }

    if (buf.substr(0, 4) != "RIFF") {
      return -5;
    }
    if (buf.substr(8, 8) != "WAVEfmt ") {
      return -6;
    }
    const uint8_t* p = (const uint8_t*)buf.c_str();
    int fmt_size = (int)p[16] | ((int)p[17] << 8) | ((int)p[18] << 16) |
                   ((int)p[19] << 24);
    if (buf.size() < 20 + fmt_size + 8) {
      return -7;
    }

    if (buf.substr(20 + fmt_size, 4) != "data") {
      return -8;
    }

    int format_code = (int)p[20] | ((int)p[21] << 8);
    int channels = (int)p[22] | ((int)p[23] << 8);
    int sample_rate = (int)p[24] | ((int)p[25] << 8) | ((int)p[26] << 16) |
                      ((int)p[27] << 24);
    int bits = (int)p[34] | ((int)p[35] << 8);

    if (format_code != 1) {
      return -2;
    }

    if (channels != 1 && channels != 2) {
      return 1;
    }

    if (bits != 16) {
      return -4;
    }
    this->channels = channels;
    this->sample_rate = sample_rate;

    int offset = 20 + fmt_size + 8;
    int n = (buf.size() - offset) / 2;
    data.reserve(n);
    p = (const uint8_t*)(buf.c_str() + offset);
    for (int i = 0; i < n; i++) {
      data.push_back((int)p[0] | ((int)p[1] << 8));
      p += 2;
    }
    return 0;
  }
};

#endif
