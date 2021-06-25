#include "rtc_manager.h"

#include <iostream>

// WebRTC
#include <absl/memory/memory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <api/task_queue/default_task_queue_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_track_source_proxy.h>
#include <media/engine/webrtc_media_engine.h>
#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/include/audio_device_factory.h>
#include <modules/audio_processing/include/audio_processing.h>
#include <modules/video_capture/video_capture.h>
#include <modules/video_capture/video_capture_factory.h>
#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>

#include "fake_network_call_factory.h"
#include "nop_video_decoder.h"
#include "peer_connection_observer.h"
#include "rtc_ssl_verifier.h"
#include "scalable_track_source.h"
#include "sctp_transport_factory.h"
#include "software_video_encoder.h"
#include "util.h"

RTCManager::RTCManager(
    RTCManagerConfig config,
    rtc::scoped_refptr<ScalableVideoTrackSource> video_track_source,
    VideoTrackReceiver* receiver)
    : config_(std::move(config)), receiver_(receiver) {
  rtc::InitializeSSL();

  network_thread_ = rtc::Thread::CreateWithSocketServer();
  network_thread_->Start();
  worker_thread_ = rtc::Thread::Create();
  worker_thread_->Start();
  signaling_thread_ = rtc::Thread::Create();
  signaling_thread_->Start();

  webrtc::PeerConnectionFactoryDependencies dependencies;
  dependencies.network_thread = network_thread_.get();
  dependencies.worker_thread = worker_thread_.get();
  dependencies.signaling_thread = signaling_thread_.get();
  dependencies.task_queue_factory = webrtc::CreateDefaultTaskQueueFactory();
  dependencies.call_factory = webrtc::CreateCallFactory();
  dependencies.event_log_factory =
      absl::make_unique<webrtc::RtcEventLogFactory>(
          dependencies.task_queue_factory.get());

  // media_dependencies
  cricket::MediaEngineDependencies media_dependencies;
  media_dependencies.task_queue_factory = dependencies.task_queue_factory.get();
  media_dependencies.adm =
      worker_thread_->Invoke<rtc::scoped_refptr<webrtc::AudioDeviceModule>>(
          RTC_FROM_HERE, [&] {
            ZakuroAudioDeviceModuleConfig admconfig;
            admconfig.task_queue_factory =
                dependencies.task_queue_factory.get();
            if (config_.audio_type == RTCManagerConfig::AudioType::Device) {
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
                       RTCManagerConfig::AudioType::NoAudio) {
              webrtc::AudioDeviceModule::AudioLayer audio_layer =
                  webrtc::AudioDeviceModule::kDummyAudio;
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::ADM;
              admconfig.adm = webrtc::AudioDeviceModule::Create(
                  audio_layer, dependencies.task_queue_factory.get());
            } else if (config_.audio_type ==
                       RTCManagerConfig::AudioType::SpecifiedFakeAudio) {
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::FakeAudio;
              admconfig.fake_audio = config_.fake_audio;
            } else if (config_.audio_type ==
                       RTCManagerConfig::AudioType::AutoGenerateFakeAudio) {
              admconfig.type = ZakuroAudioDeviceModuleConfig::Type::Safari;
            } else if (config_.audio_type ==
                       RTCManagerConfig::AudioType::External) {
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

  media_dependencies.video_encoder_factory.reset(
      new SoftwareVideoEncoderFactory(config_.openh264, config_.simulcast));
  media_dependencies.video_decoder_factory.reset(new NopVideoDecoderFactory());

  media_dependencies.audio_mixer = nullptr;
  media_dependencies.audio_processing =
      webrtc::AudioProcessingBuilder().Create();

  dependencies.media_engine =
      cricket::CreateMediaEngine(std::move(media_dependencies));

  dependencies.call_factory = CreateFakeNetworkCallFactory(
      config_.fake_network_send, config_.fake_network_receive);
  dependencies.sctp_factory.reset(
      new SctpTransportFactory(network_thread_.get(), config_.use_dcsctp));

  factory_ =
      webrtc::CreateModularPeerConnectionFactory(std::move(dependencies));
  if (!factory_.get()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__
                      << ": Failed to initialize PeerConnectionFactory";
    exit(1);
  }

  webrtc::PeerConnectionFactoryInterface::Options factory_options;
  factory_options.disable_encryption = false;
  factory_options.ssl_max_version = rtc::SSL_PROTOCOL_DTLS_12;
  factory_->SetOptions(factory_options);

  if (config_.audio_type != RTCManagerConfig::AudioType::NoAudio) {
    cricket::AudioOptions ao;
    if (config_.disable_echo_cancellation)
      ao.echo_cancellation = false;
    if (config_.disable_auto_gain_control)
      ao.auto_gain_control = false;
    if (config_.disable_noise_suppression)
      ao.noise_suppression = false;
    if (config_.disable_highpass_filter)
      ao.highpass_filter = false;
    if (config_.disable_typing_detection)
      ao.typing_detection = false;
    if (config_.disable_residual_echo_detector)
      ao.residual_echo_detector = false;
    RTC_LOG(LS_INFO) << __FUNCTION__ << ": " << ao.ToString();
    audio_track_ = factory_->CreateAudioTrack(Util::GenerateRandomChars(),
                                              factory_->CreateAudioSource(ao));
    if (!audio_track_) {
      RTC_LOG(LS_WARNING) << __FUNCTION__ << ": Cannot create audio_track";
    }
  }

  if (video_track_source && !config_.no_video_device) {
    rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> video_source =
        webrtc::VideoTrackSourceProxy::Create(
            signaling_thread_.get(), worker_thread_.get(), video_track_source);
    video_track_ =
        factory_->CreateVideoTrack(Util::GenerateRandomChars(), video_source);
    if (video_track_) {
      if (config_.fixed_resolution) {
        video_track_->set_content_hint(
            webrtc::VideoTrackInterface::ContentHint::kText);
      }
      if (receiver_ != nullptr && config_.show_me) {
        receiver_->AddTrack(video_track_);
      }
    } else {
      RTC_LOG(LS_WARNING) << __FUNCTION__ << ": Cannot create video_track";
    }
  }
}

RTCManager::~RTCManager() {
  audio_track_ = nullptr;
  video_track_ = nullptr;
  factory_ = nullptr;
  network_thread_->Stop();
  worker_thread_->Stop();
  signaling_thread_->Stop();

  rtc::CleanupSSL();
}

void RTCManager::AddDataManager(std::shared_ptr<RTCDataManager> data_manager) {
  data_manager_dispatcher_.Add(data_manager);
}

std::shared_ptr<RTCConnection> RTCManager::CreateConnection(
    webrtc::PeerConnectionInterface::RTCConfiguration rtc_config,
    RTCMessageSender* sender) {
  rtc_config.enable_dtls_srtp = true;
  rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  rtc_config.turn_port_prune_policy =
      webrtc::PortPrunePolicy::PRUNE_BASED_ON_PRIORITY;
  std::unique_ptr<PeerConnectionObserver> observer(
      new PeerConnectionObserver(sender, receiver_, &data_manager_dispatcher_));
  webrtc::PeerConnectionDependencies dependencies(observer.get());

  // WebRTC の SSL 接続の検証は自前のルート証明書(rtc_base/ssl_roots.h)でやっていて、
  // その中に Let's Encrypt の証明書が無いため、接続先によっては接続できないことがある。
  //
  // それを解消するために tls_cert_verifier を設定して自前で検証を行う。
  dependencies.tls_cert_verifier = std::unique_ptr<rtc::SSLCertificateVerifier>(
      new RTCSSLVerifier(config_.insecure));

  webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
      connection = factory_->CreatePeerConnectionOrError(
          rtc_config, std::move(dependencies));
  if (!connection.ok()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << ": CreatePeerConnection failed";
    return nullptr;
  }

  return std::make_shared<RTCConnection>(sender, std::move(observer),
                                         connection.value());
}

void RTCManager::InitTracks(RTCConnection* conn) {
  auto connection = conn->GetConnection();

  std::string stream_id = Util::GenerateRandomChars();

  if (audio_track_) {
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        audio_sender = connection->AddTrack(audio_track_, {stream_id});
    if (!audio_sender.ok()) {
      RTC_LOG(LS_WARNING) << __FUNCTION__ << ": Cannot add audio_track_";
    }
  }

  if (video_track_) {
    webrtc::RTCErrorOr<rtc::scoped_refptr<webrtc::RtpSenderInterface>>
        video_add_result = connection->AddTrack(video_track_, {stream_id});
    if (video_add_result.ok()) {
      rtc::scoped_refptr<webrtc::RtpSenderInterface> video_sender =
          video_add_result.value();
      webrtc::RtpParameters parameters = video_sender->GetParameters();
      parameters.degradation_preference = config_.GetPriority();
      video_sender->SetParameters(parameters);
    } else {
      RTC_LOG(LS_WARNING) << __FUNCTION__ << ": Cannot add video_track_";
    }
  }
}
