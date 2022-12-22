/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// TODO review includes
#include <api/peer_connection_interface.h>

#include "common/libs/utils/result.h"

namespace cuttlefish {
namespace webrtc_streaming {

Result<std::unique_ptr<rtc::Thread>> CreateAndStartThread(
    const std::string& name);

Result<rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>>
CreatePeerConnectionFactory(
    rtc::Thread* network_thread, rtc::Thread* worker_thread,
    rtc::Thread* signal_thread,
    rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module);

// TODO(b/263528313): Use a packet socket factory instead of a port range.
Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
CreatePeerConnection(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
        peer_connection_factory,
    webrtc::PeerConnectionDependencies dependencies,
    uint16_t min_port, uint16_t max_port,
    const std::vector<webrtc::PeerConnectionInterface::IceServer>&
        per_connection_servers);

}  // namespace webrtc_streaming
}  // namespace cuttlefish

