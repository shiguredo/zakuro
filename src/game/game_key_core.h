#ifndef GAME_KEY_CORE_H_
#define GAME_KEY_CORE_H_

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

class GameKeyInterface {
 public:
  ~GameKeyInterface() {}
  virtual void PushKey(uint8_t c) = 0;
};

class GameKeyCore {
 public:
  ~GameKeyCore() { Reset(); }

  void Init() {
    Reset();
    th_.reset(new std::thread([this]() {
      int fd = STDIN_FILENO;

      termios old = {};
      {
        int r = tcgetattr(fd, &old);
        if (r < 0) {
          std::cerr << "failed to tcgetattr: " << errno << std::endl;
          return;
        }
        termios ios = old;
        ios.c_lflag &= ~ICANON;
        ios.c_lflag &= ~ECHO;
        ios.c_cc[VMIN] = 1;
        ios.c_cc[VTIME] = 0;
        r = tcsetattr(fd, TCSANOW, &ios);
        if (r < 0) {
          std::cerr << "failed to tcsetattr: " << errno << std::endl;
          return;
        }
      }
      std::shared_ptr<void> restore_termios(new int(), [&old](int* p) {
        delete p;
        int r = tcsetattr(0, TCSADRAIN, &old);
        if (r < 0) {
          std::cerr << "failed to tcsetattr: " << errno << std::endl;
        }
      });

      while (true) {
        fd_set fdset;
        FD_ZERO(&fdset);
        FD_SET(fd, &fdset);
        // 100 ミリ秒ごとに停止を確認する
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100 * 1000;
        int r = select(fd + 1, &fdset, nullptr, nullptr, &tv);
        if (stopped_) {
          return;
        }
        if (r < 0) {
          std::cerr << "failed to select: " << errno << std::endl;
          return;
        }
        if (r == 0) {
          continue;
        }
        uint8_t c;
        int n = read(fd, &c, 1);
        if (n <= 0) {
          std::cerr << "failed to read: n=" << n << ", errno=" << errno
                    << std::endl;
          return;
        }
        PushKey(c);
      }
    }));
  }

  void Reset() {
    if (th_) {
      stopped_ = true;
      th_->join();
      th_.reset();
      stopped_ = false;
    }
  }

  void Register(GameKeyInterface* key) { keys_.push_back(key); }
  void Unregister(GameKeyInterface* key) {
    auto it = std::remove_if(keys_.begin(), keys_.end(),
                             [key](GameKeyInterface* k) { return k == key; });
    keys_.erase(it, keys_.end());
  }

  //int PopKey() {
  //  std::lock_guard<std::mutex> guard(mutex_);
  //  if (queue_.empty()) {
  //    return -1;
  //  }
  //  uint8_t c = queue_.front();
  //  queue_.pop();
  //  return c;
  //}

 private:
  void PushKey(uint8_t c) {
    for (auto key : keys_) {
      key->PushKey(c);
    }
  }

 private:
  std::unique_ptr<std::thread> th_;
  std::atomic_bool stopped_{false};
  std::vector<GameKeyInterface*> keys_;
};

#endif