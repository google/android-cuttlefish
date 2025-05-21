/*
 * Copyright (C) 2024 The Android Open Source Project
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

package com.android.javacard.jcproxy;

import com.licel.jcardsim.smartcardio.CardSimulator;

/**
 * This class provisions the KeyMint Applet with OEM Root key and secure boot flag. The attest ids
 * and preshared key are sent from the cuttlefish guest.
 */
public class KeymintOEMProvision extends KeymintProvision {
    public static final byte INS_PROVISION_ATTEST_IDS_CMD = 0x03;
    public static final byte INS_PROVISION_OEM_ROOT_PUBLIC_KEY_CMD = 0x0B;
    public static final byte INS_PROVISION_PRESHARED_SECRET_CMD = 0x0F;
    public static final byte INS_OEM_LOCK_PROVISIONING_CMD = 0x11;
    public static final byte INS_PROVISION_SECURE_BOOT_MODE_CMD = 0x12;

    private static final String OEM_ROOT_PUB_KEY =
            "04A7F74EF221DD1FDB1987BF3805ED4E8284AF9299367EB8BADA59FED6387060"
                    + "DAD505F283F60BD282CB8E21F5F752FF8255CAF257078EEA7AB0825984E775FBB2";

    private static final String OEM_ROOT_KEY =
            "83" // array(3)
                    + "A4" // map(4) - KeyParameters
                    + "1A10000002" // unsigned(268435458)
                    + "03" // unsigned(3)
                    + "1A20000005" // unsigned(536870917)
                    + "41" // bytes(1)
                    + "04" // 0x04
                    + "1A1000000A" // unsigned(268435466)
                    + "01" // unsigned(1)
                    + "1A20000001" // unsigned(536870913)
                    + "41" // bytes(1)
                    + "03" // 0x03
                    + "03" // unsigned(3) - KeyFormat(RAW)
                    + "5841" // bytes(65)   - public key
                    + OEM_ROOT_PUB_KEY;

    private static final byte SECURE_BOOT_MODE = 0x00; // not fused.

    KeymintOEMProvision(CardSimulator simulator) {
        super(simulator);
    }

    public void provision() {
        // Provision of preshared key comes from guest.
        // Provision of attest ids comes from HAL
        provisionOemRootKey();
        provisionSecureBoot();
    }

    public void provisionOemRootKey() {
        byte[] apdu = ByteArrayConverter.hexStringToByteArray(OEM_ROOT_KEY);
        transmit(INS_PROVISION_OEM_ROOT_PUBLIC_KEY_CMD, apdu);
    }

    public void provisionSecureBoot() {
        byte[] apdu = new byte[2];
        apdu[0] = (byte) 0x81; // Array of 1
        apdu[1] = SECURE_BOOT_MODE;
        transmit(INS_PROVISION_SECURE_BOOT_MODE_CMD, apdu);
    }
}
