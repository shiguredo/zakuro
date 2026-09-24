// ZakuroAudioDeviceModule の初期化再入とライフサイクルを検証する。
// テストフレームワークは未導入のため、この実行ファイル単体で合否を返す。

#include "zakuro_audio_device_module.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

// 録音コールバックの回数だけを数える。
class CountingAudioTransport : public webrtc::AudioTransport {
 public:
  int CallCount() const { return call_count_.load(); }

  int32_t RecordedDataIsAvailable(const void* /* audioSamples */,
                                  size_t /* nSamples */,
                                  size_t /* nBytesPerSample */,
                                  size_t /* nChannels */,
                                  uint32_t /* samplesPerSec */,
                                  uint32_t /* totalDelayMS */,
                                  int32_t /* clockDrift */,
                                  uint32_t currentMicLevel,
                                  bool /* keyPressed */,
                                  uint32_t& newMicLevel) override {
    newMicLevel = currentMicLevel;
    call_count_.fetch_add(1);
    return 0;
  }

  int32_t NeedMorePlayData(size_t /* nSamples */,
                           size_t /* nBytesPerSample */,
                           size_t /* nChannels */,
                           uint32_t /* samplesPerSec */,
                           void* /* audioSamples */,
                           size_t& nSamplesOut,
                           int64_t* /* elapsed_time_ms */,
                           int64_t* /* ntp_time_ms */) override {
    nSamplesOut = 0;
    return 0;
  }

  void PullRenderData(int /* bits_per_sample */,
                      int /* sample_rate */,
                      size_t /* number_of_channels */,
                      size_t /* number_of_frames */,
                      void* /* audio_data */,
                      int64_t* /* elapsed_time_ms */,
                      int64_t* /* ntp_time_ms */) override {}

 private:
  std::atomic<int> call_count_{0};
};

webrtc::scoped_refptr<ZakuroAudioDeviceModule> CreateFakeAudioModule() {
  ZakuroAudioDeviceModuleConfig config;
  config.type = ZakuroAudioDeviceModuleConfig::Type::FakeAudio;
  auto audio = std::make_shared<FakeAudioData>();
  // 10 ミリ秒分。オーディオスレッドの 1 回の書き込みサイズと揃える。
  audio->sample_rate = 16000;
  audio->channels = 1;
  audio->data.assign(160, 0);
  config.fake_audio = std::move(audio);
  return ZakuroAudioDeviceModule::Create(std::move(config));
}

bool WaitForCalls(const CountingAudioTransport& transport,
                  int minimum,
                  std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (transport.CallCount() >= minimum) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return transport.CallCount() >= minimum;
}

int Fail(const std::string& message) {
  std::cerr << "失敗: " << message << std::endl;
  return 1;
}

// 2 回目の Init がバッファを捨てず、登録済みコールバックが呼ばれ続けること。
int TestInitKeepsCallback() {
  auto adm = CreateFakeAudioModule();
  if (adm->Init() != 0) {
    return Fail("初回の Init が失敗した");
  }
  if (adm->InitRecording() != 0) {
    return Fail("InitRecording が失敗した");
  }

  CountingAudioTransport transport;
  if (adm->RegisterAudioCallback(&transport) != 0) {
    return Fail("RegisterAudioCallback が失敗した");
  }
  if (adm->StartRecording() != 0) {
    return Fail("StartRecording が失敗した");
  }
  if (!WaitForCalls(transport, 1, std::chrono::seconds(1))) {
    adm->StopRecording();
    return Fail("2 回目の Init の前にコールバックが呼ばれなかった");
  }

  const int calls_before = transport.CallCount();
  if (adm->Init() != 0) {
    adm->StopRecording();
    return Fail("2 回目の Init が 0 を返さなかった");
  }
  if (!WaitForCalls(transport, calls_before + 1, std::chrono::seconds(1))) {
    adm->StopRecording();
    return Fail("2 回目の Init の後にコールバックが呼ばれなかった");
  }

  if (adm->StopRecording() != 0) {
    return Fail("StopRecording が失敗した");
  }
  std::cout << "Init の再入後もコールバックが呼ばれることを確認した" << std::endl;
  return 0;
}

// 生成から破棄までを繰り返し、落ちないこと。
int TestLifecycleLoop() {
  constexpr int kLoops = 1000;
  for (int i = 0; i < kLoops; ++i) {
    auto adm = CreateFakeAudioModule();
    if (adm->Init() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の初回 Init が失敗した");
    }
    if (adm->Init() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の 2 回目の Init が失敗した");
    }
    CountingAudioTransport transport;
    if (adm->RegisterAudioCallback(&transport) != 0) {
      return Fail("ライフサイクル " + std::to_string(i) +
                  " の RegisterAudioCallback が失敗した");
    }
    if (adm->InitRecording() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の InitRecording が失敗した");
    }
    if (adm->StartRecording() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の StartRecording が失敗した");
    }
    // スレッドがバッファを触る前に止めると、解放順の不具合を踏めない。
    if (!WaitForCalls(transport, 1, std::chrono::seconds(1))) {
      adm->StopRecording();
      return Fail("ライフサイクル " + std::to_string(i) + " でコールバックが呼ばれなかった");
    }
    if (adm->StopRecording() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の StopRecording が失敗した");
    }
    if (adm->Terminate() != 0) {
      return Fail("ライフサイクル " + std::to_string(i) + " の Terminate が失敗した");
    }
  }
  std::cout << "ライフサイクルを " << kLoops << " 回繰り返してクラッシュしないことを確認した"
            << std::endl;
  return 0;
}

}  // namespace

int main() {
  if (const int result = TestInitKeepsCallback()) {
    return result;
  }
  if (const int result = TestLifecycleLoop()) {
    return result;
  }
  std::cout << "すべてのテストに成功した" << std::endl;
  return 0;
}
