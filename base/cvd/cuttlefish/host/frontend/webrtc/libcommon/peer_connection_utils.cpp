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

#include "cuttlefish/host/frontend/webrtc/libcommon/peer_connection_utils.h"

#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/create_peerconnection_factory.h>
#include <api/peer_connection_interface.h>

#include "cuttlefish/host/frontend/webrtc/libcommon/audio_device.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/composite_decoder_factory.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/composite_encoder_factory.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/encoder_provider_registry.h"
#include "cuttlefish/host/frontend/webrtc/libcommon/decoder_provider_registry.h"
#include "rtc_base/logging.h"

namespace cuttlefish {
namespace webrtc_streaming {

Result<std::unique_ptr<rtc::Thread>> CreateAndStartThread(
    const std::string& name) {
  auto thread = rtc::Thread::CreateWithSocketServer();
  CF_EXPECTF(thread.get(), "Failed to create \"{}\" thread", name);
  thread->SetName(name, nullptr);
  CF_EXPECTF(thread->Start(), "Failed to start \"{}\" thread", name);
  return thread;
}

Result<rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface>>
CreatePeerConnectionFactory(
    rtc::Thread* network_thread, rtc::Thread* worker_thread,
    rtc::Thread* signal_thread,
    rtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device_module) {

  RTC_LOG(LS_INFO) << "=== Creating PeerConnectionFactory with plugin framework ===";

  // Log available encoder providers
  RTC_LOG(LS_INFO) << "Available encoder providers:";
  for (auto* provider : EncoderProviderRegistry::Get().GetAvailableProviders()) {
    RTC_LOG(LS_INFO) << "  - " << provider->GetName()
                     << " (priority=" << provider->GetPriority() << ")";
    for (const auto& fmt : provider->GetSupportedFormats()) {
      RTC_LOG(LS_INFO) << "      " << fmt.ToString();
    }
  }

  // Log available decoder providers
  RTC_LOG(LS_INFO) << "Available decoder providers:";
  for (auto* provider : DecoderProviderRegistry::Get().GetAvailableProviders()) {
    RTC_LOG(LS_INFO) << "  - " << provider->GetName()
                     << " (priority=" << provider->GetPriority() << ")";
    for (const auto& fmt : provider->GetSupportedFormats()) {
      RTC_LOG(LS_INFO) << "      " << fmt.ToString();
    }
  }

  // Create composite factories that use all registered providers
  auto video_encoder_factory = CreateCompositeEncoderFactory();
  auto video_decoder_factory = CreateCompositeDecoderFactory();

  // Log final supported formats
  auto encoder_formats = video_encoder_factory->GetSupportedFormats();
  RTC_LOG(LS_INFO) << "Composite encoder factory supports " << encoder_formats.size() << " format(s)";
  for (const auto& fmt : encoder_formats) {
    RTC_LOG(LS_INFO) << "  " << fmt.ToString();
  }

  auto decoder_formats = video_decoder_factory->GetSupportedFormats();
  RTC_LOG(LS_INFO) << "Composite decoder factory supports " << decoder_formats.size() << " format(s)";
  for (const auto& fmt : decoder_formats) {
    RTC_LOG(LS_INFO) << "  " << fmt.ToString();
  }

  auto peer_connection_factory = webrtc::CreatePeerConnectionFactory(
      network_thread, worker_thread, signal_thread, audio_device_module,
      webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      std::move(video_encoder_factory),
      std::move(video_decoder_factory), nullptr /* audio_mixer */,
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
