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
    {0, NULL},                   // none
    {RIL_REQUEST_CONFIG_GET_SLOT_STATUS, radio_1_6::getSimSlotsStatusResponse},
    {RIL_REQUEST_CONFIG_SET_SLOT_MAPPING, radio_1_6::setSimSlotsMappingResponse},
    {RIL_REQUEST_CONFIG_GET_PHONE_CAPABILITY, radio_1_6::getPhoneCapabilityResponse},
    {RIL_REQUEST_CONFIG_SET_PREFER_DATA_MODEM, radio_1_6::setPreferredDataModemResponse},
    {RIL_REQUEST_CONFIG_SET_MODEM_CONFIG, radio_1_6::setModemsConfigResponse},
    {RIL_REQUEST_CONFIG_GET_MODEM_CONFIG, radio_1_6::getModemsConfigResponse},
    {RIL_REQUEST_CONFIG_GET_HAL_DEVICE_CAPABILITIES, radio_1_6::getHalDeviceCapabilitiesResponse},
