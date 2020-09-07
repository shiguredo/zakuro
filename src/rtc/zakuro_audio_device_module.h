#ifndef ZAKURO_AUDIO_DEVICE_MODULE_H_
#define ZAKURO_AUDIO_DEVICE_MODULE_H_

#include <stddef.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

// webrtc
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/thread.h"

struct FakeAudioData {
  int sample_rate;
  int channels;
  std::vector<int16_t> data;
};

class ZakuroAudioDeviceModule : public webrtc::AudioDeviceModule {
 public:
  ZakuroAudioDeviceModule(rtc::scoped_refptr<webrtc::AudioDeviceModule> adm,
                          webrtc::TaskQueueFactory* task_queue_factory,
                          std::shared_ptr<FakeAudioData> fake_audio)
      : adm_(adm),
        task_queue_factory_(task_queue_factory),
        fake_audio_(std::move(fake_audio)) {
    if (adm == nullptr && fake_audio_ == nullptr) {
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
          int16_t a =
              (int16_t)(volume * sin(i * 2 * M_PI / hum_period) * 32767);
          *p += a;
          ++p;
        }
      };
      fake_audio_->data.resize(SAMPLE_RATE * 2);

      int bipbop_sample_count = (int)ceil(BIPBOP_DURATION * SAMPLE_RATE);

