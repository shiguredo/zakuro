#ifndef SCTP_TRANSPORT_FACTORY_H_
#define SCTP_TRANSPORT_FACTORY_H_

#include <memory>

// WebRTC
#include <api/transport/sctp_transport_factory_interface.h>
#include <media/sctp/sctp_transport_internal.h>
#include <rtc_base/experiments/field_trial_parser.h>
#include <rtc_base/thread.h>

class SctpTransportFactory : public webrtc::SctpTransportFactoryInterface {
 public:
  explicit SctpTransportFactory(rtc::Thread* network_thread, bool use_dcsctp);

  std::unique_ptr<cricket::SctpTransportInternal> CreateSctpTransport(
      rtc::PacketTransportInternal* transport) override;

 private:
  rtc::Thread* network_thread_;
  bool use_dcsctp_;
};

#endif