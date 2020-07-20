/* ///guest/hals/ril/libril/ril_unsol_commands.h
**
** Copyright 2006, The Android Open Source Project
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
    {RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, radio_1_5::radioStateChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, radio_1_5::callStateChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, radio_1_5::networkStateChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_NEW_SMS, radio_1_5::newSmsInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT, radio_1_5::newSmsStatusReportInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_NEW_SMS_ON_SIM, radio_1_5::newSmsOnSimInd, WAKE_PARTIAL},
    {RIL_UNSOL_ON_USSD, radio_1_5::onUssdInd, WAKE_PARTIAL},
    {RIL_UNSOL_ON_USSD_REQUEST, radio_1_5::onUssdInd, DONT_WAKE},
    {RIL_UNSOL_NITZ_TIME_RECEIVED, radio_1_5::nitzTimeReceivedInd, WAKE_PARTIAL},
    {RIL_UNSOL_SIGNAL_STRENGTH, radio_1_5::currentSignalStrengthInd, DONT_WAKE},
    {RIL_UNSOL_DATA_CALL_LIST_CHANGED, radio_1_5::dataCallListChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_SUPP_SVC_NOTIFICATION, radio_1_5::suppSvcNotifyInd, WAKE_PARTIAL},
    {RIL_UNSOL_STK_SESSION_END, radio_1_5::stkSessionEndInd, WAKE_PARTIAL},
    {RIL_UNSOL_STK_PROACTIVE_COMMAND, radio_1_5::stkProactiveCommandInd, WAKE_PARTIAL},
    {RIL_UNSOL_STK_EVENT_NOTIFY, radio_1_5::stkEventNotifyInd, WAKE_PARTIAL},
    {RIL_UNSOL_STK_CALL_SETUP, radio_1_5::stkCallSetupInd, WAKE_PARTIAL},
    {RIL_UNSOL_SIM_SMS_STORAGE_FULL, radio_1_5::simSmsStorageFullInd, WAKE_PARTIAL},
    {RIL_UNSOL_SIM_REFRESH, radio_1_5::simRefreshInd, WAKE_PARTIAL},
    {RIL_UNSOL_CALL_RING, radio_1_5::callRingInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, radio_1_5::simStatusChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_CDMA_NEW_SMS, radio_1_5::cdmaNewSmsInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_NEW_BROADCAST_SMS, radio_1_5::newBroadcastSmsInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_RUIM_SMS_STORAGE_FULL, radio_1_5::cdmaRuimSmsStorageFullInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESTRICTED_STATE_CHANGED, radio_1_5::restrictedStateChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_ENTER_EMERGENCY_CALLBACK_MODE, radio_1_5::enterEmergencyCallbackModeInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_CALL_WAITING, radio_1_5::cdmaCallWaitingInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_OTA_PROVISION_STATUS, radio_1_5::cdmaOtaProvisionStatusInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_INFO_REC, radio_1_5::cdmaInfoRecInd, WAKE_PARTIAL},
    {RIL_UNSOL_OEM_HOOK_RAW, radio_1_5::oemHookRawInd, WAKE_PARTIAL},
    {RIL_UNSOL_RINGBACK_TONE, radio_1_5::indicateRingbackToneInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESEND_INCALL_MUTE, radio_1_5::resendIncallMuteInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_SUBSCRIPTION_SOURCE_CHANGED, radio_1_5::cdmaSubscriptionSourceChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_CDMA_PRL_CHANGED, radio_1_5::cdmaPrlChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_EXIT_EMERGENCY_CALLBACK_MODE, radio_1_5::exitEmergencyCallbackModeInd, WAKE_PARTIAL},
    {RIL_UNSOL_RIL_CONNECTED, radio_1_5::rilConnectedInd, WAKE_PARTIAL},
    {RIL_UNSOL_VOICE_RADIO_TECH_CHANGED, radio_1_5::voiceRadioTechChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_CELL_INFO_LIST, radio_1_5::cellInfoListInd, WAKE_PARTIAL},
    {RIL_UNSOL_RESPONSE_IMS_NETWORK_STATE_CHANGED, radio_1_5::imsNetworkStateChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_UICC_SUBSCRIPTION_STATUS_CHANGED, radio_1_5::subscriptionStatusChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_SRVCC_STATE_NOTIFY, radio_1_5::srvccStateNotifyInd, WAKE_PARTIAL},
    {RIL_UNSOL_HARDWARE_CONFIG_CHANGED, radio_1_5::hardwareConfigChangedInd, WAKE_PARTIAL},
    {RIL_UNSOL_DC_RT_INFO_CHANGED, NULL, WAKE_PARTIAL},
    {RIL_UNSOL_RADIO_CAPABILITY, radio_1_5::radioCapabilityIndicationInd, WAKE_PARTIAL},
    {RIL_UNSOL_ON_SS, radio_1_5::onSupplementaryServiceIndicationInd, WAKE_PARTIAL},
    {RIL_UNSOL_STK_CC_ALPHA_NOTIFY, radio_1_5::stkCallControlAlphaNotifyInd, WAKE_PARTIAL},
    {RIL_UNSOL_LCEDATA_RECV, radio_1_5::lceDataInd, WAKE_PARTIAL},
    {RIL_UNSOL_PCO_DATA, radio_1_5::pcoDataInd, WAKE_PARTIAL},
    {RIL_UNSOL_MODEM_RESTART, radio_1_5::modemResetInd, WAKE_PARTIAL},
    {RIL_UNSOL_CARRIER_INFO_IMSI_ENCRYPTION, radio_1_5::carrierInfoForImsiEncryption, WAKE_PARTIAL},
    {RIL_UNSOL_NETWORK_SCAN_RESULT, radio_1_5::networkScanResultInd, WAKE_PARTIAL},
