#ifndef GAME_AUDIO_H_
#define GAME_AUDIO_H_

class GameAudio {
 public:
  GameAudio(int sample_rate) : sample_rate_(sample_rate) {}

  void Play(double frequency, double duration, double volume) {
    std::lock_guard<std::mutex> guard(mutex_);

    int count = (int)(sample_rate_ * duration);
    if (buf_.size() < count) {
      buf_.resize(count);
    }
    float period = sample_rate_ / frequency;
    for (int i = 0; i < count; i++) {
      buf_[i] += (int16_t)(volume * sin(i * 2 * M_PI / period) * 32767);
    }
  }
  void Play(const std::vector<int16_t>& buf) {
    std::lock_guard<std::mutex> guard(mutex_);

    buf_.assign(buf.begin(), buf.end());
  }
  void Render(std::vector<int16_t>& buf) {
    std::lock_guard<std::mutex> guard(mutex_);

    size_t size = std::min(buf.size(), buf_.size());
    std::copy(buf_.begin(), buf_.begin() + size, buf.begin());
    std::fill(buf.begin() + size, buf.end(), 0);
    buf_.erase(buf_.begin(), buf_.begin() + size);
  }

 private:
  int sample_rate_;
  std::vector<int16_t> buf_;
  std::mutex mutex_;
};

class GameAudioManager {
 public:
  template <class... Args>
  void PlayAny(Args&&... args) {
    int n = rand() % audios_.size();
    Play(n, std::forward<Args>(args)...);
  }
  template <class... Args>
  void Play(int n, Args&&... args) {
    if (n < 0 || n >= audios_.size()) {
      return;
    }
    audios_[n]->Play(std::forward<Args>(args)...);
  }
  std::function<void(std::vector<int16_t>&)> AddGameAudio(int sample_rate) {
    auto audio = std::unique_ptr<GameAudio>(new GameAudio(sample_rate));
    auto f = [audio = audio.get()](std::vector<int16_t>& buf) {
      audio->Render(buf);
    };
    audios_.push_back(std::move(audio));
    return f;
  }

 private:
  std::vector<std::unique_ptr<GameAudio>> audios_;
};

#endif
