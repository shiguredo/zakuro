#include "sctp_transport_factory.h"

#include <media/sctp/dcsctp_transport.h>
#include <media/sctp/usrsctp_transport.h>
#include <rtc_base/system/unused.h>
#include <system_wrappers/include/clock.h>
#include <system_wrappers/include/field_trial.h>

SctpTransportFactory::SctpTransportFactory(rtc::Thread* network_thread)
    : network_thread_(network_thread) {}

std::unique_ptr<cricket::SctpTransportInternal>
SctpTransportFactory::CreateSctpTransport(
    rtc::PacketTransportInternal* transport) {
  return std::unique_ptr<cricket::SctpTransportInternal>(
      new webrtc::DcSctpTransport(network_thread_, transport,
                                  webrtc::Clock::GetRealTimeClock()));
}