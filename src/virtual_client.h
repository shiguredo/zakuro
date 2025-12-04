#ifndef VIRTUAL_CLIENT_H_
#define VIRTUAL_CLIENT_H_

#include <memory>

// Sora C++ SDK
#include <sora/sora_client_context.h>
#include <sora/sora_signaling.h>

// Boost
#include <boost/asio/io_context.hpp>

#include "zakuro_audio_device_module.h"

// 前方宣言
class DuckDBStatsWriter;

struct VirtualClientStats {
  std::string channel_id;
  std::string connection_id;
  std::string session_id;
  std::string connected_url;
  std::string role;
  bool has_audio_track = false;
  bool has_video_track = false;
  bool websocket_connected = false;
  bool datachannel_connected = false;
};

struct VirtualClientConfig {
  webrtc::scoped_refptr<webrtc::VideoTrackSourceInterface> capturer;
  sora::SoraSignalingConfig sora_config;

  int max_retry = 0;
  double retry_interval = 60;

  bool fixed_resolution = false;

  std::string priority = "BALANCE";

  bool no_video_device = false;
  bool disable_echo_cancellation = false;
  bool disable_auto_gain_control = false;
  bool disable_noise_suppression = false;
  bool disable_highpass_filter = false;

  bool initial_mute_video = false;
  bool initial_mute_audio = false;

  enum class AudioType {
    NoAudio,
    SpecifiedFakeAudio,
    AutoGenerateFakeAudio,
    Device,
    External,
  };
  AudioType audio_type = AudioType::AutoGenerateFakeAudio;
  std::shared_ptr<FakeAudioData> fake_audio;
  std::function<void(std::vector<int16_t>&)> render_audio;
  int sample_rate;
  int channels;

  std::string openh264;

  std::shared_ptr<sora::SoraClientContext> context;

  // DuckDB 統計出力
  std::shared_ptr<DuckDBStatsWriter> duckdb_writer;
  double duckdb_interval = 1.0;
};

class VirtualClient : public std::enable_shared_from_this<VirtualClient>,
                      public sora::SoraSignalingObserver {
 public:
  static std::shared_ptr<VirtualClient> Create(VirtualClientConfig config);

  void Connect();
  void Close(std::function<void(std::string)> on_close = nullptr);
  void Clear();
  void SendMessage(const std::string& label, const std::string& data);

  VirtualClientStats GetStats() const;

  void OnSetOffer(std::string offer) override;
  void OnDisconnect(sora::SoraSignalingErrorCode ec,
                    std::string message) override;
  void OnNotify(std::string text) override;
  void OnPush(std::string text) override {}
  void OnMessage(std::string label, std::string data) override {}

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {}
  void OnRemoveTrack(
      webrtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) override {}

  void OnDataChannel(std::string label) override {}

 private:
  VirtualClient(const VirtualClientConfig& config);

  // RTC 統計収集タイマーの開始
  void StartRTCStatsTimer();
  // RTC 統計収集タイマーの停止
  void StopRTCStatsTimer();
  // RTC 統計の収集と DuckDB への書き込み
  void CollectAndWriteRTCStats();

  VirtualClientConfig config_;
  bool closing_ = false;
  bool need_reconnect_ = false;
  int retry_count_ = 0;
  std::function<void(std::string)> on_close_;
  boost::asio::steady_timer retry_timer_;
  boost::asio::steady_timer rtc_stats_timer_;
  std::shared_ptr<sora::SoraSignaling> signaling_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::string session_id_;
};

#endif
