#include <atomic>
#include <condition_variable>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// Linux
#include <sys/resource.h>

// WebRTC
#include <rtc_base/log_sinks.h>
#include <rtc_base/string_utils.h>

#if defined(__APPLE__)
#include "mac_helper/mac_capturer.h"
#else
#include "v4l2_video_capturer/v4l2_video_capturer.h"
#endif

#include <blend2d.h>

#include "fake_video_capturer.h"
#include "game/game_kuzushi.h"
#include "sora/sora_server.h"
#include "util.h"
#include "virtual_client.h"
#include "wav_reader.h"
#include "zakuro_args.h"

const size_t kDefaultMaxLogFileSize = 10 * 1024 * 1024;

int main(int argc, char* argv[]) {
  rlimit lim;
  if (::getrlimit(RLIMIT_NOFILE, &lim) != 0) {
    std::cerr << "getrlimit 失敗" << std::endl;
    return -1;
  }
  if (lim.rlim_cur < 1024) {
    std::cerr << "ファイルディスクリプタの数が足りません。"
                 "最低でも 1024 以上にして下さい。"
              << std::endl;
    std::cerr << "  soft=" << lim.rlim_cur << ", hard=" << lim.rlim_max
              << std::endl;
    return -1;
  }
  ZakuroArgs args;

  int log_level = rtc::LS_NONE;

  Util::ParseArgs(argc, argv, log_level, args);

  rtc::LogMessage::LogToDebug((rtc::LoggingSeverity)log_level);
  rtc::LogMessage::LogTimestamps();
  rtc::LogMessage::LogThreads();

  std::unique_ptr<rtc::FileRotatingLogSink> log_sink(
      new rtc::FileRotatingLogSink("./", "webrtc_logs", kDefaultMaxLogFileSize,
                                   10));
  if (!log_sink->Init()) {
    RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to open log file";
    log_sink.reset();
    return 1;
  }
  rtc::LogMessage::AddLogToStream(log_sink.get(), rtc::LS_INFO);

  std::unique_ptr<GameAudioManager> gam;
  std::unique_ptr<GameKuzushi> kuzushi;
  if (args.game == "kuzushi") {
    auto size = args.GetSize();
    gam.reset(new GameAudioManager());
    kuzushi.reset(new GameKuzushi(size.width, size.height, gam.get()));
  }

  auto capturer = ([&]() -> rtc::scoped_refptr<ScalableVideoTrackSource> {
    if (args.no_video_device) {
      return nullptr;
    }

    auto size = args.GetSize();
    if (args.video_device.empty()) {
      FakeVideoCapturerConfig config;
      config.width = size.width;
      config.height = size.height;
      config.fps = args.framerate;
      if (!args.game.empty()) {
        config.type = FakeVideoCapturerConfig::Type::External;
        config.render =
            [&kuzushi](BLContext& ctx,
                       std::chrono::high_resolution_clock::time_point now) {
              kuzushi->Render(ctx, now);
            };
      } else if (args.fake_video_capture.empty()) {
        config.type = args.sandstorm ? FakeVideoCapturerConfig::Type::Sandstorm
                                     : FakeVideoCapturerConfig::Type::Safari;
      } else {
        config.type = FakeVideoCapturerConfig::Type::Y4MFile;
        config.y4m_path = args.fake_video_capture;
      }
      return FakeVideoCapturer::Create(std::move(config));
    } else {
#if defined(__APPLE__)
      return MacCapturer::Create(size.width, size.height, args.framerate,
                                 args.video_device);
#else
      V4L2VideoCapturerConfig config;
      config.video_device = args.video_device;
      config.width = size.width;
      config.height = size.height;
      config.framerate = args.framerate;
      return V4L2VideoCapturer::Create(std::move(config));
#endif
    }
  })();

  if (!capturer && !args.no_video_device) {
    std::cerr << "failed to create capturer" << std::endl;
    return 1;
  }

  RTCManagerConfig rtcm_config;
  rtcm_config.insecure = args.insecure;
  rtcm_config.no_video_device = args.no_video_device;
  rtcm_config.fixed_resolution = args.fixed_resolution;
  rtcm_config.simulcast = args.sora_simulcast;
  rtcm_config.priority = args.priority;
  rtcm_config.openh264 = args.openh264;
  if (args.no_audio_device) {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::NoAudio;
  } else if (!args.game.empty()) {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::External;
  } else if (!args.fake_audio_capture.empty()) {
    WavReader wav_reader;
    int r = wav_reader.Load(args.fake_audio_capture);
    if (r != 0) {
      std::cerr << "failed to load fake audio: path=" << args.fake_audio_capture
                << " result=" << r << std::endl;
      return 1;
    }
    rtcm_config.audio_type = RTCManagerConfig::AudioType::SpecifiedFakeAudio;
    rtcm_config.fake_audio.reset(new FakeAudioData());
    rtcm_config.fake_audio->sample_rate = wav_reader.sample_rate;
    rtcm_config.fake_audio->channels = wav_reader.channels;
    rtcm_config.fake_audio->data = std::move(wav_reader.data);
  } else {
    rtcm_config.audio_type = RTCManagerConfig::AudioType::AutoGenerateFakeAudio;
  }
  std::vector<RTCManagerConfig> rtcm_configs;
  for (int i = 0; i < args.vcs; i++) {
    RTCManagerConfig config = rtcm_config;
    if (config.audio_type == RTCManagerConfig::AudioType::External) {
      config.render_audio = gam->AddGameAudio(48000);
      config.sample_rate = 48000;
      config.channels = 1;
    }
    rtcm_configs.push_back(std::move(config));
  }

  std::vector<std::unique_ptr<VirtualClient>> vcs;

  {
    boost::asio::io_context ioc{1};
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work_guard(ioc.get_executor());

    boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](const boost::system::error_code&, int) { ioc.stop(); });

    SoraClientConfig sorac_config;
    sorac_config.insecure = args.insecure;
    sorac_config.signaling_url = args.sora_signaling_url;
    sorac_config.channel_id = args.sora_channel_id;
    sorac_config.video = args.sora_video;
    sorac_config.audio = args.sora_audio;
    sorac_config.video_codec_type = args.sora_video_codec_type;
    sorac_config.audio_codec_type = args.sora_audio_codec_type;
    sorac_config.video_bit_rate = args.sora_video_bit_rate;
    sorac_config.audio_bit_rate = args.sora_audio_bit_rate;
    sorac_config.metadata = args.sora_metadata;
    sorac_config.signaling_notify_metadata =
        args.sora_signaling_notify_metadata;
    sorac_config.role = args.sora_role;
    sorac_config.multistream = args.sora_multistream;
    sorac_config.spotlight = args.sora_spotlight;
    sorac_config.spotlight_number = args.sora_spotlight_number;
    sorac_config.port = args.sora_port;
    sorac_config.simulcast = args.sora_simulcast;

    std::atomic_bool stopped{false};
    std::thread th([&ioc, &stopped, &args, &rtcm_configs, &sorac_config, &vcs,
                    capturer = std::move(capturer)]() {
      for (int i = 0; i < args.vcs; i++) {
        auto next = std::chrono::system_clock::now() +
                    std::chrono::milliseconds((int)(1000 / args.hatch_rate));

        auto vc = std::unique_ptr<VirtualClient>(new VirtualClient(
            ioc, capturer, std::move(rtcm_configs[i]), sorac_config));
        // SoraServer を起動しない場合と、SoraServer を起動して --auto が指定されている場合は即座に接続する。
        // SoraServer を起動するけど --auto が指定されていない場合、SoraServer の API が呼ばれるまで接続しない。
        if (args.sora_port < 0 ||
            args.sora_port >= 0 && args.sora_auto_connect) {
          vc->Connect();
        }
        vcs.push_back(std::move(vc));

        // 次の生成時間になるまで待って、終了フラグが立ってたら終了する。
        // cond var 使うのが面倒なので atomic_bool で定期的にフラグを見る。
        while (next > std::chrono::system_clock::now()) {
          if (stopped) {
            return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (stopped) {
          return;
        }
      }
    });

    if (args.sora_port >= 0) {
      SoraServerConfig config;
      const boost::asio::ip::tcp::endpoint endpoint{
          boost::asio::ip::make_address("127.0.0.1"),
          static_cast<unsigned short>(args.sora_port)};
      // TODO: vcs をスレッドセーフにする（VC 生成スレッドと競合するので）
      SoraServer::Create(ioc, endpoint, &vcs, std::move(config))->Run();
    }

    ioc.run();

    stopped = true;
    th.join();

    for (auto& vc : vcs) {
      vc->Clear();
    }
  }

  vcs.clear();

  return 0;
}
