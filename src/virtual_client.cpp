#include "virtual_client.h"

#include <chrono>
#include <iostream>
#include <mutex>

#include <boost/json.hpp>

#include "duckdb_stats_writer.h"

// Sora C++ SDK
#include <sora/sora_video_encoder_factory.h>

// WebRTC
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <rtc_base/crypto_random.h>
#include <rtc_base/logging.h>

std::shared_ptr<VirtualClient> VirtualClient::Create(
    VirtualClientConfig config) {
  return std::shared_ptr<VirtualClient>(new VirtualClient(config));
}
VirtualClient::VirtualClient(const VirtualClientConfig& config)
    : config_(config),
      retry_timer_(*config.sora_config.io_context),
      rtc_stats_timer_(
          new boost::asio::deadline_timer(*config.sora_config.io_context)),
      role_(config.sora_config.role) {}

void VirtualClient::Connect() {
  if (closing_) {
    need_reconnect_ = true;
    return;
  }

  if (signaling_) {
    closing_ = true;
    need_reconnect_ = true;
    signaling_->Disconnect();
    return;
  }

  retry_timer_.cancel();

  if (config_.audio_type != VirtualClientConfig::AudioType::NoAudio) {
    webrtc::AudioOptions ao;
    if (config_.disable_echo_cancellation)
      ao.echo_cancellation = false;
    if (config_.disable_auto_gain_control)
      ao.auto_gain_control = false;
    if (config_.disable_noise_suppression)
      ao.noise_suppression = false;
    if (config_.disable_highpass_filter)
      ao.highpass_filter = false;
    std::string audio_track_id = webrtc::CreateRandomString(16);
    audio_track_ = config_.context->peer_connection_factory()->CreateAudioTrack(
        audio_track_id, config_.context->peer_connection_factory()
                            ->CreateAudioSource(ao)
                            .get());
  }
  if (!config_.no_video_device) {
    std::string video_track_id = webrtc::CreateRandomString(16);
    video_track_ = config_.context->peer_connection_factory()->CreateVideoTrack(
        config_.capturer, video_track_id);

    if (config_.fixed_resolution) {
      video_track_->set_content_hint(
          webrtc::VideoTrackInterface::ContentHint::kText);
    }
  }

  sora::SoraSignalingConfig config = config_.sora_config;
  config.pc_factory = config_.context->peer_connection_factory();
  config.observer = shared_from_this();
  config.network_manager =
      config_.context->signaling_thread()->BlockingCall([this]() {
        return config_.context->connection_context()->default_network_manager();
      });
  config.socket_factory =
      config_.context->signaling_thread()->BlockingCall([this]() {
        return config_.context->connection_context()->default_socket_factory();
      });

  signaling_ = sora::SoraSignaling::Create(config);
  signaling_->Connect();
}

void VirtualClient::Close(std::function<void(std::string)> on_close) {
  if (closing_) {
    if (on_close_ == nullptr) {
      on_close_ = on_close;
    } else {
      on_close("already closing");
    }
    return;
  }
  if (signaling_) {
    closing_ = true;
    on_close_ = on_close;
    signaling_->Disconnect();
  } else {
    on_close("already closed");
  }
}

void VirtualClient::Clear() {
  retry_timer_.cancel();
  {
    std::lock_guard<std::mutex> lock(rtc_stats_timer_mutex_);
    if (rtc_stats_timer_) {
      rtc_stats_timer_->cancel();
    }
  }
  signaling_.reset();
}

void VirtualClient::SendMessage(const std::string& label,
                                const std::string& data) {
  if (signaling_ == nullptr || closing_) {
    return;
  }
  signaling_->SendDataChannel(label, data);
}

VirtualClientStats VirtualClient::GetStats() const {
  if (signaling_ == nullptr) {
    return VirtualClientStats();
  }
  VirtualClientStats st;

  // OnSetOffer で保存した情報を使用
  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    st.channel_id = channel_id_;
    st.connection_id = connection_id_;
    st.session_id = session_id_;
    st.role = role_;
    st.has_audio_track = has_audio_;
    st.has_video_track = has_video_;
  }

  st.connected_url = signaling_->GetConnectedSignalingURL();
  st.datachannel_connected = signaling_->IsConnectedDataChannel();
  st.websocket_connected = signaling_->IsConnectedWebsocket();

  return st;
}

