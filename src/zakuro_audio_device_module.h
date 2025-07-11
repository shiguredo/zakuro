#ifndef ZAKURO_AUDIO_DEVICE_MODULE_H_
#define ZAKURO_AUDIO_DEVICE_MODULE_H_

#include <stddef.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

// webrtc
#include "api/make_ref_counted.h"
#include "api/environment/environment_factory.h"
#include "modules/audio_device/audio_device_buffer.h"
#include "modules/audio_device/include/audio_device.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/thread.h"

struct FakeAudioData {
  int sample_rate;
  int channels;
  std::vector<int16_t> data;
};

struct ZakuroAudioDeviceModuleConfig {
  enum class Type {
    ADM,
    Safari,
    FakeAudio,
    External,
  };
  Type type;
  // ADM
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm;
  // FakeAudio
  std::shared_ptr<FakeAudioData> fake_audio;
  // External
  std::function<void(std::vector<int16_t>&)> render;
  int sample_rate;
  int channels;

};

class ZakuroAudioDeviceModule : public webrtc::AudioDeviceModule {
 public:
  ZakuroAudioDeviceModule(ZakuroAudioDeviceModuleConfig config);
  ~ZakuroAudioDeviceModule() override;

  // adm != nullptr の場合 -> デバイスを使って録音する
  // adm == nullptr && fake_audio != nullptr の場合 -> 指定されたダミーデータを使って録音する
  // adm == nullptr && fake_audio == nullptr の場合 -> 特定のダミーデータを作って録音する
  // （録音を無効にしたい場合は webrtc::AudioDeviceModule::kDummyAudio で ADM を作って渡せば良い）
  static webrtc::scoped_refptr<ZakuroAudioDeviceModule> Create(
      ZakuroAudioDeviceModuleConfig config) {
    return webrtc::make_ref_counted<ZakuroAudioDeviceModule>(std::move(config));
  }

  void StartAudioThread();
  void StopAudioThread();

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
    if (adm_) {
      return adm_->RegisterAudioCallback(audioCallback);
    } else {
      device_buffer_->RegisterAudioCallback(audioCallback);
      return 0;
    }
  }

  // Main initialization and termination
  virtual int32_t Init() override {
    auto env = webrtc::CreateEnvironment();
    device_buffer_ =
        std::make_unique<webrtc::AudioDeviceBuffer>(&env.task_queue_factory());
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
      switch (config_.type) {
        case ZakuroAudioDeviceModuleConfig::Type::Safari:
        case ZakuroAudioDeviceModuleConfig::Type::FakeAudio:
        case ZakuroAudioDeviceModuleConfig::Type::External:
          device_buffer_->SetRecordingSampleRate(config_.sample_rate);
          device_buffer_->SetRecordingChannels(config_.channels);
          break;
        default:
          break;
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
  virtual int32_t InitSpeaker() override { return 0; }
  virtual bool SpeakerIsInitialized() const override { return false; }
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
  virtual int32_t SetSpeakerVolume(uint32_t volume) override { return 0; }
  virtual int32_t SpeakerVolume(uint32_t* volume) const override { return 0; }
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
  virtual int32_t SetSpeakerMute(bool enable) override { return 0; }
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
  virtual int32_t SetStereoPlayout(bool enable) override { return 0; }
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
  virtual int32_t PlayoutDelay(uint16_t* delayMS) const override { return 0; }

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
  ZakuroAudioDeviceModuleConfig config_;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
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