      add_hum(BIPBOP_VOLUME, BIP_FREQUENCY, SAMPLE_RATE, 0,
              fake_audio_->data.data(), bipbop_sample_count);
      add_hum(BIPBOP_VOLUME, BOP_FREQUENCY, SAMPLE_RATE, 0,
              fake_audio_->data.data() + SAMPLE_RATE, bipbop_sample_count);
      add_hum(NOISE_VOLUME, NOISE_FREQUENCY, SAMPLE_RATE, 0,
              fake_audio_->data.data(), 2 * SAMPLE_RATE);
      add_hum(HUM_VOLUME, HUM_FREQUENCY, SAMPLE_RATE, 0,
              fake_audio_->data.data(), 2 * SAMPLE_RATE);
    }
  }

  ~ZakuroAudioDeviceModule() override {
    Terminate();
  }

  // adm != nullptr の場合 -> デバイスを使って録音する
  // adm == nullptr && fake_audio != nullptr の場合 -> 指定されたダミーデータを使って録音する
  // adm == nullptr && fake_audio == nullptr の場合 -> 特定のダミーデータを作って録音する
  // （録音を無効にしたい場合は webrtc::AudioDeviceModule::kDummyAudio で ADM を作って渡せば良い）
  static rtc::scoped_refptr<ZakuroAudioDeviceModule> Create(
      rtc::scoped_refptr<webrtc::AudioDeviceModule> adm,
      webrtc::TaskQueueFactory* task_queue_factory,
      std::shared_ptr<FakeAudioData> fake_audio) {
    return new rtc::RefCountedObject<ZakuroAudioDeviceModule>(
        adm, task_queue_factory, std::move(fake_audio));
  }

  void StartAudioThread() {
    if (!fake_audio_) {
      return;
    }

    StopAudioThread();
    audio_thread_.reset(new std::thread([this]() {
      int index = 0;
      // 10 ミリ秒毎に送信
      std::vector<int16_t> buf;
      int buf_size =
          fake_audio_->sample_rate * fake_audio_->channels * 10 / 1000;
      buf.reserve(buf_size);
      auto prev_at = std::chrono::steady_clock::now();
      while (!audio_thread_stopped_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto now = std::chrono::steady_clock::now();
        auto sample_count =
            fake_audio_->sample_rate *
            std::chrono::duration_cast<std::chrono::milliseconds>(now - prev_at)
                .count() /
            1000;
        for (int i = 0; i < sample_count; i++) {
          for (int j = 0; j < fake_audio_->channels; j++) {
            buf.push_back(fake_audio_->data[index]);
            index += 1;
          }
          if (buf.size() >= buf_size) {
            device_buffer_->SetRecordedBuffer(buf.data(),
                                              buf_size / fake_audio_->channels);
            device_buffer_->DeliverRecordedData();
            buf.clear();
          }
          if (index >= fake_audio_->data.size()) {
            index = 0;
          }
        }
        prev_at = now;
      }
    }));
  }
  void StopAudioThread() {
    if (audio_thread_) {
      audio_thread_stopped_ = true;
      audio_thread_->join();
      audio_thread_.reset();
      audio_thread_stopped_ = false;
    }
  }

  //webrtc::AudioDeviceModule
  // Retrieve the currently utilized audio layer
  virtual int32_t ActiveAudioLayer(AudioLayer* audioLayer) const override {
    if (adm_) {
      adm_->ActiveAudioLayer(audioLayer);
    } else {
      *audioLayer = AudioDeviceModule::kPlatformDefaultAudio;
    }
    return 0;
  }
  // Full-duplex transportation of PCM audio
  virtual int32_t RegisterAudioCallback(
      webrtc::AudioTransport* audioCallback) override {
    RTC_LOG(LS_INFO) << "RegisterAudioCallback";
    if (adm_) {
      return adm_->RegisterAudioCallback(audioCallback);
    } else {
      device_buffer_->RegisterAudioCallback(audioCallback);
      return 0;
    }
  }

  // Main initialization and termination
  virtual int32_t Init() override {
    RTC_LOG(LS_INFO) << "Init";
    device_buffer_ =
        std::make_unique<webrtc::AudioDeviceBuffer>(task_queue_factory_);
    initialized_ = true;
    if (adm_) {
      return adm_->Init();
    } else {
      return 0;
    }
  }
  virtual int32_t Terminate() override {
    initialized_ = false;
    is_recording_ = false;
    microphone_initialized_ = false;
    recording_initialized_ = false;
    device_buffer_.reset();

    StopAudioThread();

    if (adm_) {
      return adm_->Terminate();
    } else {
      return 0;
    }
  }
  virtual bool Initialized() const override { return initialized_; }

  // Device enumeration
  virtual int16_t PlayoutDevices() override { return 0; }
  virtual int16_t RecordingDevices() override {
    return adm_ ? adm_->RecordingDevices() : 0;
  }
  virtual int32_t PlayoutDeviceName(
      uint16_t index,
      char name[webrtc::kAdmMaxDeviceNameSize],
      char guid[webrtc::kAdmMaxGuidSize]) override {
    return 0;
  }
  virtual int32_t RecordingDeviceName(
      uint16_t index,
      char name[webrtc::kAdmMaxDeviceNameSize],
      char guid[webrtc::kAdmMaxGuidSize]) override {
    return adm_ ? adm_->RecordingDeviceName(index, name, guid) : 0;
  }

  // Device selection
  virtual int32_t SetPlayoutDevice(uint16_t index) override { return 0; }
  virtual int32_t SetPlayoutDevice(WindowsDeviceType device) override {
    return 0;
  }
  virtual int32_t SetRecordingDevice(uint16_t index) override {
    return adm_ ? adm_->SetRecordingDevice(index) : 0;
  }
  virtual int32_t SetRecordingDevice(WindowsDeviceType device) override {
    return adm_ ? adm_->SetRecordingDevice(device) : 0;
  }

  // Audio transport initialization
  virtual int32_t PlayoutIsAvailable(bool* available) override {
    *available = false;
    return 0;
  }
  virtual int32_t InitPlayout() override { return 0; }
  virtual bool PlayoutIsInitialized() const override { return 0; }
  virtual int32_t RecordingIsAvailable(bool* available) override {
    if (adm_) {
      return adm_->RecordingIsAvailable(available);
    } else {
      *available = true;
      return 0;
    }
  }
  virtual int32_t InitRecording() override {
    if (adm_) {
      return adm_->InitRecording();
    } else {
      recording_initialized_ = true;
      if (fake_audio_) {
        device_buffer_->SetRecordingSampleRate(fake_audio_->sample_rate);
        device_buffer_->SetRecordingChannels(fake_audio_->channels);
      }
      return 0;
    }
  }
  virtual bool RecordingIsInitialized() const override {
    return adm_ ? adm_->RecordingIsInitialized() : (bool)recording_initialized_;
  }

  // Audio transport control
  virtual int32_t StartPlayout() override { return 0; }
  virtual int32_t StopPlayout() override { return 0; }
  virtual bool Playing() const override { return false; }
  virtual int32_t StartRecording() override {
    if (adm_) {
      return adm_->StartRecording();
    } else {
      StartAudioThread();
      is_recording_ = true;
      return 0;
    }
  }
  virtual int32_t StopRecording() override {
    if (adm_) {
      return adm_->StopRecording();
    } else {
      StopAudioThread();
      is_recording_ = false;
      return 0;
    }
  }
  virtual bool Recording() const override {
    return adm_ ? adm_->Recording() : (bool)is_recording_;
  }

  // Audio mixer initialization
  virtual int32_t InitSpeaker() override {
    return 0;
  }
  virtual bool SpeakerIsInitialized() const override {
    return false;
  }
  virtual int32_t InitMicrophone() override {
    microphone_initialized_ = true;
    return adm_ ? adm_->InitMicrophone() : 0;
  }
  virtual bool MicrophoneIsInitialized() const override {
    return adm_ ? adm_->MicrophoneIsInitialized()
                : (bool)microphone_initialized_;
  }

  // Speaker volume controls
  virtual int32_t SpeakerVolumeIsAvailable(bool* available) override {
    *available = false;
    return 0;
  }
  virtual int32_t SetSpeakerVolume(uint32_t volume) override {
    return 0;
  }
  virtual int32_t SpeakerVolume(uint32_t* volume) const override {
    return 0;
  }
  virtual int32_t MaxSpeakerVolume(uint32_t* maxVolume) const override {
    return 0;
  }
  virtual int32_t MinSpeakerVolume(uint32_t* minVolume) const override {
    return 0;
  }

  // Microphone volume controls
  virtual int32_t MicrophoneVolumeIsAvailable(bool* available) override {
    *available = false;
    return adm_ ? adm_->MicrophoneVolumeIsAvailable(available) : 0;
  }
  virtual int32_t SetMicrophoneVolume(uint32_t volume) override {
    return adm_ ? adm_->SetMicrophoneVolume(volume) : 0;
  }
  virtual int32_t MicrophoneVolume(uint32_t* volume) const override {
    return adm_ ? adm_->MicrophoneVolume(volume) : 0;
  }
  virtual int32_t MaxMicrophoneVolume(uint32_t* maxVolume) const override {
    return adm_ ? adm_->MaxMicrophoneVolume(maxVolume) : 0;
  }
  virtual int32_t MinMicrophoneVolume(uint32_t* minVolume) const override {
    return adm_ ? adm_->MinMicrophoneVolume(minVolume) : 0;
  }

  // Speaker mute control
  virtual int32_t SpeakerMuteIsAvailable(bool* available) override {
    *available = false;
    return 0;
  }
  virtual int32_t SetSpeakerMute(bool enable) override {
    return 0;
  }
  virtual int32_t SpeakerMute(bool* enabled) const override {
    *enabled = false;
    return 0;
  }

  // Microphone mute control
  virtual int32_t MicrophoneMuteIsAvailable(bool* available) override {
    return adm_ ? adm_->MicrophoneMuteIsAvailable(available) : 0;
  }
  virtual int32_t SetMicrophoneMute(bool enable) override {
    return adm_ ? adm_->SetMicrophoneMute(enable) : 0;
  }
  virtual int32_t MicrophoneMute(bool* enabled) const override {
    return adm_ ? adm_->MicrophoneMute(enabled) : 0;
  }

  // Stereo support
  virtual int32_t StereoPlayoutIsAvailable(bool* available) const override {
    *available = false;
    return 0;
  }
  virtual int32_t SetStereoPlayout(bool enable) override {
    return 0;
  }
  virtual int32_t StereoPlayout(bool* enabled) const override {
    *enabled = false;
    return 0;
  }
  virtual int32_t StereoRecordingIsAvailable(bool* available) const override {
    if (adm_) {
      return adm_->StereoRecordingIsAvailable(available);
    } else {
      *available = false;
      return 0;
    }
  }
  virtual int32_t SetStereoRecording(bool enable) override {
    return adm_ ? adm_->SetStereoRecording(enable) : 0;
  }
  virtual int32_t StereoRecording(bool* enabled) const override {
    if (adm_) {
      return adm_->StereoRecording(enabled);
    } else {
      *enabled = false;
      return 0;
    }
  }

  // Playout delay
  virtual int32_t PlayoutDelay(uint16_t* delayMS) const override {
    return 0;
  }

  // Only supported on Android.
  virtual bool BuiltInAECIsAvailable() const override { return false; }
  virtual bool BuiltInAGCIsAvailable() const override { return false; }
  virtual bool BuiltInNSIsAvailable() const override { return false; }

  // Enables the built-in audio effects. Only supported on Android.
  virtual int32_t EnableBuiltInAEC(bool enable) override { return 0; }
  virtual int32_t EnableBuiltInAGC(bool enable) override { return 0; }
  virtual int32_t EnableBuiltInNS(bool enable) override { return 0; }

// Only supported on iOS.
#if defined(WEBRTC_IOS)
  virtual int GetPlayoutAudioParameters(
      webrtc::AudioParameters* params) const override {
    return -1;
  }
  virtual int GetRecordAudioParameters(
      webrtc::AudioParameters* params) const override {
    return -1;
  }
#endif  // WEBRTC_IOS

 private:
  rtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  webrtc::TaskQueueFactory* task_queue_factory_;
  std::unique_ptr<std::thread> audio_thread_;
  std::atomic_bool audio_thread_stopped_ = {false};
  std::unique_ptr<webrtc::AudioDeviceBuffer> device_buffer_;
  std::atomic_bool initialized_ = {false};
  std::atomic_bool microphone_initialized_ = {false};
  std::atomic_bool recording_initialized_ = {false};
  std::atomic_bool is_recording_ = {false};
  std::vector<int16_t> converted_audio_data_;
  std::shared_ptr<FakeAudioData> fake_audio_;
};

#endif
