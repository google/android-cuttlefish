/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "host/commands/secure_env/encrypted_serializable.h"

#include <gtest/gtest.h>
#include <keymaster/serializable.h>
#include <string.h>

#include "host/commands/secure_env/primary_key_builder.h"
#include "host/commands/secure_env/test_tpm.h"
#include "host/commands/secure_env/tpm_resource_manager.h"

namespace cuttlefish {

TEST(TpmEncryptedSerializable, BinaryData) {
  TestTpm tpm;
  TpmResourceManager resource_manager(tpm.Esys());

  uint8_t input_data[] = {1, 2, 3, 4, 5};
  keymaster::Buffer input(input_data, sizeof(input_data));
  EncryptedSerializable encrypt_input(resource_manager,
                                      ParentKeyCreator("test"), input);

  std::vector<uint8_t> encrypted_data(encrypt_input.SerializedSize());
  auto encrypt_return = encrypt_input.Serialize(
      encrypted_data.data(), encrypted_data.data() + encrypted_data.size());

  keymaster::Buffer output(sizeof(input_data));
  EncryptedSerializable decrypt_intermediate(resource_manager,
                                             ParentKeyCreator("test"), output);
  const uint8_t* encrypted_data_ptr = encrypted_data.data();
  auto decrypt_return = decrypt_intermediate.Deserialize(
      &encrypted_data_ptr, encrypted_data_ptr + encrypted_data.size());

  ASSERT_EQ(encrypt_return, encrypted_data.data() + encrypted_data.size());
  ASSERT_TRUE(decrypt_return);
  ASSERT_EQ(encrypted_data_ptr, encrypted_data.data() + encrypted_data.size());
  ASSERT_EQ(0, memcmp(input_data, output.begin(), sizeof(input_data)));
}

}  // namespace cuttlefish
