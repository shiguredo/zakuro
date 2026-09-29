#include "zakuro_audio_device_module.h"

#include <cmath>

ZakuroAudioDeviceModule::ZakuroAudioDeviceModule(
    ZakuroAudioDeviceModuleConfig config)
    : env_(webrtc::CreateEnvironment()), config_(std::move(config)) {
  adm_ = config_.adm;
  if (config_.type == ZakuroAudioDeviceModuleConfig::Type::FakeAudio) {
    fake_audio_ = config_.fake_audio;

    config_.sample_rate = fake_audio_->sample_rate;
    config_.channels = fake_audio_->channels;
  }

  if (config_.type == ZakuroAudioDeviceModuleConfig::Type::Safari) {
    static const int SAMPLE_RATE = 48000;
    static const double BIPBOP_DURATION = 0.07;
    static const double BIPBOP_VOLUME = 0.5;
    static const double BIP_FREQUENCY = 1500;
    static const double BOP_FREQUENCY = 500;
    static const double HUM_FREQUENCY = 150;
    static const double HUM_VOLUME = 0.1;
    static const double NOISE_FREQUENCY = 3000;
    static const double NOISE_VOLUME = 0.05;

    fake_audio_.reset(new FakeAudioData());
    fake_audio_->sample_rate = SAMPLE_RATE;
    fake_audio_->channels = 1;
    auto add_hum = [](float volume, float frequency, float sample_rate,
                      int start, int16_t* p, int count) {
      float hum_period = sample_rate / frequency;
      for (int i = start; i < start + count; ++i) {
        int16_t a = (int16_t)(volume * sin(i * 2 * M_PI / hum_period) * 32767);
        *p += a;
        ++p;
      }
    };
    fake_audio_->data.resize(SAMPLE_RATE * 2);

    int bipbop_sample_count = (int)std::ceil(BIPBOP_DURATION * SAMPLE_RATE);

    add_hum(BIPBOP_VOLUME, BIP_FREQUENCY, SAMPLE_RATE, 0,
            fake_audio_->data.data(), bipbop_sample_count);
    add_hum(BIPBOP_VOLUME, BOP_FREQUENCY, SAMPLE_RATE, 0,
            fake_audio_->data.data() + SAMPLE_RATE, bipbop_sample_count);
    add_hum(NOISE_VOLUME, NOISE_FREQUENCY, SAMPLE_RATE, 0,
            fake_audio_->data.data(), 2 * SAMPLE_RATE);
    add_hum(HUM_VOLUME, HUM_FREQUENCY, SAMPLE_RATE, 0, fake_audio_->data.data(),
            2 * SAMPLE_RATE);

    config_.sample_rate = fake_audio_->sample_rate;
    config_.channels = fake_audio_->channels;
  }
}

ZakuroAudioDeviceModule::~ZakuroAudioDeviceModule() {
  Terminate();
}

void ZakuroAudioDeviceModule::StartAudioThread() {
  switch (config_.type) {
    case ZakuroAudioDeviceModuleConfig::Type::Safari:
    case ZakuroAudioDeviceModuleConfig::Type::FakeAudio:
    case ZakuroAudioDeviceModuleConfig::Type::External:
      break;
    default:
      return;
  }

  StopAudioThread();

  // 10 ミリ秒分のバッファサイズ。sample_rate と channels が小さすぎる場合は 0 になり、
  // 剰余算や 0 除算が起こるため、音声スレッドを開始しない
  int buf_size = config_.sample_rate * config_.channels * 10 / 1000;
  if (buf_size <= 0) {
    return;
  }

  audio_thread_.reset(new std::thread([this, buf_size]() {
    // 10 ミリ秒毎に送信
    // index は BSD の関数名と衝突するため sample_index とする
    int sample_index = 0;
    std::vector<int16_t> buf;
    buf.reserve(buf_size);

    auto deliver = [this, buf_size](std::vector<int16_t>& b) {
      device_buffer_->SetRecordedBuffer(b.data(), buf_size / config_.channels);
      device_buffer_->DeliverRecordedData();
      b.clear();
    };

    auto prev_at = std::chrono::steady_clock::now();
    while (!audio_thread_stopped_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      auto now = std::chrono::steady_clock::now();
      auto sample_count =
          config_.sample_rate *
          std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_at)
              .count() /
          1000;
      prev_at = now;

      if (config_.type == ZakuroAudioDeviceModuleConfig::Type::Safari ||
          config_.type == ZakuroAudioDeviceModuleConfig::Type::FakeAudio) {
        if (fake_audio_->data.empty()) {
          // 空の data を読むと空の vector への添字アクセスになるため無音を送出する
          buf.resize(buf_size, 0);
          deliver(buf);
          continue;
        }
        const int data_size = (int)fake_audio_->data.size();
        for (int i = 0; i < sample_count; i++) {
          for (int j = 0; j < config_.channels; j++) {
            // data の要素数が channels の倍数でない場合も添字が範囲内になるようにする
            buf.push_back(fake_audio_->data[sample_index]);
            sample_index = (sample_index + 1) % data_size;
          }
          if ((int)buf.size() >= buf_size) {
            deliver(buf);
          }
        }
      } else if (config_.type ==
                 ZakuroAudioDeviceModuleConfig::Type::External) {
        while (sample_count >= buf_size) {
          config_.render(buf);
          deliver(buf);
          sample_count -= buf_size;
        }
      }
    }
  }));
}

void ZakuroAudioDeviceModule::StopAudioThread() {
  if (audio_thread_) {
    audio_thread_stopped_ = true;
    audio_thread_->join();
    audio_thread_.reset();
    audio_thread_stopped_ = false;
  }
}
