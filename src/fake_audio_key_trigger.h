#ifndef FAKE_AUDIO_KEY_TRIGGER_H_
#define FAKE_AUDIO_KEY_TRIGGER_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "game/game_audio.h"
#include "game/game_key.h"
#include "voice_number_reader.h"

class FakeAudioKeyTrigger {
 public:
  FakeAudioKeyTrigger(GameAudioManager* gam) : gam_(gam) {
    key_.Init();
    th_.reset(new std::thread([this]() {
      while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stopped_) {
          return;
        }

        int n = key_.PopKey();
        if (n < 0) {
          continue;
        }
        if ('0' <= n && n <= '9') {
          std::vector<int16_t> buf = voice_reader_.Read(n - '0');
          gam_->Play(n - '0', buf);
        }
      }
    }));
  }
  ~FakeAudioKeyTrigger() { Reset(); }

  void Reset() {
    if (th_) {
      stopped_ = true;
      th_->join();
      th_.reset();
      stopped_ = false;
    }
  }

 private:
  std::unique_ptr<std::thread> th_;
  std::atomic_bool stopped_{false};
  VoiceNumberReader voice_reader_;
  GameKey key_;
  GameAudioManager* gam_;
};

#endif
