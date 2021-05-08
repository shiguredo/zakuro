#ifndef RTC_MANAGER_H_
#define RTC_MANAGER_H_

// WebRTC
#include <api/peer_connection_interface.h>
#include <pc/video_track_source.h>

#include "rtc_connection.h"
#include "rtc_data_manager_dispatcher.h"
#include "rtc_message_sender.h"
#include "scalable_track_source.h"
#include "video_track_receiver.h"
#include "zakuro_audio_device_module.h"

struct RTCManagerConfig {
  bool insecure = false;

  bool no_video_device = false;

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

  bool fixed_resolution = false;
  bool show_me = false;
  bool simulcast = false;

  bool disable_echo_cancellation = false;
  bool disable_auto_gain_control = false;
  bool disable_noise_suppression = false;
  bool disable_highpass_filter = false;
  bool disable_typing_detection = false;
  bool disable_residual_echo_detector = false;

  std::string priority = "BALANCE";

  // FRAMERATE が優先のときは RESOLUTION をデグレさせていく
  webrtc::DegradationPreference GetPriority() {
    if (priority == "FRAMERATE") {
      return webrtc::DegradationPreference::MAINTAIN_RESOLUTION;
    } else if (priority == "RESOLUTION") {
      return webrtc::DegradationPreference::MAINTAIN_FRAMERATE;
    }
    return webrtc::DegradationPreference::BALANCED;
  }

  std::string openh264;
};

class RTCManager {
 public:
  RTCManager(RTCManagerConfig config,
             rtc::scoped_refptr<ScalableVideoTrackSource> video_track_source,
             VideoTrackReceiver* receiver);
  ~RTCManager();
  void AddDataManager(std::shared_ptr<RTCDataManager> data_manager);
  std::shared_ptr<RTCConnection> CreateConnection(
      webrtc::PeerConnectionInterface::RTCConfiguration rtc_config,
      RTCMessageSender* sender);
  void InitTracks(RTCConnection* conn);

 private:
  rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::unique_ptr<rtc::Thread> network_thread_;
  std::unique_ptr<rtc::Thread> worker_thread_;
  std::unique_ptr<rtc::Thread> signaling_thread_;
  RTCManagerConfig config_;
  VideoTrackReceiver* receiver_;
  RTCDataManagerDispatcher data_manager_dispatcher_;
};

#endif
