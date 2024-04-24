#ifndef ENABLE_MEDIA_WITH_FAKE_CALL_H_
#define ENABLE_MEDIA_WITH_FAKE_CALL_H_

#include <api/peer_connection_interface.h>
#include <call/degraded_call.h>

void EnableMediaWithFakeCall(
    webrtc::PeerConnectionFactoryDependencies& deps,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& send_config,
    const webrtc::DegradedCall::TimeScopedNetworkConfig& receive_config);

#endif