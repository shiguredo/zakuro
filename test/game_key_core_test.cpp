// GameKeyCore の keys_ が複数スレッドから安全に扱われることを検証する。
// テストフレームワークは未導入のため、この実行ファイル単体で合否を返す。

#include "game/game_key_core.h"
#include "game/game_key.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

// 配送中の解除を繰り返す回数。
// GameKey 1 個の生成と破棄で Register と Unregister が 1 回ずつ走るため、
// 回数を増やすほど背景スレッドの走査と登録解除が重なる機会が増える。
constexpr int kDispatchLoops = 20000;

// pty を 1 組開いて標準入力をスレーブ側へ差し替える。
// GameKeyCore::Init の背景スレッドは標準入力を tcgetattr するため、
// 端末が無い環境ではキー入力の待ち受けを始めずに終了してしまう。
// テストでは pty を用意して端末がある状態を作る。
class PseudoTerminal {
 public:
  PseudoTerminal() = default;
  PseudoTerminal(const PseudoTerminal&) = delete;
  PseudoTerminal& operator=(const PseudoTerminal&) = delete;
  ~PseudoTerminal() { Restore(); }

  // pty を開き、標準入力をスレーブ側へ差し替える。
  bool Open() {
    // 標準入力が閉じていると、posix_openpt が fd 0 をマスターに割り当ててしまい、
    // 直後の dup2 がマスターを潰す。その状態では配送が届かないうえ、背景スレッドの
    // 終了時の tcsetattr(TCSADRAIN) がブロックしてテストが終わらなくなる。
    // fd 0 を /dev/null で埋めて確保しておき、Restore はこの fd へ戻す。
    if (fcntl(STDIN_FILENO, F_GETFD) < 0) {
      if (open("/dev/null", O_RDWR) != STDIN_FILENO) {
        return false;
      }
    }
    master_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ < 0) {
      return false;
    }
    if (grantpt(master_) != 0 || unlockpt(master_) != 0) {
      return false;
    }
    const char* slave_name = ptsname(master_);
    if (slave_name == nullptr) {
      return false;
    }
    slave_ = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_ < 0) {
      return false;
    }
    // 元の標準入力を保存してから差し替える。Restore で元に戻す。
    saved_stdin_ = dup(STDIN_FILENO);
    if (saved_stdin_ < 0) {
      return false;
    }
    if (dup2(slave_, STDIN_FILENO) < 0) {
      return false;
    }
    close(slave_);
    slave_ = -1;
    return true;
  }

  // マスターへ書いた内容は、標準入力へ差し替えたスレーブから読み出せる。
  bool Write(const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
      const ssize_t n =
          write(master_, data.data() + written, data.size() - written);
      if (n <= 0) {
        return false;
      }
      written += static_cast<size_t>(n);
    }
    return true;
  }

  // 背景スレッドが標準入力を非カノニカル・エコー無しに切り替えるまで待つ。
  // 切り替え前に書き込むと 2 つの問題が起きる。
  // 1 つ目はカノニカルバッファに溜まったまま配送されないこと。
  // 2 つ目は pty がエコーした内容がマスター側に溜まること。背景スレッドは終了時に
  // tcsetattr(TCSADRAIN) で端末設定を戻すが、pty ではマスター側に未読の出力が残っていると
  // その出力待ちでブロックする。エコーを止めてから書き込めばマスター側には何も溜まらない。
  bool WaitUntilRawInput(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      termios attr = {};
      if (tcgetattr(STDIN_FILENO, &attr) == 0 && (attr.c_lflag & ICANON) == 0 &&
          (attr.c_lflag & ECHO) == 0) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  }

  // 標準入力を元へ戻し、pty を閉じる。
  void Restore() {
    if (saved_stdin_ >= 0) {
      dup2(saved_stdin_, STDIN_FILENO);
      close(saved_stdin_);
      saved_stdin_ = -1;
    }
    if (slave_ >= 0) {
      close(slave_);
      slave_ = -1;
    }
    if (master_ >= 0) {
      close(master_);
      master_ = -1;
    }
  }

 private:
  int master_ = -1;
  int slave_ = -1;
  int saved_stdin_ = -1;
};

int Fail(const std::string& message) {
  std::cerr << "失敗: " << message << std::endl;
  return 1;
}

