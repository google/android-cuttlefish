/*
 * Copyright (C) 2018 The Android Open Source Project
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

enum hwsim_cmd {
    HWSIM_CMD_UNSPEC,
    HWSIM_CMD_REGISTER,
    HWSIM_CMD_FRAME,
    HWSIM_CMD_TX_INFO_FRAME,
    HWSIM_CMD_NEW_RADIO,
    HWSIM_CMD_DEL_RADIO,
    HWSIM_CMD_GET_RADIO,
    HWSIM_CMD_SUBSCRIBE,
    __HWSIM_CMD_MAX
};

enum hwsim_attr {
    /* 0 */ HWSIM_ATTR_UNSPEC,
    /* 1 */ HWSIM_ATTR_ADDR_RECEIVER,
    /* 2 */ HWSIM_ATTR_ADDR_TRANSMITTER,
    /* 3 */ HWSIM_ATTR_FRAME,
    /* 4 */ HWSIM_ATTR_FLAGS,
    /* 5 */ HWSIM_ATTR_RX_RATE,
    /* 6 */ HWSIM_ATTR_SIGNAL,
    /* 7 */ HWSIM_ATTR_TX_INFO,
    /* 8 */ HWSIM_ATTR_COOKIE,
    /* 9 */ HWSIM_ATTR_CHANNELS,
    /* 10 */ HWSIM_ATTR_RADIO_ID,
    /* 11 */ HWSIM_ATTR_REG_HINT_ALPHA2,
    /* 12 */ HWSIM_ATTR_REG_CUSTOM_REG,
    /* 13 */ HWSIM_ATTR_REG_STRICT_REG,
    /* 14 */ HWSIM_ATTR_SUPPORT_P2P_DEVICE,
    /* 15 */ HWSIM_ATTR_USE_CHANCTX,
    /* 16 */ HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE,
    /* 17 */ HWSIM_ATTR_RADIO_NAME,
    /* 18 */ HWSIM_ATTR_NO_VIF,
    /* 19 */ HWSIM_ATTR_FREQ,
    __HWSIM_ATTR_MAX
};

enum hwsim_tx_control_flags {
    HWSIM_TX_CTL_REQ_TX_STATUS  = 1,
    HWSIM_TX_CTL_NO_ACK         = 2,
    HWSIM_TX_STAT_ACK           = 4,
};
