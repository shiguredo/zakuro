#ifndef FAKE_AUDIO_KEY_TRIGGER_H_
#define FAKE_AUDIO_KEY_TRIGGER_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

// Boost
#include <boost/asio.hpp>

#include "game/game_audio.h"
#include "game/game_key.h"
#include "scenario_player.h"
#include "virtual_client.h"
#include "voice_number_reader.h"

class FakeAudioKeyTrigger {
 public:
  FakeAudioKeyTrigger(boost::asio::io_context& ioc,
                      GameAudioManager* gam,
                      ScenarioPlayer* sp,
                      std::vector<std::unique_ptr<VirtualClient>>& vcs)
      : ioc_(ioc), gam_(gam), sp_(sp), vcs_(vcs) {
    key_.Init();
    th_.reset(new std::thread([this]() {
      std::string seq;
      std::chrono::system_clock::time_point seq_time;

      while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (stopped_) {
          return;
        }

        auto now = std::chrono::system_clock::now();
        if (now > seq_time + std::chrono::milliseconds(1000)) {
          seq = "";
          seq_time = now;
        }

        int n = key_.PopKey();
        if (n < 0) {
          continue;
        }
        if (n == 's') {
          // 一時停止
          boost::asio::post(ioc_, [this]() { sp_->PauseAll(); });
        } else if (n == 'S') {
          // 再開
          boost::asio::post(ioc_, [this]() { sp_->ResumeAll(); });
        } else if ('a' <= n && n <= 'z' || 'A' <= n && n <= 'Z') {
          seq += (char)n;
          seq_time = now;
        } else if ('0' <= n && n <= '9') {
          // '1'->0, '2'->1, '3'->2, ..., '0'->9
          int m = n == '0' ? 9 : n - '1';

          // <num> → 音声再生
          if (seq.empty()) {
            std::vector<int16_t> buf = voice_reader_.Read(m + 1);
            gam_->Play(m, buf);
          }
          // q+<num> → 切断
          if (seq.size() == 1 && seq[0] == 'q') {
            if (m < vcs_.size()) {
              boost::asio::post(ioc_, [this, m]() { vcs_[m]->Close(); });
            }
          }
          // Q+<num> → 再接続
          if (seq.size() == 1 && seq[0] == 'Q') {
            if (m < vcs_.size()) {
              boost::asio::post(ioc_, [this, m]() { vcs_[m]->Connect(); });
            }
          }
          seq.clear();
          seq_time = now;
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
  boost::asio::io_context& ioc_;
  GameAudioManager* gam_;
  ScenarioPlayer* sp_;
  std::vector<std::unique_ptr<VirtualClient>>& vcs_;
};

#endif