// 登録済みの GameKey が期待したキーを受け取るまで待つ。
// 配送済みのキーは毎回すべて取り出し、ストームで溜まった分を捌ききれないまま
// 待ち時間を使い切らないようにする。
bool WaitForKey(GameKey& key, int expected, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    while (true) {
      const int c = key.PopKey();
      if (c < 0) {
        break;
      }
      if (c == expected) {
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

// 複数のスレッドから GameKey の生成と破棄を繰り返す。
// GameKey のコンストラクタが Register、デストラクタが Unregister を呼ぶため、
// keys_ への追加と削除が並行しても壊れないことを確認する。
int TestConcurrentRegisterAndUnregister() {
  constexpr int kThreadCount = 8;
  constexpr int kLoops = 500;

  auto core = std::make_shared<GameKeyCore>();
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreadCount; i++) {
    threads.emplace_back([core]() {
      for (int j = 0; j < kLoops; j++) {
        GameKey key(core);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }

  std::cout << kThreadCount << " スレッドから " << kLoops
            << " 回ずつ登録と解除を繰り返してクラッシュしないことを確認した"
            << std::endl;
  return 0;
}

// pty からキー入力を配送し続けている間に GameKey の生成と破棄を繰り返し、
// keys_ の走査中に Register / Unregister が走っても壊れないことを確認する。
int RunDispatchChecks(const std::shared_ptr<GameKeyCore>& core,
                      PseudoTerminal& pty) {
  if (!pty.WaitUntilRawInput(std::chrono::seconds(2))) {
    return Fail("背景スレッドが標準入力を非カノニカルモードに切り替えなかった");
  }

  // まず配送経路そのものが動くことを確認する。
  {
    GameKey key(core);
    if (!pty.Write("a")) {
      return Fail("pty へキー入力を書き込めなかった");
    }
    if (!WaitForKey(key, 'a', std::chrono::seconds(2))) {
      return Fail("背景スレッドがキー入力を配送しなかった");
    }
  }

  // 配送を絶やさないために pty へ書き込み続ける。この間ずっと
  // 背景スレッドは keys_ を走査して配送する。
  std::atomic<bool> stop{false};
  std::thread writer([&pty, &stop]() {
    while (!stop) {
      if (!pty.Write("b")) {
        return;
      }
    }
  });

  // GameKey は破棄で Unregister、生成で Register を呼ぶ。
  // 配送と並行して生成と破棄を繰り返し、走査と登録解除が重なる状況を作る。
  for (int i = 0; i < kDispatchLoops; i++) {
    GameKey key(core);
  }

  stop = true;
  writer.join();

  // 解除を繰り返した後も配送が続いていることを確認する。
  {
    GameKey key(core);
    if (!pty.Write("c")) {
      return Fail("pty へキー入力を書き込めなかった");
    }
    if (!WaitForKey(key, 'c', std::chrono::seconds(2))) {
      return Fail("登録と解除を繰り返した後にキー入力が配送されなくなった");
    }
  }
  return 0;
}

// キー入力の配送中に GameKey が破棄されても壊れないことを確認する。
int TestDispatchWhileUnregistering() {
  PseudoTerminal pty;
  if (!pty.Open()) {
    return Fail("pty を用意できなかった");
  }

  auto core = std::make_shared<GameKeyCore>();
  core->Init();

  const int result = RunDispatchChecks(core, pty);

  // 背景スレッドの終了処理は tcsetattr(TCSADRAIN) で端末設定を戻す。
  // pty のマスター側に未読の出力があるとここでブロックするが、
  // PseudoTerminal::WaitUntilRawInput のコメントのとおりエコーを止めてからしか
  // 書き込まないため、標準入力を pty に差し替えたまま止めてよい。
  core->Reset();

  if (result != 0) {
    return result;
  }
  std::cout << "キー入力の配送中に " << kDispatchLoops
            << " 回の登録と解除を繰り返してクラッシュしないことを確認した"
            << std::endl;
  return 0;
}

}  // namespace

int main() {
  if (const int result = TestConcurrentRegisterAndUnregister()) {
    return result;
  }
  if (const int result = TestDispatchWhileUnregistering()) {
    return result;
  }
  std::cout << "すべてのテストに成功した" << std::endl;
  return 0;
}
