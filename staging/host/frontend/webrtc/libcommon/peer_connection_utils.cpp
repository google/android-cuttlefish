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

#include "host/frontend/webrtc/libcommon/peer_connection_utils.h"

#include <api/audio_codecs/audio_decoder_factory.h>
#include <api/audio_codecs/audio_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/video_codecs/video_decoder_factory.h>
#include <api/video_codecs/video_encoder_factory.h>

#include "host/frontend/webrtc/libcommon/audio_device.h"
#include "host/frontend/webrtc/libcommon/vp8only_encoder_factory.h"

namespace cuttlefish {
namespace webrtc_streaming {

Result<std::unique_ptr<rtc::Thread>> CreateAndStartThread(
    const std::string& name) {
  auto thread = rtc::Thread::CreateWithSocketServer();
  CF_EXPECT(thread.get(), "Failed to create " << name << " thread");
  thread->SetName(name, nullptr);
  CF_EXPECT(thread->Start(), "Failed to start " << name << " thread");
  return thread;
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>>
CreatePeerConnectionFactory(
    rtc::Thread* network_thread, rtc::Thread* worker_thread,
    rtc::Thread* signal_thread,
    rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module) {
  auto peer_connection_factory = webrtc::CreatePeerConnectionFactory(
      network_thread, worker_thread, signal_thread, audio_device_module,
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      // Only VP8 is supported
      std::make_unique<VP8OnlyEncoderFactory>(
          webrtc::CreateBuiltinVideoEncoderFactory()),
      webrtc::CreateBuiltinVideoDecoderFactory(), nullptr /* audio_mixer */,
      nullptr /* audio_processing */);
  CF_EXPECT(peer_connection_factory.get(),
            "Failed to create peer connection factory");

  webrtc::PeerConnectionFactoryInterface::Options options;
  // By default the loopback network is ignored, but generating candidates for
  // it is useful when using TCP port forwarding.
  options.network_ignore_mask = 0;
  peer_connection_factory->SetOptions(options);

  return peer_connection_factory;
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionInterface>>
CreatePeerConnection(
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>
        peer_connection_factory,
    webrtc::PeerConnectionDependencies dependencies,
    uint16_t min_port, uint16_t max_port,
    const std::vector<webrtc::PeerConnectionInterface::IceServer>& servers) {
  webrtc::PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  config.servers.insert(config.servers.end(), servers.begin(), servers.end());
  config.set_min_port(min_port);
  config.set_max_port(max_port);
  auto result = peer_connection_factory->CreatePeerConnectionOrError(
      config, std::move(dependencies));

  CF_EXPECT(result.ok(),
            "Failed to create peer connection: " << result.error().message());
  return result.MoveValue();
}

}  // namespace webrtc_streaming
}  // namespace cuttlefish
