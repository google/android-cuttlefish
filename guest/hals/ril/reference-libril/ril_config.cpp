/*
**
** Copyright 2020, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "RILC"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/hardware/radio/config/1.1/IRadioConfig.h>
#include <android/hardware/radio/config/1.2/IRadioConfigIndication.h>
#include <android/hardware/radio/config/1.2/IRadioConfigResponse.h>
#include <android/hardware/radio/config/1.3/IRadioConfig.h>
#include <android/hardware/radio/config/1.3/IRadioConfigResponse.h>
#include <libradiocompat/RadioConfig.h>

#include <ril.h>
#include <guest/hals/ril/reference-libril/ril_service.h>
#include <hidl/HidlTransportSupport.h>

using namespace android::hardware::radio::V1_0;
using namespace android::hardware::radio::config;
using namespace android::hardware::radio::config::V1_0;
using namespace android::hardware::radio::config::V1_3;
using ::android::hardware::Return;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Void;
using android::CommandInfo;
using android::RequestInfo;
using android::requestToString;
using android::sp;

RIL_RadioFunctions *s_vendorFunctions_config = NULL;
static CommandInfo *s_configCommands;
struct RadioConfigImpl;
sp<RadioConfigImpl> radioConfigService;
volatile int32_t mCounterRadioConfig;

#if defined (ANDROID_MULTI_SIM)
#define RIL_UNSOL_RESPONSE(a, b, c, d) RIL_onUnsolicitedResponse((a), (b), (c), (d))
#define CALL_ONREQUEST(a, b, c, d, e) s_vendorFunctions_config->onRequest((a), (b), (c), (d), (e))
#define CALL_ONSTATEREQUEST(a) s_vendorFunctions_config->onStateRequest(a)
#else
#define RIL_UNSOL_RESPONSE(a, b, c, d) RIL_onUnsolicitedResponse((a), (b), (c))
#define CALL_ONREQUEST(a, b, c, d, e) s_vendorFunctions_config->onRequest((a), (b), (c), (d))
#define CALL_ONSTATEREQUEST(a) s_vendorFunctions_config->onStateRequest()
#endif

extern void populateResponseInfo(RadioResponseInfo& responseInfo, int serial, int responseType,
                RIL_Errno e);

extern void populateResponseInfo_1_6(
    ::android::hardware::radio::V1_6::RadioResponseInfo &responseInfo,
    int serial, int responseType, RIL_Errno e);

extern bool dispatchVoid(int serial, int slotId, int request);
extern bool dispatchString(int serial, int slotId, int request, const char * str);
extern bool dispatchStrings(int serial, int slotId, int request, bool allowEmpty,
                int countStrings, ...);
extern bool dispatchInts(int serial, int slotId, int request, int countInts, ...);
extern hidl_string convertCharPtrToHidlString(const char *ptr);
extern void sendErrorResponse(android::RequestInfo *pRI, RIL_Errno err);
extern RadioIndicationType convertIntToRadioIndicationType(int indicationType);

extern bool isChangeSlotId(int serviceId, int slotId);

struct RadioConfigImpl : public V1_3::IRadioConfig {
    int32_t mSlotId;
    sp<V1_0::IRadioConfigResponse> mRadioConfigResponse;
    sp<V1_0::IRadioConfigIndication> mRadioConfigIndication;
    sp<V1_1::IRadioConfigResponse> mRadioConfigResponseV1_1;
    sp<V1_2::IRadioConfigResponse> mRadioConfigResponseV1_2;
    sp<V1_2::IRadioConfigIndication> mRadioConfigIndicationV1_2;
    sp<V1_3::IRadioConfigResponse> mRadioConfigResponseV1_3;

    Return<void> setResponseFunctions(
            const ::android::sp<V1_0::IRadioConfigResponse>& radioConfigResponse,
            const ::android::sp<V1_0::IRadioConfigIndication>& radioConfigIndication);

    Return<void> getSimSlotsStatus(int32_t serial);

    Return<void> setSimSlotsMapping(int32_t serial, const hidl_vec<uint32_t>& slotMap);

    Return<void> getPhoneCapability(int32_t serial);

    Return<void> setPreferredDataModem(int32_t serial, uint8_t modemId);

    Return<void> setModemsConfig(int32_t serial, const V1_1::ModemsConfig& modemsConfig);

    Return<void> getModemsConfig(int32_t serial);

    Return<void> getHalDeviceCapabilities(int32_t serial);

    void checkReturnStatus_config(Return<void>& ret);
};
Return<void> RadioConfigImpl::setResponseFunctions(
        const ::android::sp<V1_0::IRadioConfigResponse>& radioConfigResponse,
        const ::android::sp<V1_0::IRadioConfigIndication>& radioConfigIndication) {
    pthread_rwlock_t *radioServiceRwlockPtr = radio_1_6::getRadioServiceRwlock(RIL_SOCKET_1);
    int ret = pthread_rwlock_wrlock(radioServiceRwlockPtr);
    CHECK_EQ(ret, 0);

    mRadioConfigResponse = radioConfigResponse;
    mRadioConfigIndication = radioConfigIndication;


    mRadioConfigResponseV1_1 =
        V1_1::IRadioConfigResponse::castFrom(mRadioConfigResponse).withDefault(nullptr);
    if (mRadioConfigResponseV1_1 == nullptr) {
        mRadioConfigResponseV1_1 = nullptr;
    }

    mRadioConfigResponseV1_2 =
        V1_2::IRadioConfigResponse::castFrom(mRadioConfigResponse).withDefault(nullptr);
    mRadioConfigIndicationV1_2 =
        V1_2::IRadioConfigIndication::castFrom(mRadioConfigIndication).withDefault(nullptr);
    if (mRadioConfigResponseV1_2 == nullptr || mRadioConfigIndicationV1_2 == nullptr) {
        mRadioConfigResponseV1_2 = nullptr;
        mRadioConfigIndicationV1_2 = nullptr;
    }

    mRadioConfigResponseV1_3 =
        V1_3::IRadioConfigResponse::castFrom(mRadioConfigResponse).withDefault(nullptr);
    if (mRadioConfigResponseV1_3 == nullptr || mRadioConfigResponseV1_3 == nullptr) {
        mRadioConfigResponseV1_3 = nullptr;
    }

    mCounterRadioConfig++;

    ret = pthread_rwlock_unlock(radioServiceRwlockPtr);
    CHECK_EQ(ret, 0);

    return Void();
}

Return<void> RadioConfigImpl::getSimSlotsStatus(int32_t serial) {
#if VDBG
    RLOGD("getSimSlotsStatus: serial %d", serial);
#endif
    dispatchVoid(serial, mSlotId, RIL_REQUEST_CONFIG_GET_SLOT_STATUS);

    return Void();
}

Return<void> RadioConfigImpl::setSimSlotsMapping(int32_t serial, const hidl_vec<uint32_t>& slotMap) {
#if VDBG
    RLOGD("setSimSlotsMapping: serial %d", serial);
#endif
    RequestInfo *pRI = android::addRequestToList(serial, RIL_SOCKET_1,
        RIL_REQUEST_CONFIG_SET_SLOT_MAPPING);
    if (pRI == NULL) {
        return Void();
    }
    size_t slotNum = slotMap.size();

    if (slotNum > MAX_LOGICAL_MODEM_NUM) {
        RLOGE("setSimSlotsMapping: invalid parameter");
        sendErrorResponse(pRI, RIL_E_INVALID_ARGUMENTS);
        return Void();
    }

    for (size_t socket_id = 0; socket_id < slotNum; socket_id++) {
        if (slotMap[socket_id] >= MAX_LOGICAL_MODEM_NUM) {
            RLOGE("setSimSlotsMapping: invalid parameter[%zu]", socket_id);
            sendErrorResponse(pRI, RIL_E_INVALID_ARGUMENTS);
            return Void();
        }
        // confirm logical id is not duplicate
        for (size_t nextId = socket_id + 1; nextId < slotNum; nextId++) {
            if (slotMap[socket_id] == slotMap[nextId]) {
                RLOGE("setSimSlotsMapping: slot parameter is the same:[%zu] and [%zu]",
                    socket_id, nextId);
                sendErrorResponse(pRI, RIL_E_INVALID_ARGUMENTS);
                return Void();
            }
        }
    }
    int *pSlotMap = (int *)calloc(slotNum, sizeof(int));

    for (size_t socket_id = 0; socket_id < slotNum; socket_id++) {
        pSlotMap[socket_id] = slotMap[socket_id];
    }

    CALL_ONREQUEST(RIL_REQUEST_CONFIG_SET_SLOT_MAPPING, pSlotMap,
        slotNum * sizeof(int), pRI, pRI->socket_id);
    if (pSlotMap != NULL) {
        free(pSlotMap);
    }

    return Void();
}

Return<void> RadioConfigImpl::getPhoneCapability(int32_t serial) {
#if VDBG
    RLOGD("getPhoneCapability: serial %d", serial);
#endif
    dispatchVoid(serial, mSlotId, RIL_REQUEST_CONFIG_GET_PHONE_CAPABILITY);
    return Void();
}

Return<void> RadioConfigImpl::setPreferredDataModem(int32_t serial, uint8_t modemId) {
#if VDBG
    RLOGD("setPreferredDataModem: serial %d", serial);
#endif
    dispatchInts(serial, mSlotId, RIL_REQUEST_CONFIG_SET_PREFER_DATA_MODEM, 1, modemId);
    return Void();
}

Return<void> RadioConfigImpl::setModemsConfig(int32_t serial, const V1_1::ModemsConfig& modemsConfig) {
#if VDBG
    RLOGD("setModemsConfig: serial %d", serial);
#endif
    RequestInfo *pRI = android::addRequestToList(serial, mSlotId,
        RIL_REQUEST_CONFIG_SET_MODEM_CONFIG);
    if (pRI == NULL) {
        return Void();
    }

    RIL_ModemConfig mdConfig = {};

    mdConfig.numOfLiveModems = modemsConfig.numOfLiveModems;


    CALL_ONREQUEST(RIL_REQUEST_CONFIG_SET_MODEM_CONFIG, &mdConfig,
        sizeof(RIL_ModemConfig), pRI, pRI->socket_id);

    return Void();
}

Return<void> RadioConfigImpl::getModemsConfig(int32_t serial) {
#if VDBG
    RLOGD("getModemsConfig: serial %d", serial);
#endif
    dispatchVoid(serial, mSlotId, RIL_REQUEST_CONFIG_GET_MODEM_CONFIG);
    return Void();
}

Return<void> RadioConfigImpl::getHalDeviceCapabilities(int32_t serial) {
#if VDBG
    RLOGD("getHalDeviceCapabilities: serial %d", serial);
#endif
    dispatchVoid(serial, mSlotId, RIL_REQUEST_CONFIG_GET_HAL_DEVICE_CAPABILITIES);
    return Void();
}

void radio_1_6::registerConfigService(RIL_RadioFunctions *callbacks, CommandInfo *commands) {
    using namespace android::hardware;
    using namespace std::string_literals;
    namespace compat = android::hardware::radio::compat;

    RLOGD("Entry %s", __FUNCTION__);
    const char *serviceNames = "default";

    s_vendorFunctions_config = callbacks;
    s_configCommands = commands;

    int slotId = RIL_SOCKET_1;

    pthread_rwlock_t *radioServiceRwlockPtr = getRadioServiceRwlock(0);
    int ret = pthread_rwlock_wrlock(radioServiceRwlockPtr);
    CHECK_EQ(ret, 0);
    RLOGD("registerConfigService: starting V1_2::IConfigRadio %s", serviceNames);
    radioConfigService = new RadioConfigImpl;

    radioConfigService->mSlotId = slotId;
    radioConfigService->mRadioConfigResponse = NULL;
    radioConfigService->mRadioConfigIndication = NULL;
    radioConfigService->mRadioConfigResponseV1_1 = NULL;
    radioConfigService->mRadioConfigResponseV1_2 = NULL;
    radioConfigService->mRadioConfigResponseV1_3 = NULL;
    radioConfigService->mRadioConfigIndicationV1_2 = NULL;

    // use a compat shim to convert HIDL interface to AIDL and publish it
    // TODO(bug 220004469): replace with a full AIDL implementation
    static auto aidlHal = ndk::SharedRefBase::make<compat::RadioConfig>(radioConfigService);
    const auto instance = compat::RadioConfig::descriptor + "/"s + std::string(serviceNames);
    const auto status = AServiceManager_addService(aidlHal->asBinder().get(), instance.c_str());
    RLOGD("registerConfigService addService: status %d", status);
    CHECK_EQ(status, STATUS_OK);

    ret = pthread_rwlock_unlock(radioServiceRwlockPtr);
    CHECK_EQ(ret, 0);
}

void checkReturnStatus(Return<void>& ret) {
    if (ret.isOk() == false) {
        RLOGE("checkReturnStatus_config: unable to call response/indication callback");
        // Remote process hosting the callbacks must be dead. Reset the callback objects;
        // there's no other recovery to be done here. When the client process is back up, it will
        // call setResponseFunctions()

        // Caller should already hold rdlock, release that first
        // note the current counter to avoid overwriting updates made by another thread before
        // write lock is acquired.
        int counter = mCounterRadioConfig;
        pthread_rwlock_t *radioServiceRwlockPtr = radio_1_6::getRadioServiceRwlock(0);
        int ret = pthread_rwlock_unlock(radioServiceRwlockPtr);
        CHECK_EQ(ret, 0);

        // acquire wrlock
        ret = pthread_rwlock_wrlock(radioServiceRwlockPtr);
        CHECK_EQ(ret, 0);

        // make sure the counter value has not changed
        if (counter == mCounterRadioConfig) {
            radioConfigService->mRadioConfigResponse = NULL;
            radioConfigService->mRadioConfigIndication = NULL;
            radioConfigService->mRadioConfigResponseV1_1 = NULL;
            radioConfigService->mRadioConfigResponseV1_2 = NULL;
            radioConfigService->mRadioConfigResponseV1_3 = NULL;
            radioConfigService->mRadioConfigIndicationV1_2 = NULL;
            mCounterRadioConfig++;
        } else {
            RLOGE("checkReturnStatus_config: not resetting responseFunctions as they likely "
                  "got updated on another thread");
        }

        // release wrlock
        ret = pthread_rwlock_unlock(radioServiceRwlockPtr);
        CHECK_EQ(ret, 0);

        // Reacquire rdlock
        ret = pthread_rwlock_rdlock(radioServiceRwlockPtr);
        CHECK_EQ(ret, 0);
    }
}

void RadioConfigImpl::checkReturnStatus_config(Return<void>& ret) {
    ::checkReturnStatus(ret);
}

int radio_1_6::getSimSlotsStatusResponse(int slotId, int responseType, int serial,
                                     RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("getSimSlotsResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponse != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        hidl_vec<SimSlotStatus> simSlotStatus = {};

        if ((response == NULL) || (responseLen % sizeof(RIL_SimSlotStatus_V1_2) != 0)) {
            RLOGE("getSimSlotsStatusResponse: Invalid response");
            if (e == RIL_E_SUCCESS) responseInfo.error = RadioError::INVALID_RESPONSE;
        } else {
            RIL_SimSlotStatus_V1_2 *psimSlotStatus = ((RIL_SimSlotStatus_V1_2 *) response);
            int num = responseLen / sizeof(RIL_SimSlotStatus_V1_2);
            simSlotStatus.resize(num);
            for (int i = 0; i < num; i++) {
                simSlotStatus[i].cardState = (CardState)psimSlotStatus->base.cardState;
                simSlotStatus[i].slotState = (SlotState)psimSlotStatus->base.slotState;
                simSlotStatus[i].atr = convertCharPtrToHidlString(psimSlotStatus->base.atr);
                simSlotStatus[i].logicalSlotId = psimSlotStatus->base.logicalSlotId;
                simSlotStatus[i].iccid = convertCharPtrToHidlString(psimSlotStatus->base.iccid);
                psimSlotStatus += 1;
            }
        }
        Return<void> retStatus = radioConfigService->mRadioConfigResponse->getSimSlotsStatusResponse(
                responseInfo, simSlotStatus);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("getSimSlotsResponse: radioConfigService->mRadioConfigResponse == NULL");
    }

    return 0;
}

int radio_1_6::setSimSlotsMappingResponse(int slotId, int responseType, int serial,
                                      RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("setSimSlotsMappingResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponse != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        Return<void> retStatus = radioConfigService->mRadioConfigResponse->setSimSlotsMappingResponse(
                responseInfo);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("setSimSlotsMappingResponse: radioConfigService->mRadioConfigResponse == NULL");
    }

    return 0;
}

int radio_1_6::getPhoneCapabilityResponse(int slotId, int responseType, int serial,
                                      RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("getPhoneCapabilityResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponseV1_1 != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        V1_1::PhoneCapability phoneCapability = {};
        if ((response == NULL) || (responseLen % sizeof(RIL_PhoneCapability) != 0)) {
            RLOGE("getPhoneCapabilityResponse Invalid response: NULL");
            if (e == RIL_E_SUCCESS) responseInfo.error = RadioError::INVALID_RESPONSE;
        } else {
            RIL_PhoneCapability *pCapability = (RIL_PhoneCapability *)response;
            phoneCapability.maxActiveData = pCapability->maxActiveData;
            phoneCapability.maxActiveInternetData = pCapability->maxActiveInternetData;
            phoneCapability.isInternetLingeringSupported = pCapability->isInternetLingeringSupported;
            phoneCapability.logicalModemList.resize(SIM_COUNT);
            for (int i = 0 ; i < SIM_COUNT; i++) {
                RIL_ModemInfo logicalModemInfo = pCapability->logicalModemList[i];
                phoneCapability.logicalModemList[i].modemId = logicalModemInfo.modemId;
            }
        }
        Return<void> retStatus = radioConfigService->mRadioConfigResponseV1_1->getPhoneCapabilityResponse(
                responseInfo, phoneCapability);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("getPhoneCapabilityResponse: radioConfigService->mRadioConfigResponseV1_1 == NULL");
    }

    return 0;
}

int radio_1_6::setPreferredDataModemResponse(int slotId, int responseType, int serial,
                                         RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("setPreferredDataModemResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponseV1_1 != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        Return<void> retStatus = radioConfigService->mRadioConfigResponseV1_1->setPreferredDataModemResponse(
                responseInfo);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("setPreferredDataModemResponse: radioConfigService->mRadioConfigResponseV1_1 == NULL");
    }

    return 0;
}

int radio_1_6::setModemsConfigResponse(int slotId, int responseType, int serial,
                                   RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("setModemsConfigResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponseV1_1 != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        Return<void> retStatus = radioConfigService->mRadioConfigResponseV1_1->setModemsConfigResponse(
                responseInfo);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("setModemsConfigResponse: radioConfigService->mRadioConfigResponseV1_1 == NULL");
    }

    return 0;
}

int radio_1_6::getModemsConfigResponse(int slotId, int responseType, int serial,
                                   RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("getModemsConfigResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponseV1_1 != NULL) {
        RadioResponseInfo responseInfo = {};
        populateResponseInfo(responseInfo, serial, responseType, e);
        V1_1::ModemsConfig mdCfg = {};
        RIL_ModemConfig *pMdCfg = (RIL_ModemConfig *)response;
        if ((response == NULL) || (responseLen != sizeof(RIL_ModemConfig))) {
            RLOGE("getModemsConfigResponse Invalid response: NULL");
            if (e == RIL_E_SUCCESS) responseInfo.error = RadioError::INVALID_RESPONSE;
        } else {
            mdCfg.numOfLiveModems = pMdCfg->numOfLiveModems;
        }
        Return<void> retStatus = radioConfigService->mRadioConfigResponseV1_1->getModemsConfigResponse(
                responseInfo, mdCfg);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("getModemsConfigResponse: radioConfigService->mRadioConfigResponseV1_1 == NULL");
    }

    return 0;
}

int radio_1_6::getHalDeviceCapabilitiesResponse(int slotId, int responseType, int serial,
                                   RIL_Errno e, void *response, size_t responseLen) {
#if VDBG
    RLOGD("getHalDeviceCapabilitiesResponse: serial %d", serial);
#endif

    if (radioConfigService->mRadioConfigResponseV1_3 != NULL) {
        ::android::hardware::radio::V1_6::RadioResponseInfo responseInfo = {};
        populateResponseInfo_1_6(responseInfo, serial, responseType, e);

        bool modemReducedFeatureSet1 = false;
        if (response == NULL || responseLen != sizeof(bool)) {
            RLOGE("getHalDeviceCapabilitiesResponse Invalid response.");
        } else {
            modemReducedFeatureSet1 = (*((bool *) response));
        }

        Return<void> retStatus = radioConfigService->mRadioConfigResponseV1_3->getHalDeviceCapabilitiesResponse(
                responseInfo, modemReducedFeatureSet1);
        radioConfigService->checkReturnStatus_config(retStatus);
    } else {
        RLOGE("getHalDeviceCapabilitiesResponse: radioConfigService->getHalDeviceCapabilities == NULL");
    }

    return 0;
}

int radio_1_6::simSlotsStatusChanged(int slotId, int indicationType, int token, RIL_Errno e,
                                 void *response, size_t responseLen) {
    if (radioConfigService != NULL &&
        (radioConfigService->mRadioConfigIndication != NULL ||
         radioConfigService->mRadioConfigIndicationV1_2 != NULL)) {
        if ((response == NULL) || (responseLen % sizeof(RIL_SimSlotStatus_V1_2) != 0)) {
            RLOGE("simSlotsStatusChanged: invalid response");
            return 0;
        }

        RIL_SimSlotStatus_V1_2 *psimSlotStatus = ((RIL_SimSlotStatus_V1_2 *)response);
        int num = responseLen / sizeof(RIL_SimSlotStatus_V1_2);
        if (radioConfigService->mRadioConfigIndication != NULL) {
            hidl_vec<SimSlotStatus> simSlotStatus = {};
            simSlotStatus.resize(num);
            for (int i = 0; i < num; i++) {
                simSlotStatus[i].cardState = (CardState) psimSlotStatus->base.cardState;
                simSlotStatus[i].slotState = (SlotState) psimSlotStatus->base.slotState;
                simSlotStatus[i].atr = convertCharPtrToHidlString(psimSlotStatus->base.atr);
                simSlotStatus[i].logicalSlotId = psimSlotStatus->base.logicalSlotId;
                simSlotStatus[i].iccid = convertCharPtrToHidlString(psimSlotStatus->base.iccid);
#if VDBG
                RLOGD("simSlotsStatusChanged: cardState %d slotState %d", simSlotStatus[i].cardState,
                        simSlotStatus[i].slotState);
#endif
                psimSlotStatus += 1;
            }

            Return<void> retStatus = radioConfigService->mRadioConfigIndication->simSlotsStatusChanged(
                    convertIntToRadioIndicationType(indicationType), simSlotStatus);
            radioConfigService->checkReturnStatus_config(retStatus);
        } else if (radioConfigService->mRadioConfigIndicationV1_2) {
            hidl_vec<V1_2::SimSlotStatus> simSlotStatus;
            simSlotStatus.resize(num);
            for (int i = 0; i < num; i++) {
                simSlotStatus[i].base.cardState = (CardState)(psimSlotStatus->base.cardState);
                simSlotStatus[i].base.slotState = (SlotState) psimSlotStatus->base.slotState;
                simSlotStatus[i].base.atr = convertCharPtrToHidlString(psimSlotStatus->base.atr);
                simSlotStatus[i].base.logicalSlotId = psimSlotStatus->base.logicalSlotId;
                simSlotStatus[i].base.iccid = convertCharPtrToHidlString(psimSlotStatus->base.iccid);
                simSlotStatus[i].eid = convertCharPtrToHidlString(psimSlotStatus->eid);
                psimSlotStatus += 1;
#if VDBG
            RLOGD("simSlotsStatusChanged_1_2: cardState %d slotState %d",
                    simSlotStatus[i].base.cardState, simSlotStatus[i].base.slotState);
#endif
            }

            Return<void> retStatus = radioConfigService->mRadioConfigIndicationV1_2->simSlotsStatusChanged_1_2(
                    convertIntToRadioIndicationType(indicationType), simSlotStatus);
            radioConfigService->checkReturnStatus_config(retStatus);
        }
    } else {
        RLOGE("simSlotsStatusChanged: radioService->mRadioIndication == NULL");
    }

    return 0;
}
