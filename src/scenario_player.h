#ifndef SCENARIO_PLAYER_H_
#define SCENARIO_PLAYER_H_

#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <vector>

// Boost
#include <boost/asio.hpp>
#include <boost/variant.hpp>

#include "binary_pool.h"
#include "game/game_audio.h"
#include "virtual_client.h"
#include "voice_number_reader.h"

struct ScenarioData {
  struct OpSleep {
    int min_time_ms;
    int max_time_ms;
  };
  struct OpPlaySubScenario {
    std::string name;
    // 不完全型回避のために shared_ptr にする
    std::shared_ptr<ScenarioData> data;
    int loop_op_index;
  };
  struct OpPlayVoiceNumberClient {};
  struct OpSendDataChannelMessage {
    std::string label;
    int min_size;
    int max_size;
  };
  struct OpDisconnect {};
  struct OpReconnect {};
  struct OpExit {};
  enum Type {
    OP_SLEEP,
    OP_PLAY_SUB_SCENARIO,
    OP_PLAY_VOICE_NUMBER_CLIENT,
    OP_SEND_DATA_CHANNEL_MESSAGE,
    OP_DISCONNECT,
    OP_RECONNECT,
    OP_EXIT,
  };

  typedef boost::variant<OpSleep,
                         OpPlaySubScenario,
                         OpPlayVoiceNumberClient,
                         OpSendDataChannelMessage,
                         OpDisconnect,
                         OpReconnect,
                         OpExit>
      operation_t;
  std::vector<operation_t> ops;

  void Sleep(int min_time_ms, int max_time_ms) {
    ops.push_back(OpSleep{min_time_ms, max_time_ms});
  }
  void PlaySubScenario(std::string name,
                       const ScenarioData& data,
                       int loop_op_index) {
    ops.push_back(OpPlaySubScenario{
        std::move(name), std::shared_ptr<ScenarioData>(new ScenarioData(data)),
        loop_op_index});
  }
  void PlayVoiceNumberClient() { ops.push_back(OpPlayVoiceNumberClient()); }
  void SendDataChannelMessage(std::string label, int min_size, int max_size) {
    ops.push_back(
        OpSendDataChannelMessage{std::move(label), min_size, max_size});
  }
  void Disconnect() { ops.push_back(OpDisconnect()); }
  void Reconnect() { ops.push_back(OpReconnect()); }
  void Exit() { ops.push_back(OpExit()); }
};

struct ScenarioPlayerConfig {
  boost::asio::io_context* ioc;
  GameAudioManager* gam;
  const std::vector<std::shared_ptr<VirtualClient>>* vcs;
  std::shared_ptr<BinaryPool> binary_pool;
};

class ScenarioPlayer {
 public:
  ScenarioPlayer(const ScenarioPlayerConfig& config)
      : config_(config), engine_(std::random_device()()) {
    for (int i = 0; i < config_.vcs->size(); i++) {
      client_infos_.push_back(ClientInfo(*config_.ioc));
    }
  }

  void Play(int client_id, ScenarioData data, int loop_op_index) {
    if (client_id < 0 || client_id >= client_infos_.size() ||
        data.ops.empty() || loop_op_index < 0 ||
        loop_op_index >= data.ops.size()) {
      return;
    }

    client_infos_[client_id].data = std::move(data);
    client_infos_[client_id].op_index = 0;
    client_infos_[client_id].loop_op_index = loop_op_index;
    client_infos_[client_id].timer.cancel();
    client_infos_[client_id].paused = false;
    client_infos_[client_id].exit = false;
    DoNext(client_id);
  }
  void PauseAll() {
    for (auto& info : client_infos_) {
      info.timer.cancel();
      info.paused = true;
    }
  }
  void ResumeAll() {
    for (int i = 0; i < client_infos_.size(); i++) {
      client_infos_[i].paused = false;
      DoNext(i);
    }
  }

 private:
  void Next(int client_id) {
    auto& info = client_infos_[client_id];
    if (info.paused) {
      return;
    }

    info.op_index += 1;
    if (info.op_index == info.data.ops.size()) {
      info.op_index = info.loop_op_index;
    }
    DoNext(client_id);
  }
  void DoNext(int client_id) {
    boost::asio::post(*config_.ioc,
                      std::bind(&ScenarioPlayer::OnNext, this, client_id));
  }

