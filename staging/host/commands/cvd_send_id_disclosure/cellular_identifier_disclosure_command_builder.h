/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <string>

namespace cuttlefish {

/**
 * Allows for the serialization of
 * aidl::android::hardware::radio::network::CellularIdentifierDisclosure objects
 * into an AT command that can be processed by the cuttlefish RIL to trigger
 * unsolicited calls to
 * aidl::android::hardware::radio::network::IRadioNetworkIndication::cellularIdentifierDisclosed.
 */
std::string GetATCommand(const std::string &plmn, int32_t identifierType,
                         int32_t protocolMessage, bool isEmergency);

}  // namespace cuttlefish
