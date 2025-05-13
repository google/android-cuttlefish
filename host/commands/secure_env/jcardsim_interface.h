/*
 * Copyright 2024 The Android Open Source Project
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

#include <iostream>
#include <memory>
#include <vector>

#include "common/libs/utils/result.h"

#include <jni.h>

namespace cuttlefish {

// This class helps to interact with JCardSimulator
class JCardSimInterface {
  // Private Constructor
  JCardSimInterface(JavaVM* jvm);

 public:
  ~JCardSimInterface();

  // This function Loads and initializes a Java VM. Installs and
  // personalizes the required applets.
  static Result<std::unique_ptr<JCardSimInterface>> Create();

  // This function transmits the data to JCardSimulator and
  // returns the response from simulator back to the caller.
  Result<std::vector<uint8_t>> Transmit(const uint8_t* data, size_t len) const;

 private:
  Result<void> PersonalizeKeymintApplet(JNIEnv* env);
  Result<void> ProvisionPresharedSecret(JNIEnv* env);
  Result<std::vector<uint8_t>> OpenChannel(JNIEnv* env);
  Result<std::vector<uint8_t>> SelectKeymintApplet(JNIEnv* env, uint8_t cla);
  Result<std::vector<uint8_t>> CloseChannel(JNIEnv* env,
                                            uint8_t channel_number);
  Result<std::vector<uint8_t>> PreSharedKey();
  Result<std::vector<uint8_t>> InternalTransmit(JNIEnv* env,
                                                const uint8_t* data,
                                                size_t len) const;

  jobject jcardsim_proxy_inst_;
  jclass jcardsim_proxy_class_;
  JavaVM* jvm_;
};

}  // namespace cuttlefish
