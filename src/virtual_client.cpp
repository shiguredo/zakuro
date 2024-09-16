#include "virtual_client.h"

#include <iostream>

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
#include <rtc_base/logging.h>
#include <rtc_base/crypto_random.h>

std::shared_ptr<VirtualClient> VirtualClient::Create(
    VirtualClientConfig config) {
  return std::shared_ptr<VirtualClient>(new VirtualClient(config));
}
VirtualClient::VirtualClient(const VirtualClientConfig& config)
    : config_(config), retry_timer_(*config.sora_config.io_context) {}

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
    cricket::AudioOptions ao;
    if (config_.disable_echo_cancellation)
      ao.echo_cancellation = false;
    if (config_.disable_auto_gain_control)
      ao.auto_gain_control = false;
    if (config_.disable_noise_suppression)
      ao.noise_suppression = false;
    if (config_.disable_highpass_filter)
      ao.highpass_filter = false;
    std::string audio_track_id = rtc::CreateRandomString(16);
    audio_track_ = config_.context->peer_connection_factory()->CreateAudioTrack(
        audio_track_id, config_.context->peer_connection_factory()
                            ->CreateAudioSource(ao)
                            .get());
  }
  if (!config_.no_video_device) {
    std::string video_track_id = rtc::CreateRandomString(16);
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
  st.channel_id = config_.sora_config.channel_id;
  st.connection_id = signaling_->GetConnectionID();
  st.connected_url = signaling_->GetConnectedSignalingURL();
  st.datachannel_connected = signaling_->IsConnectedDataChannel();
  st.websocket_connected = signaling_->IsConnectedWebsocket();
  return st;
}

void VirtualClient::OnSetOffer(std::string offer) {
  std::string stream_id = rtc::CreateRandomString(16);
  if (audio_track_ != nullptr) {
    if (config_.initial_mute_audio) {
      audio_track_->set_enabled(false);
    }
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_result = signaling_->GetPeerConnection()->AddTrack(audio_track_,
                                                                 {stream_id});
  }
  if (video_track_ != nullptr) {
    if (config_.initial_mute_video) {
      video_track_->set_enabled(false);
    }
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_result = signaling_->GetPeerConnection()->AddTrack(video_track_,
                                                                 {stream_id});
    if (video_result.ok()) {
      rtc::scoped_refptr<webrtc::RtpSenderInterface> video_sender =
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