void VirtualClient::OnSetOffer(std::string offer) {

  // offer メッセージから channel_id, connection_id, session_id, audio, video を取得
  auto json = boost::json::parse(offer);
  {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (json.as_object().contains("channel_id")) {
      channel_id_ = json.at("channel_id").as_string().c_str();
    }
    if (json.as_object().contains("connection_id")) {
      connection_id_ = json.at("connection_id").as_string().c_str();
    }
    if (json.as_object().contains("session_id")) {
      session_id_ = json.at("session_id").as_string().c_str();
    }
    if (json.as_object().contains("audio")) {
      has_audio_ = json.at("audio").as_bool();
    }
    if (json.as_object().contains("video")) {
      has_video_ = json.at("video").as_bool();
    }
  }

  // type:offer 時点で DuckDB に書き込む
  if (config_.duckdb_writer && !connection_id_.empty()) {
    std::vector<VirtualClientStats> stats;
    stats.push_back(GetStats());
    config_.duckdb_writer->WriteStats(stats);

    // WebRTC統計情報の定期取得を開始
    StartRTCStatsTimer();
  }

  std::string stream_id = webrtc::CreateRandomString(16);
  if (audio_track_ != nullptr) {
    if (config_.initial_mute_audio) {
      audio_track_->set_enabled(false);
    }
    webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_result = signaling_->GetPeerConnection()->AddTrack(audio_track_,
                                                                 {stream_id});
  }
  if (video_track_ != nullptr) {
    if (config_.initial_mute_video) {
      video_track_->set_enabled(false);
    }
    webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result = signaling_->GetPeerConnection()->AddTrack(video_track_,
                                                                 {stream_id});
    if (video_result.ok()) {
      webrtc::scoped_refptr<webrtc::RtpSenderInterface> video_sender =
          video_result.value();
      webrtc::RtpParameters parameters = video_sender->GetParameters();
      if (config_.priority == "FRAMERATE") {
        parameters.degradation_preference =
            webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
      } else if (config_.priority == "RESOLUTION") {
        parameters.degradation_preference =
            webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
      } else {
        parameters.degradation_preference =
            webrtc::DegradationPreference::BALANCED;
      }
      video_sender->SetParameters(parameters);
    }
  }
}
void VirtualClient::OnDisconnect(sora::SoraSignalingErrorCode ec,
                                 std::string message) {
  signaling_.reset();
  retry_timer_.cancel();
  
  // RTC統計情報タイマーをキャンセル
  {
    std::lock_guard<std::mutex> lock(rtc_stats_timer_mutex_);
    if (rtc_stats_timer_) {
      rtc_stats_timer_->cancel();
    }
  }

  if (!closing_) {
    // VirtualClient の外から明示的に呼び出されていない、つまり不意に接続が切れた場合にここに来る
    // この場合は、設定次第で再接続を試みる
    if (retry_count_ < config_.max_retry) {
      retry_count_ += 1;
      retry_timer_.expires_from_now(boost::posix_time::milliseconds(
          (int)(config_.retry_interval * 1000)));
      retry_timer_.async_wait([this](boost::system::error_code ec) {
        if (ec) {
          return;
        }
        need_reconnect_ = false;
        if (on_close_ == nullptr) {
          Connect();
        }
      });
    }
  } else {
    closing_ = false;
    if (need_reconnect_) {
      need_reconnect_ = false;
      if (on_close_ == nullptr) {
        Connect();
      }
    }
  }

  if (on_close_) {
    on_close_(message);
    on_close_ = nullptr;
  }
}
void VirtualClient::OnNotify(std::string text) {
  auto json = boost::json::parse(text);
  if (json.at("event_type").as_string() == "connection.created") {
    // 接続できたらリトライ数をリセットする
    // 他人が接続された時もリセットされることになるけど、
    // その時は 0 のままになってるはずなので問題ない
    retry_count_ = 0;
  }
}

void VirtualClient::StartRTCStatsTimer() {
  std::lock_guard<std::mutex> lock(rtc_stats_timer_mutex_);
  if (!signaling_ || !rtc_stats_timer_ || !config_.duckdb_writer) {
    return;
  }

  rtc_stats_timer_->expires_from_now(
      boost::posix_time::seconds(config_.rtc_stats_interval));
  rtc_stats_timer_->async_wait(
      [this](const boost::system::error_code& ec) { OnRTCStatsTimer(ec); });
}

void VirtualClient::OnRTCStatsTimer(const boost::system::error_code& ec) {
  if (ec || closing_ || !signaling_) {
    return;
  }

  // WebRTC統計情報を取得
  auto pc = signaling_->GetPeerConnection();
  if (pc) {
    auto callback =
        webrtc::make_ref_counted<StatsCollectorCallback>(shared_from_this());
    pc->GetStats(callback.get());
  }

  // 次回のタイマーを設定
  StartRTCStatsTimer();
}

// StatsCollectorCallback の実装
void StatsCollectorCallback::OnStatsDelivered(
    const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report) {
  auto client = client_.lock();
  if (!client || !client->config_.duckdb_writer) {
    return;
  }

  // GetStats() から情報を取得
  VirtualClientStats client_stats = client->GetStats();
  std::string channel_id = client_stats.channel_id;
  std::string session_id = client_stats.session_id;
  std::string connection_id = client_stats.connection_id;

  if (connection_id.empty()) {
    return;
  }

  // 現在時刻を取得（すべてのレコードで共通のtimestampを使用）
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  double timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() /
      1000.0;

  // レポートから必要な統計情報を抽出
  for (const auto& stats : *report) {
    const std::string type = stats.type();

    // フィルタリング: inbound-rtp, outbound-rtp, codec, media-source, remote-inbound-rtp, remote-outbound-rtp, data-channel のみ
    if (type != "inbound-rtp" && type != "outbound-rtp" && type != "codec" &&
        type != "media-source" && type != "remote-inbound-rtp" && 
        type != "remote-outbound-rtp" && type != "data-channel") {
      continue;
    }

    // RTCStats を JSON 文字列として取得
    std::string json_str = stats.ToJson();
    double rtc_timestamp =
        stats.timestamp().us() / 1000000.0;  // rtc_timestampは別カラムで保存

    // DuckDBにJSON文字列を保存（共通のtimestampを使用）
    client->config_.duckdb_writer->WriteRTCStats(
        channel_id, session_id, connection_id, type, rtc_timestamp, json_str,
        timestamp);
  }
}
