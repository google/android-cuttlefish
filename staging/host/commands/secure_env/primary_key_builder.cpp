//
// Copyright (C) 2020 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "host/commands/secure_env/primary_key_builder.h"

#include <android-base/logging.h>
#include <tss2/tss2_mu.h>
#include <tss2/tss2_rc.h>

namespace cuttlefish {

PrimaryKeyBuilder::PrimaryKeyBuilder() : public_area_({}) {
  public_area_.nameAlg = TPM2_ALG_SHA256;
};

void PrimaryKeyBuilder::SigningKey() {
  public_area_.type = TPM2_ALG_KEYEDHASH;
  public_area_.objectAttributes |= TPMA_OBJECT_SIGN_ENCRYPT;
  public_area_.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
  public_area_.objectAttributes |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
  public_area_.parameters.keyedHashDetail.scheme = (TPMT_KEYEDHASH_SCHEME) {
    .scheme = TPM2_ALG_HMAC,
    .details.hmac.hashAlg = TPM2_ALG_SHA256,
  };
}

void PrimaryKeyBuilder::ParentKey() {
  public_area_.type = TPM2_ALG_SYMCIPHER;
  public_area_.objectAttributes |= TPMA_OBJECT_USERWITHAUTH;
  public_area_.objectAttributes |= TPMA_OBJECT_RESTRICTED;
  public_area_.objectAttributes |= TPMA_OBJECT_DECRYPT;
  public_area_.objectAttributes |= TPMA_OBJECT_FIXEDTPM;
  public_area_.objectAttributes |= TPMA_OBJECT_FIXEDPARENT;
  public_area_.objectAttributes |= TPMA_OBJECT_SENSITIVEDATAORIGIN;
  public_area_.parameters.symDetail.sym = (TPMT_SYM_DEF_OBJECT) {
    .algorithm = TPM2_ALG_AES,
    .keyBits.aes = 128, // The default maximum AES key size in the simulator.
    .mode.aes = TPM2_ALG_CFB,
  };
}

void PrimaryKeyBuilder::UniqueData(const std::string& data) {
  if (data.size() > TPM2_SHA256_DIGEST_SIZE) {
    LOG(FATAL) << "Unique data size was too large";
  }
  /* The unique field normally has a precise size to go with the type of the
   * object. During primary key creation the unique field accepts any short byte
   * string to let the user introduce variability into the primary key creation
   * process which is otherwise determinstic relative to secret TPM state. */
  public_area_.unique.sym.size = data.size();
  memcpy(&public_area_.unique.sym.buffer, data.data(), data.size());
}

TpmObjectSlot PrimaryKeyBuilder::CreateKey(
    TpmResourceManager& resource_manager) {
  TPM2B_AUTH authValue = {};
  auto rc =
      Esys_TR_SetAuth(resource_manager.Esys(), ESYS_TR_RH_OWNER, &authValue);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_TR_SetAuth failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }

  TPM2B_TEMPLATE public_template = {};
  size_t offset = 0;
  rc = Tss2_MU_TPMT_PUBLIC_Marshal(&public_area_, &public_template.buffer[0],
                                   sizeof(public_template.buffer), &offset);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Tss2_MU_TPMT_PUBLIC_Marshal failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  public_template.size = offset;

  TPM2B_SENSITIVE_CREATE in_sensitive = {};

  auto key_slot = resource_manager.ReserveSlot();
  if (!key_slot) {
    LOG(ERROR) << "No slots available";
    return {};
  }
  ESYS_TR raw_handle;
  // TODO(b/154956668): Define better ACLs on these keys.
  // Since this is a primary key, it's generated deterministically. It would
  // also be possible to generate this once and hold it in storage.
  rc = Esys_CreateLoaded(
    /* esysContext */ resource_manager.Esys(),
    /* primaryHandle */ ESYS_TR_RH_OWNER,
    /* shandle1 */ ESYS_TR_PASSWORD,
    /* shandle2 */ ESYS_TR_NONE,
    /* shandle3 */ ESYS_TR_NONE,
    /* inSensitive */ &in_sensitive,
    /* inPublic */ &public_template,
    /* objectHandle */ &raw_handle,
    /* outPrivate */ nullptr,
    /* outPublic */ nullptr);
  if (rc != TSS2_RC_SUCCESS) {
    LOG(ERROR) << "Esys_CreateLoaded failed with return code " << rc
               << " (" << Tss2_RC_Decode(rc) << ")";
    return {};
  }
  key_slot->set(raw_handle);
  return key_slot;
}

std::function<TpmObjectSlot(TpmResourceManager&)>
SigningKeyCreator(const std::string& unique) {
  return [unique](TpmResourceManager& resource_manager) {
    PrimaryKeyBuilder key_builder;
    key_builder.SigningKey();
    key_builder.UniqueData(unique);
    return key_builder.CreateKey(resource_manager);
  };
}

std::function<TpmObjectSlot(TpmResourceManager&)>
ParentKeyCreator(const std::string& unique) {
  return [unique](TpmResourceManager& resource_manager) {
    PrimaryKeyBuilder key_builder;
    key_builder.ParentKey();
    key_builder.UniqueData(unique);
    return key_builder.CreateKey(resource_manager);
  };
}

TpmObjectSlot PrimaryKeyBuilder::CreateSigningKey(
    TpmResourceManager& resource_manager, const std::string& unique_data) {
  return SigningKeyCreator(unique_data)(resource_manager);
}

}  // namespace cuttlefish
