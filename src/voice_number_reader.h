#ifndef VOICE_NUMBER_READER_H_
#define VOICE_NUMBER_READER_H_

#include <cassert>
#include <vector>

#include "embedded_binary.h"
#include "wav_reader.h"

class VoiceNumberReader {
public:
  std::vector<int16_t> Read(int n) {
    if (n < 0) {
      return {};
    }
    if (n >= 100) {
      return {};
    }

    // 0-29
    if (0 <= n && n <= 29) {
      const int res1[] = {
          RESOURCE_NUM000_01_WAV, RESOURCE_NUM001_01_WAV,
          RESOURCE_NUM002_01_WAV, RESOURCE_NUM003_01_WAV,
          RESOURCE_NUM004_01_WAV, RESOURCE_NUM005_01_WAV,
          RESOURCE_NUM006_01_WAV, RESOURCE_NUM007_01_WAV,
          RESOURCE_NUM008_01_WAV, RESOURCE_NUM009_01_WAV,
          RESOURCE_NUM010_01_WAV, RESOURCE_NUM011_01_WAV,
          RESOURCE_NUM012_01_WAV, RESOURCE_NUM013_01_WAV,
          RESOURCE_NUM014_01_WAV, RESOURCE_NUM015_01_WAV,
          RESOURCE_NUM016_01_WAV, RESOURCE_NUM017_01_WAV,
          RESOURCE_NUM018_01_WAV, RESOURCE_NUM019_01_WAV,
          RESOURCE_NUM020_01_WAV, RESOURCE_NUM021_01_WAV,
          RESOURCE_NUM022_01_WAV, RESOURCE_NUM023_01_WAV,
          RESOURCE_NUM024_01_WAV, RESOURCE_NUM025_01_WAV,
          RESOURCE_NUM026_01_WAV, RESOURCE_NUM027_01_WAV,
          RESOURCE_NUM028_01_WAV, RESOURCE_NUM029_01_WAV,
      };
      return res1[n];
    }
    // 30,40,50,...,90
    if (n % 10 == 0) {
      const int res10[] = {
          -1,
          -1,
          -1,
          RESOURCE_NUM030_01_WAV,
          RESOURCE_NUM040_01_WAV,
          RESOURCE_NUM050_01_WAV,
          RESOURCE_NUM060_01_WAV,
          RESOURCE_NUM070_01_WAV,
          RESOURCE_NUM080_01_WAV,
          RESOURCE_NUM090_01_WAV,
      };
      return Get(res10[n / 10]);
    }
    // 31,32,...,39
    // 41,42,...,49
    // ...
    // 91,92,...,99
    if (n <= 99) {
      const int res10[] = {
          -1,
          -1,
          -1,
          RESOURCE_NUM030_02_WAV,
          RESOURCE_NUM040_02_WAV,
          RESOURCE_NUM050_02_WAV,
          RESOURCE_NUM060_02_WAV,
          RESOURCE_NUM070_02_WAV,
          RESOURCE_NUM080_02_WAV,
          RESOURCE_NUM090_02_WAV,
      };
      const int res1[] = {
          -1,
          RESOURCE_NUM001_01_WAV,
          RESOURCE_NUM002_01_WAV,
          RESOURCE_NUM003_01_WAV,
          RESOURCE_NUM004_01_WAV,
          RESOURCE_NUM005_01_WAV,
          RESOURCE_NUM006_01_WAV,
          RESOURCE_NUM007_01_WAV,
          RESOURCE_NUM008_01_WAV,
          RESOURCE_NUM009_01_WAV,
      };
      return Get(res10[n / 10], res1[n % 10]);
    }
    return {};
  }

 private:
  std::vector<int16_t> Get(int a) { return Concat({EmbeddedBinary::Get(a)}); }
  std::vector<int16_t> Get(int a, int b) {
    return Concat({EmbeddedBinary::Get(a), EmbeddedBinary::Get(b)});
  }
  std::vector<int16_t> Get(int a, int b, int c) {
    return Concat({EmbeddedBinary::Get(a), EmbeddedBinary::Get(b),
                   EmbeddedBinary::Get(c)});
  }

  std::vector<int16_t> Concat(std::vector<EmbeddedBinaryContent> contents) {
    std::vector<int16_t> buf;
    for (auto content : contents) {
      WavReader wav_reader;
      int r = wav_reader.Load(content.ptr, content.size);
      assert(r == 0);
      assert(wav_reader.channels == 1);
      assert(wav_reader.sample_rate == 16000);
      buf.insert(buf.end(), wav_reader.data.begin(), wav_reader.data.end());
    }
    return buf;
  }
};

#endif
