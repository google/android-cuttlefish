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

#include "commands.h"

#include <linux/vtpm_proxy.h>
#include <tss2/tss2_tpm2_types.h>

#include <cstddef>
#include <string>

std::string TpmCommandName(std::uint32_t command_num) {
  switch(command_num) {
    #define MATCH_TPM_COMMAND(name) case name: return #name;
    MATCH_TPM_COMMAND(TPM2_CC_NV_UndefineSpaceSpecial)
    MATCH_TPM_COMMAND(TPM2_CC_EvictControl)
    MATCH_TPM_COMMAND(TPM2_CC_HierarchyControl)
    MATCH_TPM_COMMAND(TPM2_CC_NV_UndefineSpace)
    MATCH_TPM_COMMAND(TPM2_CC_ChangeEPS)
    MATCH_TPM_COMMAND(TPM2_CC_ChangePPS)
    MATCH_TPM_COMMAND(TPM2_CC_Clear)
    MATCH_TPM_COMMAND(TPM2_CC_ClearControl)
    MATCH_TPM_COMMAND(TPM2_CC_ClockSet)
    MATCH_TPM_COMMAND(TPM2_CC_HierarchyChangeAuth)
    MATCH_TPM_COMMAND(TPM2_CC_NV_DefineSpace)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_Allocate)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_SetAuthPolicy)
    MATCH_TPM_COMMAND(TPM2_CC_PP_Commands)
    MATCH_TPM_COMMAND(TPM2_CC_SetPrimaryPolicy)
    MATCH_TPM_COMMAND(TPM2_CC_FieldUpgradeStart)
    MATCH_TPM_COMMAND(TPM2_CC_ClockRateAdjust)
    MATCH_TPM_COMMAND(TPM2_CC_CreatePrimary)
    MATCH_TPM_COMMAND(TPM2_CC_NV_GlobalWriteLock)
    MATCH_TPM_COMMAND(TPM2_CC_GetCommandAuditDigest)
    MATCH_TPM_COMMAND(TPM2_CC_NV_Increment)
    MATCH_TPM_COMMAND(TPM2_CC_NV_SetBits)
    MATCH_TPM_COMMAND(TPM2_CC_NV_Extend)
    MATCH_TPM_COMMAND(TPM2_CC_NV_Write)
    MATCH_TPM_COMMAND(TPM2_CC_NV_WriteLock)
    MATCH_TPM_COMMAND(TPM2_CC_DictionaryAttackLockReset)
    MATCH_TPM_COMMAND(TPM2_CC_DictionaryAttackParameters)
    MATCH_TPM_COMMAND(TPM2_CC_NV_ChangeAuth)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_Event)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_Reset)
    MATCH_TPM_COMMAND(TPM2_CC_SequenceComplete)
    MATCH_TPM_COMMAND(TPM2_CC_SetAlgorithmSet)
    MATCH_TPM_COMMAND(TPM2_CC_SetCommandCodeAuditStatus)
    MATCH_TPM_COMMAND(TPM2_CC_FieldUpgradeData)
    MATCH_TPM_COMMAND(TPM2_CC_IncrementalSelfTest)
    MATCH_TPM_COMMAND(TPM2_CC_SelfTest)
    MATCH_TPM_COMMAND(TPM2_CC_Startup)
    MATCH_TPM_COMMAND(TPM2_CC_Shutdown)
    MATCH_TPM_COMMAND(TPM2_CC_StirRandom)
    MATCH_TPM_COMMAND(TPM2_CC_ActivateCredential)
    MATCH_TPM_COMMAND(TPM2_CC_Certify)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyNV)
    MATCH_TPM_COMMAND(TPM2_CC_CertifyCreation)
    MATCH_TPM_COMMAND(TPM2_CC_Duplicate)
    MATCH_TPM_COMMAND(TPM2_CC_GetTime)
    MATCH_TPM_COMMAND(TPM2_CC_GetSessionAuditDigest)
    MATCH_TPM_COMMAND(TPM2_CC_NV_Read)
    MATCH_TPM_COMMAND(TPM2_CC_NV_ReadLock)
    MATCH_TPM_COMMAND(TPM2_CC_ObjectChangeAuth)
    MATCH_TPM_COMMAND(TPM2_CC_PolicySecret)
    MATCH_TPM_COMMAND(TPM2_CC_Rewrap)
    MATCH_TPM_COMMAND(TPM2_CC_Create)
    MATCH_TPM_COMMAND(TPM2_CC_ECDH_ZGen)
    MATCH_TPM_COMMAND(TPM2_CC_HMAC)
    MATCH_TPM_COMMAND(TPM2_CC_Import)
    MATCH_TPM_COMMAND(TPM2_CC_Load)
    MATCH_TPM_COMMAND(TPM2_CC_Quote)
    MATCH_TPM_COMMAND(TPM2_CC_RSA_Decrypt)
    MATCH_TPM_COMMAND(TPM2_CC_HMAC_Start)
    MATCH_TPM_COMMAND(TPM2_CC_SequenceUpdate)
    MATCH_TPM_COMMAND(TPM2_CC_Sign)
    MATCH_TPM_COMMAND(TPM2_CC_Unseal)
    MATCH_TPM_COMMAND(TPM2_CC_PolicySigned)
    MATCH_TPM_COMMAND(TPM2_CC_ContextLoad)
    MATCH_TPM_COMMAND(TPM2_CC_ContextSave)
    MATCH_TPM_COMMAND(TPM2_CC_ECDH_KeyGen)
    MATCH_TPM_COMMAND(TPM2_CC_EncryptDecrypt)
    MATCH_TPM_COMMAND(TPM2_CC_FlushContext)
    MATCH_TPM_COMMAND(TPM2_CC_LoadExternal)
    MATCH_TPM_COMMAND(TPM2_CC_MakeCredential)
    MATCH_TPM_COMMAND(TPM2_CC_NV_ReadPublic)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyAuthorize)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyAuthValue)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyCommandCode)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyCounterTimer)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyCpHash)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyLocality)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyNameHash)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyOR)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyTicket)
    MATCH_TPM_COMMAND(TPM2_CC_ReadPublic)
    MATCH_TPM_COMMAND(TPM2_CC_RSA_Encrypt)
    MATCH_TPM_COMMAND(TPM2_CC_StartAuthSession)
    MATCH_TPM_COMMAND(TPM2_CC_VerifySignature)
    MATCH_TPM_COMMAND(TPM2_CC_ECC_Parameters)
    MATCH_TPM_COMMAND(TPM2_CC_FirmwareRead)
    MATCH_TPM_COMMAND(TPM2_CC_GetCapability)
    MATCH_TPM_COMMAND(TPM2_CC_GetRandom)
    MATCH_TPM_COMMAND(TPM2_CC_GetTestResult)
    MATCH_TPM_COMMAND(TPM2_CC_Hash)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_Read)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyPCR)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyRestart)
    MATCH_TPM_COMMAND(TPM2_CC_ReadClock)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_Extend)
    MATCH_TPM_COMMAND(TPM2_CC_PCR_SetAuthValue)
    MATCH_TPM_COMMAND(TPM2_CC_NV_Certify)
    MATCH_TPM_COMMAND(TPM2_CC_EventSequenceComplete)
    MATCH_TPM_COMMAND(TPM2_CC_HashSequenceStart)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyPhysicalPresence)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyDuplicationSelect)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyGetDigest)
    MATCH_TPM_COMMAND(TPM2_CC_TestParms)
    MATCH_TPM_COMMAND(TPM2_CC_Commit)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyPassword)
    MATCH_TPM_COMMAND(TPM2_CC_ZGen_2Phase)
    MATCH_TPM_COMMAND(TPM2_CC_EC_Ephemeral)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyNvWritten)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyTemplate)
    MATCH_TPM_COMMAND(TPM2_CC_CreateLoaded)
    MATCH_TPM_COMMAND(TPM2_CC_PolicyAuthorizeNV)
    MATCH_TPM_COMMAND(TPM2_CC_EncryptDecrypt2)
    MATCH_TPM_COMMAND(TPM2_CC_AC_GetCapability)
    MATCH_TPM_COMMAND(TPM2_CC_AC_Send)
    MATCH_TPM_COMMAND(TPM2_CC_Policy_AC_SendSelect)
    MATCH_TPM_COMMAND(TPM2_CC_Vendor_TCG_Test)
    MATCH_TPM_COMMAND(TPM2_CC_SET_LOCALITY)
    #undef MATCH_TPM_COMMAND
    default:
      return "Unknown";
  }
}
