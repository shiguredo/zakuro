#ifndef GAME_KEY_H_
#define GAME_KEY_H_

#include <atomic>
#include <chrono>
#include <memory>
#include <queue>
#include <thread>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include "game_key_core.h"

class GameKey : public GameKeyInterface {
 public:
  GameKey(std::shared_ptr<GameKeyCore> core) : core_(core) {
    core->Register(this);
  }
  ~GameKey() {
    core_->Unregister(this);
  }

  void PushKey(uint8_t c) override {
    std::lock_guard<std::mutex> guard(mutex_);
    queue_.push(c);
  }

  int PopKey() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (queue_.empty()) {
      return -1;
    }
    uint8_t c = queue_.front();
    queue_.pop();
    return c;
  }

 private:
  std::unique_ptr<std::thread> th_;
  std::atomic_bool stopped_{false};
  std::mutex mutex_;
  std::queue<uint8_t> queue_;
  std::shared_ptr<GameKeyCore> core_;
};

#endif
