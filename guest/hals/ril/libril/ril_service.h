/*
 * Copyright (c) 2016 The Android Open Source Project
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

#ifndef RIL_SERVICE_H
#define RIL_SERVICE_H

#include <guest/hals/ril/libril/ril.h>
#include <ril_internal.h>

namespace radio_1_5 {
void registerService(RIL_RadioFunctions *callbacks, android::CommandInfo *commands);

int getIccCardStatusResponse(int slotId, int responseType,
                            int token, RIL_Errno e, void *response, size_t responselen);

int supplyIccPinForAppResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int supplyIccPukForAppResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int supplyIccPin2ForAppResponse(int slotId,
                               int responseType, int serial, RIL_Errno e, void *response,
                               size_t responselen);

int supplyIccPuk2ForAppResponse(int slotId,
                               int responseType, int serial, RIL_Errno e, void *response,
                               size_t responselen);

int changeIccPinForAppResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int changeIccPin2ForAppResponse(int slotId,
                               int responseType, int serial, RIL_Errno e, void *response,
                               size_t responselen);

int supplyNetworkDepersonalizationResponse(int slotId,
                                          int responseType, int serial, RIL_Errno e,
                                          void *response, size_t responselen);

int getCurrentCallsResponse(int slotId,
                           int responseType, int serial, RIL_Errno e, void *response,
                           size_t responselen);

int dialResponse(int slotId,
                int responseType, int serial, RIL_Errno e, void *response, size_t responselen);

int getIMSIForAppResponse(int slotId, int responseType,
                         int serial, RIL_Errno e, void *response, size_t responselen);

int hangupConnectionResponse(int slotId, int responseType,
                            int serial, RIL_Errno e, void *response, size_t responselen);

int hangupWaitingOrBackgroundResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int hangupForegroundResumeBackgroundResponse(int slotId,
                                            int responseType, int serial, RIL_Errno e,
                                            void *response, size_t responselen);

int switchWaitingOrHoldingAndActiveResponse(int slotId,
                                           int responseType, int serial, RIL_Errno e,
                                           void *response, size_t responselen);

int conferenceResponse(int slotId, int responseType,
                      int serial, RIL_Errno e, void *response, size_t responselen);

int rejectCallResponse(int slotId, int responseType,
                      int serial, RIL_Errno e, void *response, size_t responselen);

int getLastCallFailCauseResponse(int slotId,
                                int responseType, int serial, RIL_Errno e, void *response,
                                size_t responselen);

int getSignalStrengthResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responseLen);

int getVoiceRegistrationStateResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int getDataRegistrationStateResponse(int slotId,
                                    int responseType, int serial, RIL_Errno e, void *response,
                                    size_t responselen);

int getOperatorResponse(int slotId,
                       int responseType, int serial, RIL_Errno e, void *response,
                       size_t responselen);

int setRadioPowerResponse(int slotId,
                         int responseType, int serial, RIL_Errno e, void *response,
                         size_t responselen);

int sendDtmfResponse(int slotId,
                    int responseType, int serial, RIL_Errno e, void *response,
                    size_t responselen);

int sendSmsResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response,
                   size_t responselen);

int sendSMSExpectMoreResponse(int slotId,
                             int responseType, int serial, RIL_Errno e, void *response,
                             size_t responselen);

int setupDataCallResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responseLen);

int iccIOForAppResponse(int slotId,
                       int responseType, int serial, RIL_Errno e, void *response,
                       size_t responselen);

int sendUssdResponse(int slotId,
                    int responseType, int serial, RIL_Errno e, void *response,
                    size_t responselen);

int cancelPendingUssdResponse(int slotId,
                             int responseType, int serial, RIL_Errno e, void *response,
                             size_t responselen);

int getClirResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response, size_t responselen);

int setClirResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response, size_t responselen);

int getCallForwardStatusResponse(int slotId,
                                int responseType, int serial, RIL_Errno e, void *response,
                                size_t responselen);

int setCallForwardResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int getCallWaitingResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int setCallWaitingResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int acknowledgeLastIncomingGsmSmsResponse(int slotId,
                                         int responseType, int serial, RIL_Errno e, void *response,
                                         size_t responselen);

int acceptCallResponse(int slotId,
                      int responseType, int serial, RIL_Errno e, void *response,
                      size_t responselen);

int deactivateDataCallResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int getFacilityLockForAppResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e, void *response,
                                 size_t responselen);

int setFacilityLockForAppResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e, void *response,
                                 size_t responselen);

int setBarringPasswordResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int getNetworkSelectionModeResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e, void *response,
                                   size_t responselen);

int setNetworkSelectionModeAutomaticResponse(int slotId,
                                            int responseType, int serial, RIL_Errno e,
                                            void *response, size_t responselen);

int setNetworkSelectionModeManualResponse(int slotId,
                                         int responseType, int serial, RIL_Errno e, void *response,
                                         size_t responselen);

int getAvailableNetworksResponse(int slotId,
                                int responseType, int serial, RIL_Errno e, void *response,
                                size_t responselen);

int startNetworkScanResponse(int slotId,
                             int responseType, int serial, RIL_Errno e, void *response,
                             size_t responselen);

int stopNetworkScanResponse(int slotId,
                            int responseType, int serial, RIL_Errno e, void *response,
                            size_t responselen);

int startDtmfResponse(int slotId,
                     int responseType, int serial, RIL_Errno e, void *response,
                     size_t responselen);

int stopDtmfResponse(int slotId,
                    int responseType, int serial, RIL_Errno e, void *response,
                    size_t responselen);

int getBasebandVersionResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int separateConnectionResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int setMuteResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response,
                   size_t responselen);

int getMuteResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response,
                   size_t responselen);

int getClipResponse(int slotId,
                   int responseType, int serial, RIL_Errno e, void *response,
                   size_t responselen);

int getDataCallListResponse(int slotId,
                            int responseType, int serial, RIL_Errno e,
                            void *response, size_t responseLen);

int setSuppServiceNotificationsResponse(int slotId,
                                       int responseType, int serial, RIL_Errno e, void *response,
                                       size_t responselen);

int writeSmsToSimResponse(int slotId,
                         int responseType, int serial, RIL_Errno e, void *response,
                         size_t responselen);

int deleteSmsOnSimResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int setBandModeResponse(int slotId,
                       int responseType, int serial, RIL_Errno e, void *response,
                       size_t responselen);

int getAvailableBandModesResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e, void *response,
                                 size_t responselen);

int sendEnvelopeResponse(int slotId,
                        int responseType, int serial, RIL_Errno e, void *response,
                        size_t responselen);

int sendTerminalResponseToSimResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int handleStkCallSetupRequestFromSimResponse(int slotId,
                                            int responseType, int serial, RIL_Errno e,
                                            void *response, size_t responselen);

int explicitCallTransferResponse(int slotId,
                                int responseType, int serial, RIL_Errno e, void *response,
                                size_t responselen);

int setPreferredNetworkTypeResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e, void *response,
                                   size_t responselen);

int getPreferredNetworkTypeResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e, void *response,
                                   size_t responselen);

int setPreferredNetworkTypeBitmapResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e, void *response,
                                   size_t responselen);

int getPreferredNetworkTypeBitmapResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e, void *response,
                                   size_t responselen);

int getNeighboringCidsResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int setLocationUpdatesResponse(int slotId,
                              int responseType, int serial, RIL_Errno e, void *response,
                              size_t responselen);

int setCdmaSubscriptionSourceResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int setCdmaRoamingPreferenceResponse(int slotId,
                                    int responseType, int serial, RIL_Errno e, void *response,
                                    size_t responselen);

int getCdmaRoamingPreferenceResponse(int slotId,
                                    int responseType, int serial, RIL_Errno e, void *response,
                                    size_t responselen);

int setTTYModeResponse(int slotId,
                      int responseType, int serial, RIL_Errno e, void *response,
                      size_t responselen);

int getTTYModeResponse(int slotId,
                      int responseType, int serial, RIL_Errno e, void *response,
                      size_t responselen);

int setPreferredVoicePrivacyResponse(int slotId,
                                    int responseType, int serial, RIL_Errno e, void *response,
                                    size_t responselen);

int getPreferredVoicePrivacyResponse(int slotId,
                                    int responseType, int serial, RIL_Errno e, void *response,
                                    size_t responselen);

int sendCDMAFeatureCodeResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responselen);

int sendBurstDtmfResponse(int slotId,
                         int responseType, int serial, RIL_Errno e, void *response,
                         size_t responselen);

int sendCdmaSmsResponse(int slotId,
                       int responseType, int serial, RIL_Errno e, void *response,
                       size_t responselen);

int acknowledgeLastIncomingCdmaSmsResponse(int slotId,
                                          int responseType, int serial, RIL_Errno e, void *response,
                                          size_t responselen);

int getGsmBroadcastConfigResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e, void *response,
                                 size_t responselen);

int setGsmBroadcastConfigResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e, void *response,
                                 size_t responselen);

int setGsmBroadcastActivationResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int getCdmaBroadcastConfigResponse(int slotId,
                                  int responseType, int serial, RIL_Errno e, void *response,
                                  size_t responselen);

int setCdmaBroadcastConfigResponse(int slotId,
                                  int responseType, int serial, RIL_Errno e, void *response,
                                  size_t responselen);

int setCdmaBroadcastActivationResponse(int slotId,
                                      int responseType, int serial, RIL_Errno e,
                                      void *response, size_t responselen);

int getCDMASubscriptionResponse(int slotId,
                               int responseType, int serial, RIL_Errno e, void *response,
                               size_t responselen);

int writeSmsToRuimResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int deleteSmsOnRuimResponse(int slotId,
                           int responseType, int serial, RIL_Errno e, void *response,
                           size_t responselen);

int getDeviceIdentityResponse(int slotId,
                             int responseType, int serial, RIL_Errno e, void *response,
                             size_t responselen);

int exitEmergencyCallbackModeResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int getSmscAddressResponse(int slotId,
                          int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int setCdmaBroadcastActivationResponse(int slotId,
                                      int responseType, int serial, RIL_Errno e,
                                      void *response, size_t responselen);

int setSmscAddressResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int reportSmsMemoryStatusResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e,
                                 void *response, size_t responselen);

int reportStkServiceIsRunningResponse(int slotId,
                                      int responseType, int serial, RIL_Errno e,
                                      void *response, size_t responseLen);

int getCdmaSubscriptionSourceResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int requestIsimAuthenticationResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e, void *response,
                                     size_t responselen);

int acknowledgeIncomingGsmSmsWithPduResponse(int slotId,
                                            int responseType, int serial, RIL_Errno e,
                                            void *response, size_t responselen);

int sendEnvelopeWithStatusResponse(int slotId,
                                  int responseType, int serial, RIL_Errno e, void *response,
                                  size_t responselen);

int getVoiceRadioTechnologyResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e,
                                   void *response, size_t responselen);

int getCellInfoListResponse(int slotId,
                            int responseType,
                            int serial, RIL_Errno e, void *response,
                            size_t responseLen);

int setCellInfoListRateResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responselen);

int setInitialAttachApnResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responselen);

int getImsRegistrationStateResponse(int slotId,
                                   int responseType, int serial, RIL_Errno e,
                                   void *response, size_t responselen);

int sendImsSmsResponse(int slotId, int responseType,
                      int serial, RIL_Errno e, void *response, size_t responselen);

int iccTransmitApduBasicChannelResponse(int slotId,
                                       int responseType, int serial, RIL_Errno e,
                                       void *response, size_t responselen);

int iccOpenLogicalChannelResponse(int slotId,
                                  int responseType, int serial, RIL_Errno e, void *response,
                                  size_t responselen);


int iccCloseLogicalChannelResponse(int slotId,
                                  int responseType, int serial, RIL_Errno e,
                                  void *response, size_t responselen);

int iccTransmitApduLogicalChannelResponse(int slotId,
                                         int responseType, int serial, RIL_Errno e,
                                         void *response, size_t responselen);

int nvReadItemResponse(int slotId,
                      int responseType, int serial, RIL_Errno e,
                      void *response, size_t responselen);


int nvWriteItemResponse(int slotId,
                       int responseType, int serial, RIL_Errno e,
                       void *response, size_t responselen);

int nvWriteCdmaPrlResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int nvResetConfigResponse(int slotId,
                         int responseType, int serial, RIL_Errno e,
                         void *response, size_t responselen);

int setUiccSubscriptionResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responselen);

int setDataAllowedResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int getHardwareConfigResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responseLen);

int requestIccSimAuthenticationResponse(int slotId,
                                       int responseType, int serial, RIL_Errno e,
                                       void *response, size_t responselen);

int setDataProfileResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int requestShutdownResponse(int slotId,
                           int responseType, int serial, RIL_Errno e,
                           void *response, size_t responselen);

int getRadioCapabilityResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responseLen);

int setRadioCapabilityResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responseLen);

int startLceServiceResponse(int slotId,
                           int responseType, int serial, RIL_Errno e,
                           void *response, size_t responselen);

int stopLceServiceResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int pullLceDataResponse(int slotId,
                        int responseType, int serial, RIL_Errno e,
                        void *response, size_t responseLen);

int getModemActivityInfoResponse(int slotId,
                                int responseType, int serial, RIL_Errno e,
                                void *response, size_t responselen);

int getModemStackStatusResponse(int slotId,
                                int responseType, int serial, RIL_Errno e,
                                void *response, size_t responselen);

int enableModemResponse(int slotId, int responseType, int serial, RIL_Errno e,
                                void *response, size_t responselen);

int setAllowedCarriersResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responselen);

int getAllowedCarriersResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responselen);

int sendDeviceStateResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responselen);

int setIndicationFilterResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responselen);

int setSimCardPowerResponse(int slotId,
                              int responseType, int serial, RIL_Errno e,
                              void *response, size_t responselen);

int startKeepaliveResponse(int slotId,
                           int responseType, int serial, RIL_Errno e,
                           void *response, size_t responselen);

int stopKeepaliveResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

void acknowledgeRequest(int slotId, int serial);

int radioStateChangedInd(int slotId,
                          int indicationType, int token, RIL_Errno e, void *response,
                          size_t responseLen);

int callStateChangedInd(int slotId, int indType, int token,
                        RIL_Errno e, void *response, size_t responselen);

int networkStateChangedInd(int slotId, int indType,
                                int token, RIL_Errno e, void *response, size_t responselen);

int newSmsInd(int slotId, int indicationType,
              int token, RIL_Errno e, void *response, size_t responselen);

int newSmsStatusReportInd(int slotId, int indicationType,
                          int token, RIL_Errno e, void *response, size_t responselen);

int newSmsOnSimInd(int slotId, int indicationType,
                   int token, RIL_Errno e, void *response, size_t responselen);

int onUssdInd(int slotId, int indicationType,
              int token, RIL_Errno e, void *response, size_t responselen);

int nitzTimeReceivedInd(int slotId, int indicationType,
                        int token, RIL_Errno e, void *response, size_t responselen);

int currentSignalStrengthInd(int slotId,
                             int indicationType, int token, RIL_Errno e,
                             void *response, size_t responselen);

int dataCallListChangedInd(int slotId, int indicationType,
                           int token, RIL_Errno e, void *response, size_t responselen);

int suppSvcNotifyInd(int slotId, int indicationType,
                     int token, RIL_Errno e, void *response, size_t responselen);

int stkSessionEndInd(int slotId, int indicationType,
                     int token, RIL_Errno e, void *response, size_t responselen);

int stkProactiveCommandInd(int slotId, int indicationType,
                           int token, RIL_Errno e, void *response, size_t responselen);

int stkEventNotifyInd(int slotId, int indicationType,
                      int token, RIL_Errno e, void *response, size_t responselen);

int stkCallSetupInd(int slotId, int indicationType,
                    int token, RIL_Errno e, void *response, size_t responselen);

int simSmsStorageFullInd(int slotId, int indicationType,
                         int token, RIL_Errno e, void *response, size_t responselen);

int simRefreshInd(int slotId, int indicationType,
                  int token, RIL_Errno e, void *response, size_t responselen);

int callRingInd(int slotId, int indicationType,
                int token, RIL_Errno e, void *response, size_t responselen);

int simStatusChangedInd(int slotId, int indicationType,
                        int token, RIL_Errno e, void *response, size_t responselen);

int cdmaNewSmsInd(int slotId, int indicationType,
                  int token, RIL_Errno e, void *response, size_t responselen);

int newBroadcastSmsInd(int slotId,
                       int indicationType, int token, RIL_Errno e, void *response,
                       size_t responselen);

int cdmaRuimSmsStorageFullInd(int slotId,
                              int indicationType, int token, RIL_Errno e, void *response,
                              size_t responselen);

int restrictedStateChangedInd(int slotId,
                              int indicationType, int token, RIL_Errno e, void *response,
                              size_t responselen);

int enterEmergencyCallbackModeInd(int slotId,
                                  int indicationType, int token, RIL_Errno e, void *response,
                                  size_t responselen);

int cdmaCallWaitingInd(int slotId,
                       int indicationType, int token, RIL_Errno e, void *response,
                       size_t responselen);

int cdmaOtaProvisionStatusInd(int slotId,
                              int indicationType, int token, RIL_Errno e, void *response,
                              size_t responselen);

int cdmaInfoRecInd(int slotId,
                   int indicationType, int token, RIL_Errno e, void *response,
                   size_t responselen);

int oemHookRawInd(int slotId,
                  int indicationType, int token, RIL_Errno e, void *response,
                  size_t responselen);

int indicateRingbackToneInd(int slotId,
                            int indicationType, int token, RIL_Errno e, void *response,
                            size_t responselen);

int resendIncallMuteInd(int slotId,
                        int indicationType, int token, RIL_Errno e, void *response,
                        size_t responselen);

int cdmaSubscriptionSourceChangedInd(int slotId,
                                     int indicationType, int token, RIL_Errno e,
                                     void *response, size_t responselen);

int cdmaPrlChangedInd(int slotId,
                      int indicationType, int token, RIL_Errno e, void *response,
                      size_t responselen);

int exitEmergencyCallbackModeInd(int slotId,
                                 int indicationType, int token, RIL_Errno e, void *response,
                                 size_t responselen);

int rilConnectedInd(int slotId,
                    int indicationType, int token, RIL_Errno e, void *response,
                    size_t responselen);

int voiceRadioTechChangedInd(int slotId,
                             int indicationType, int token, RIL_Errno e, void *response,
                             size_t responselen);

int cellInfoListInd(int slotId,
                    int indicationType, int token, RIL_Errno e, void *response,
                    size_t responselen);

int imsNetworkStateChangedInd(int slotId,
                              int indicationType, int token, RIL_Errno e, void *response,
                              size_t responselen);

int subscriptionStatusChangedInd(int slotId,
                                 int indicationType, int token, RIL_Errno e, void *response,
                                 size_t responselen);

int srvccStateNotifyInd(int slotId,
                        int indicationType, int token, RIL_Errno e, void *response,
                        size_t responselen);

int hardwareConfigChangedInd(int slotId,
                             int indicationType, int token, RIL_Errno e, void *response,
                             size_t responselen);

int radioCapabilityIndicationInd(int slotId,
                                 int indicationType, int token, RIL_Errno e, void *response,
                                 size_t responselen);

int onSupplementaryServiceIndicationInd(int slotId,
                                        int indicationType, int token, RIL_Errno e,
                                        void *response, size_t responselen);

int stkCallControlAlphaNotifyInd(int slotId,
                                 int indicationType, int token, RIL_Errno e, void *response,
                                 size_t responselen);

int lceDataInd(int slotId,
               int indicationType, int token, RIL_Errno e, void *response,
               size_t responselen);

int pcoDataInd(int slotId,
               int indicationType, int token, RIL_Errno e, void *response,
               size_t responselen);

int modemResetInd(int slotId,
                  int indicationType, int token, RIL_Errno e, void *response,
                  size_t responselen);

int networkScanResultInd(int slotId,
                         int indicationType, int token, RIL_Errno e, void *response,
                         size_t responselen);

int keepaliveStatusInd(int slotId,
                       int indicationType, int token, RIL_Errno e, void *response,
                       size_t responselen);

int sendRequestRawResponse(int slotId,
                           int responseType, int serial, RIL_Errno e,
                           void *response, size_t responseLen);

int sendRequestStringsResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responseLen);

int setCarrierInfoForImsiEncryptionResponse(int slotId,
                                            int responseType, int serial, RIL_Errno e,
                                            void *response, size_t responseLen);

int emergencyDialResponse(int slotId,
                          int responseType, int serial, RIL_Errno e,
                          void *response, size_t responselen);

int carrierInfoForImsiEncryption(int slotId,
                        int responseType, int serial, RIL_Errno e,
                        void *response, size_t responseLen);

int setSystemSelectionChannelsResponse(int slotId,
                               int responseType, int serial, RIL_Errno e,
                               void *response, size_t responseLen);

int setSignalStrengthReportingCriteriaResponse(int slotId, int responseType, int serial,
                                               RIL_Errno e, void *response, size_t responselen);

int setLinkCapacityReportingCriteriaResponse(int slotId, int responseType, int serial,
                                             RIL_Errno e, void *response, size_t responselen);

int enableUiccApplicationsResponse(int slotId,
                                 int responseType, int serial, RIL_Errno e,
                                 void *response, size_t responselen);

int areUiccApplicationsEnabledResponse(int slotId,
                                     int responseType, int serial, RIL_Errno e,
                                     void *response, size_t responselen);

int setRadioPowerResponse(int slotId, int responseType, int serial, RIL_Errno e, void *response,
                          size_t responselen);

int getBarringInfoResponse(int slotId, int responseType, int serial, RIL_Errno e, void *response,
                           size_t responselen);

int sendCdmaSmsExpectMoreResponse(int slotId, int responseType, int serial, RIL_Errno e,
                                  void *response, size_t responselen);

int supplySimDepersonalizationResponse(int slotId, int responseType, int serial, RIL_Errno e,
                                       void *response, size_t responselen);

pthread_rwlock_t * getRadioServiceRwlock(int slotId);

void setNitzTimeReceived(int slotId, long timeReceived);

}   // namespace radio

#endif  // RIL_SERVICE_H
