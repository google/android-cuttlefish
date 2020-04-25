/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <webrtc/RTPSession.h>
#include <webrtc/RTPSocketHandler.h>
#include <webrtc/SDP.h>
#include <webrtc/ServerState.h>

#include <https/RunLoop.h>
#include <https/WebSocketHandler.h>
#include <source/KeyboardSink.h>
#include <source/TouchSink.h>

#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

struct ClientHandler : public std::enable_shared_from_this<ClientHandler> {
  explicit ClientHandler(std::shared_ptr<ServerState> serverState,
                         std::function<void(const Json::Value&)> send_client_cb);

  void HandleMessage(const Json::Value& client_message);

  void OnConnectionTimeOut(std::function<void()> cb) {
    on_connection_timeout_cb_ = cb;
  }

 private:
  enum OptionBits : uint32_t {
    disableAudio = 1,
    bundleTracks = 2,
    enableData = 4,
    useSingleCertificateForAllTracks = 8,
    useTCP = 16,
  };

  using TouchSink = android::TouchSink;
  using KeyboardSink = android::KeyboardSink;

  std::shared_ptr<RunLoop> mRunLoop;
  std::shared_ptr<ServerState> mServerState;
  uint32_t mOptions;
  std::function<void(const Json::Value&)> sendToClient_;

  // Vector has the same ordering as the media entries in the SDP, i.e.
  // vector index is "mlineIndex". (unless we are bundling, in which case
  // there is only a single session).
  std::vector<std::shared_ptr<RTPSession>> mSessions;

  SDP mOfferedSDP;
  std::vector<std::shared_ptr<RTPSocketHandler>> mRTPs;

  std::shared_ptr<TouchSink> mTouchSink;
  std::shared_ptr<KeyboardSink> mKeyboardSink;

  std::pair<std::shared_ptr<X509>, std::shared_ptr<EVP_PKEY>>
      mCertificateAndKey;

  std::function<void()> on_connection_timeout_cb_ = []{};

  void LogAndReplyError(const std::string& error_msg) const ;

  std::string BuildOffer();

  // Pass -1 for mlineIndex to access the "general" section.
  std::optional<std::string> getSDPValue(
      ssize_t mlineIndex, std::string_view key,
      bool fallthroughToGeneralSection) const;

  std::string getRemotePassword(size_t mlineIndex) const;
  std::string getRemoteUFrag(size_t mlineIndex) const;
  std::string getRemoteFingerprint(size_t mlineIndex) const;

  bool GatherAndSendCandidate(int32_t mid);

  static std::pair<std::shared_ptr<X509>, std::shared_ptr<EVP_PKEY>>
  CreateDTLSCertificateAndKey();

  std::pair<std::string, std::string> createUniqueUFragAndPassword();

  void parseOptions(const Json::Value &options);
  size_t countTracks() const;

  void prepareSessions();

  void emitTrackIceOptionsAndFingerprint(std::stringstream &ss,
                                         size_t mlineIndex) const;

  // Returns -1 on error.
  ssize_t mlineIndexForMid(int32_t mid) const;

  static void CreateRandomIceCharSequence(char *dst, size_t size);
};