  void OnNext(int client_id) {
    auto& info = client_infos_[client_id];

    auto& opv = info.data.ops[info.op_index];
    switch (opv.which()) {
      case ScenarioData::OP_SLEEP: {
        auto& op = boost::get<ScenarioData::OpSleep>(opv);
        auto ms =
            engine_() % (op.max_time_ms - op.min_time_ms + 1) + op.min_time_ms;
        info.timer.expires_from_now(boost::posix_time::milliseconds(ms));
        info.timer.async_wait(
            [this, client_id](const boost::system::error_code& ec) {
              if (ec == boost::asio::error::operation_aborted) {
                return;
              }
              Next(client_id);
            });
        return;
      }
      case ScenarioData::OP_PLAY_SUB_SCENARIO: {
        auto& op = boost::get<ScenarioData::OpPlaySubScenario>(opv);
        if (sub_scenario_[op.name] == nullptr) {
          sub_scenario_[op.name] = std::make_shared<ScenarioPlayer>(config_);
        }
        sub_scenario_[op.name]->Play(client_id, *op.data, op.loop_op_index);
        break;
      }
      case ScenarioData::OP_PLAY_VOICE_NUMBER_CLIENT: {
        std::vector<int16_t> buf = voice_reader_.Read(client_id + 1);
        config_.gam->Play(client_id, buf);
        break;
      }
      case ScenarioData::OP_SEND_DATA_CHANNEL_MESSAGE: {
        auto& op = boost::get<ScenarioData::OpSendDataChannelMessage>(opv);
        std::string data =
            config_.binary_pool->Get(op.min_size - 48, op.max_size - 48);

        // 0-5 (6 bytes): ZAKURO
        // 6-13 (8 bytes): 現在時刻（マイクロ秒単位の UNIX Time）
        // 14-21 (8 bytes): ラベルごとに一意に増えていくカウンター
        // 22-47 (26 bytes): Connection ID
        char buf[48] = "ZAKURO";
        char* p = buf + 6;

        uint64_t time = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
        for (int i = 0; i < 8; i++) {
          p[i] = (char)((time >> ((7 - i) * 8)) & 0xff);
        }
        p += 8;

        auto& counter = dc_counter_[op.label];
        for (int i = 0; i < 8; i++) {
          p[i] = (char)((counter >> ((7 - i) * 8)) & 0xff);
        }
        p += 8;

        auto conn_id = (*config_.vcs)[client_id]->GetStats().connection_id;
        memcpy(p, conn_id.c_str(), std::min<int>(conn_id.size(), 26));

        data.insert(data.begin(), buf, buf + sizeof(buf));

        RTC_LOG(LS_INFO) << "Send DataChannel unixtime(us)=" << time
                         << " counter=" << counter
                         << " connection_id=" << conn_id;
        (*config_.vcs)[client_id]->SendMessage(op.label, data);
        counter += 1;
        break;
      }
      case ScenarioData::OP_DISCONNECT: {
        (*config_.vcs)[client_id]->Close();
        break;
      }
      case ScenarioData::OP_RECONNECT: {
        (*config_.vcs)[client_id]->Connect();
        break;
      }
      case ScenarioData::OP_EXIT: {
        (*config_.vcs)[client_id]->Close([this,
                                          client_id](std::string message) {
          RTC_LOG(LS_INFO) << "Client " << client_id << " exited: " << message;
          client_infos_[client_id].exit = true;
          bool all_exited =
              std::all_of(client_infos_.begin(), client_infos_.end(),
                          [](auto& info) { return info.exit; });
          if (all_exited) {
            config_.ioc->stop();
          }
        });

        break;
      }
    }

    Next(client_id);
  }

 private:
  ScenarioPlayerConfig config_;
  std::mt19937 engine_;

  struct ClientInfo {
    ClientInfo(boost::asio::io_context& ioc) : timer(ioc) {}

    ScenarioData data;
    int op_index;
    int loop_op_index;
    boost::asio::deadline_timer timer;
    bool paused;
    bool exit;
  };
  std::vector<ClientInfo> client_infos_;
  VoiceNumberReader voice_reader_;
  std::map<std::string, std::shared_ptr<ScenarioPlayer>> sub_scenario_;
  std::map<std::string, uint64_t> dc_counter_;
};

#endif
