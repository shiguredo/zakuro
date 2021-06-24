#include "sctp_transport_factory.h"

#include <media/sctp/dcsctp_transport.h>
#include <media/sctp/usrsctp_transport.h>
#include <rtc_base/system/unused.h>
#include <system_wrappers/include/clock.h>
#include <system_wrappers/include/field_trial.h>

SctpTransportFactory::SctpTransportFactory(rtc::Thread* network_thread,
                                           bool use_dcsctp)
    : network_thread_(network_thread), use_dcsctp_(use_dcsctp) {}

std::unique_ptr<cricket::SctpTransportInternal>
SctpTransportFactory::CreateSctpTransport(
    rtc::PacketTransportInternal* transport) {
  std::unique_ptr<cricket::SctpTransportInternal> result;
  if (use_dcsctp_) {
    result = std::unique_ptr<cricket::SctpTransportInternal>(
        new webrtc::DcSctpTransport(network_thread_, transport,
                                    webrtc::Clock::GetRealTimeClock()));
  }
  if (!result) {
    result = std::unique_ptr<cricket::SctpTransportInternal>(
        new cricket::UsrsctpTransport(network_thread_, transport));
  }
  return result;
}