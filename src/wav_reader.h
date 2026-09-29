#ifndef WAV_READER_H_
#define WAV_READER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class WavReader {
 public:
  int channels;
  int sample_rate;
  std::vector<int16_t> data;

  // 戻り値は 0 が成功、それ以外は失敗。失敗の値は原因を表す
  //   1: channels が 1 / 2 以外
  //  -1: データが短すぎる、または読み込み中に領域の確保に失敗した
  //  -4: bits が 16 以外
  //  -5: RIFF の識別子が不正
  //  -6: WAVE の識別子が不正
  //  -7: fmt チャンクの読み出しに失敗
  //  -8: 先頭のチャンクが fmt でない
  //  -9: format_code が 1 (PCM) 以外
  //  -10: data チャンクの読み出しに失敗
  int Load(std::string path);
  int Load(const void* ptr, size_t size);
};

#endif
