#ifndef WAV_READER_H_
#define WAV_READER_H_

#include <string>
#include <vector>

class WavReader {
 public:
  int channels;
  int sample_rate;
  std::vector<int16_t> data;

  int Load(std::string path);
};

#endif
