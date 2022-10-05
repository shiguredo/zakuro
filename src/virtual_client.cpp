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

#include "dynamic_h264_video_encoder.h"
#include "fake_network_call_factory.h"
#include "nop_video_decoder.h"
#include "sctp_transport_factory.h"

VirtualClient::VirtualClient(VirtualClientConfig config)
    : sora::SoraDefaultClient(config_), config_(config) {}

void VirtualClient::Connect() {
  if (closing_) {
    return;
  }

  if (signaling_) {
    closing_ = true;
    need_reconnect_ = true;
    signaling_->Disconnect();
    return;
  }

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
    audio_track_ = factory()->CreateAudioTrack(
        audio_track_id, factory()->CreateAudioSource(ao).get());
  }
  if (!config_.no_video_device) {
    std::string video_track_id = rtc::CreateRandomString(16);
    video_track_ =
        factory()->CreateVideoTrack(video_track_id, config_.capturer.get());

    if (config_.fixed_resolution) {
      video_track_->set_content_hint(
          webrtc::VideoTrackInterface::ContentHint::kText);
    }
  }

  sora::SoraSignalingConfig config = config_.sora_config;
  config.pc_factory = factory();
  config.observer = shared_from_this();
  config.network_manager = signaling_thread()->Invoke<rtc::NetworkManager*>(
      RTC_FROM_HERE,
      [this]() { return connection_context()->default_network_manager(); });
  config.socket_factory = signaling_thread()->Invoke<rtc::PacketSocketFactory*>(
      RTC_FROM_HERE,
      [this]() { return connection_context()->default_socket_factory(); });

  signaling_ = sora::SoraSignaling::Create(config);
  signaling_->Connect();
}

void VirtualClient::Close() {
  if (closing_) {
    return;
  }
  if (signaling_) {
    closing_ = true;
    signaling_->Disconnect();
  }
}

void VirtualClient::Clear() {
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
  // TODO(melpon): Sora C++ SDK に関数を追加して実装する
  //st.connection_id = singaling_->GetConnectionID();
  //st.connected_url = singaling_->GetConnectedSignalingURL();
  //st.datachannel_connected = singaling_->IsConnectedDataChannel();
  //st.websocket_connected = singaling_->IsConnectedWebsocket();
  return st;
}

void VirtualClient::ConfigureDependencies(
    webrtc::PeerConnectionFactoryDependencies& dependencies) {
  cricket::MediaEngineDependencies media_dependencies;

  media_dependencies.task_queue_factory = dependencies.task_queue_factory.get();
  media_dependencies.adm =
      worker_thread()->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(
          RTC_FROM_HERE, [&] {
            ZakuroAudioDeviceModuleConfig admconfig;
            admconfig.task_queue_factory =
                dependencies.task_queue_factory.get();
            if (config_.audio_type == VirtualClientConfig::AudioType::Device) {
#if defined(__linux__)
              webrtc::AudioDeviceModule::AudioLayer audio_layer =
                  webrtc::AudioDeviceModule::kLinuxAlsaAudio;
#else
              webrtc::AudioDeviceModule::AudioLayer audio_layer =
                  webrtc::AudioDeviceModule::kPlatformDefaultAudio;
#endif
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::ADM;
              admconfig.adm = webrtc::AudioDeviceModule::Create(
                  audio_layer, dependencies.task_queue_factory.get());
            } else if (config_.audio_type ==
                       VirtualClientConfig::AudioType::NoAudio) {
              webrtc::AudioDeviceModule::AudioLayer audio_layer =
                  webrtc::AudioDeviceModule::kDummyAudio;
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::ADM;
              admconfig.adm = webrtc::AudioDeviceModule::Create(
                  audio_layer, dependencies.task_queue_factory.get());
            } else if (config_.audio_type ==
                       VirtualClientConfig::AudioType::SpecifiedFakeAudio) {
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::FakeAudio;
              admconfig.fake_audio = config_.fake_audio;
            } else if (config_.audio_type ==
                       VirtualClientConfig::AudioType::AutoGenerateFakeAudio) {
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::Safari;
            } else if (config_.audio_type ==
                       VirtualClientConfig::AudioType::External) {
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::External;
              admconfig.render = config_.render_audio;
              admconfig.sample_rate = config_.sample_rate;
              admconfig.channels = config_.channels;
            }
            return ZakuroAudioDeviceModule::Create(std::move(admconfig));
          });
  media_dependencies.audio_encoder_factory =
      webrtc::CreateBuiltinAudioEncoderFactory();
  media_dependencies.audio_decoder_factory =
      webrtc::CreateBuiltinAudioDecoderFactory();

  auto sw_config = sora::GetSoftwareOnlyVideoEncoderFactoryConfig();
  sw_config.use_simulcast_adapter = true;
  sw_config.encoders.push_back(sora::VideoEncoderConfig(
      webrtc::kVideoCodecH264,
      [openh264 = config_.openh264](
          auto format) -> std::unique_ptr<webrtc::VideoEncoder> {
        return webrtc::DynamicH264VideoEncoder::Create(
            cricket::VideoCodec(format), openh264);
      }));
  media_dependencies.video_encoder_factory =
      absl::make_unique<sora::SoraVideoEncoderFactory>(std::move(sw_config));
  media_dependencies.video_decoder_factory.reset(new NopVideoDecoderFactory());

  media_dependencies.audio_mixer = nullptr;
  media_dependencies.audio_processing =
      webrtc::AudioProcessingBuilder().Create();

  dependencies.media_engine =
      cricket::CreateMediaEngine(std::move(media_dependencies));

  dependencies.call_factory = CreateFakeNetworkCallFactory(
      config_.fake_network_send, config_.fake_network_receive);
  dependencies.sctp_factory.reset(new SctpTransportFactory(network_thread()));
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
  closing_ = false;
  if (need_reconnect_) {
    need_reconnect_ = false;
    Connect();
  }
}
