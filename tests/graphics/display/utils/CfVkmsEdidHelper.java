/*
 * Copyright (C) 2025 The Android Open Source Project
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

package com.android.cuttlefish.tests;

import com.android.tradefed.log.LogUtil.CLog;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public final class CfVkmsEdidHelper {
    private CfVkmsEdidHelper() {}

    public interface Monitor {
        byte[] getBinaryEdid();
    }

    public enum EdpDisplay implements Monitor {
        REDRIX("00ffffffffffff0009e5760a00000000"
            + "191f0104a51c137803ee95a3544c9926"
            + "0f505400000001010101010101010101"
            + "010101010101125cd01881e02d503020"
            + "36001dbe1000001a623dd01881e02d50"
            + "302036001dbe1000001a000000000000"
            + "00000000000000000000000000000002"
            + "000d28ff0a3cc80f0b23c800000000cc");

        private final String edidHex;

        EdpDisplay(String edidHex) {
            this.edidHex = edidHex;
        }

        @Override
        public byte[] getBinaryEdid() {
            return hexStringToBinary(edidHex);
        }
    }

    public enum DpMonitor implements Monitor {
        ACI_9713_ASUS_VE258_DP("00ffffffffffff000469f125f3c60100"
            + "1d150104a5371f783a7695a5544ba226"
            + "115054bfef00714f81c0814081809500"
            + "950fb300d1c0023a801871382d40582c"
            + "450029372100001e000000ff0042374c"
            + "4d54463131363436370a000000fd0032"
            + "4b185311041100f0f838f03c000000fc"
            + "00415355532056453235380a202001b7"
            + "020322714f0102031112130414051f90"
            + "0e0f1d1e2309170783010000656e0c00"
            + "10008c0ad08a20e02d10103e96002937"
            + "21000018011d007251d01e206e285500"
            + "29372100001e011d00bc52d01e20b828"
            + "554029372100001e8c0ad09020403120"
            + "0c405500293721000018000000000000"
            + "000000000000000000000000000000aa"),

        DEL_61463_DELL_U2410_DP("00ffffffffffff0010ac17f04c334a31"
            + "08150104b53420783a1ec5ae4f34b126"
            + "0e5054a54b008180a940d100714f0101"
            + "010101010101283c80a070b023403020"
            + "360006442100001a000000ff00463532"
            + "354d313247314a334c0a000000fc0044"
            + "454c4c2055323431300a2020000000fd"
            + "00384c1e5111000a20202020202001ff"
            + "02031df15090050403020716011f1213"
            + "14201511062309070783010000023a80"
            + "1871382d40582c450006442100001e01"
            + "1d8018711c1620582c25000644210000"
            + "9e011d007251d01e206e285500064421"
            + "00001e8c0ad08a20e02d10103e960006"
            + "44210000180000000000000000000000"
            + "00000000000000000000000000000021"),

        HP_SPECTRE32_4K_DP("00ffffffffffff0022F01A3200000000"
            + "2E180104B54728783A87D5A8554D9F25"
            + "0E5054210800D1C0A9C081C0D100B300"
            + "9500A94081804DD000A0F0703E803020"
            + "3500C48F2100001A000000FD00183C1E"
            + "873C000A202020202020000000FC0048"
            + "502053706563747265203332000000FF"
            + "00434E43393430303030310A2020018F"
            + "020318F14B101F041303120211010514"
            + "2309070783010000A36600A0F0701F80"
            + "30203500C48F2100001A565E00A0A0A0"
            + "295030203500C48F2100001AEF5100A0"
            + "F070198030203500C48F2100001AB339"
            + "00A080381F4030203A00C48F2100001A"
            + "283C80A070B0234030203600C48F2100"
            + "001A00000000000000000000000000C4"),

        HWP_12446_HP_Z24i_DP("00ffffffffffff0022f09e3000000000"
            + "15180104a5342078264ca5a7554da226"
            + "105054a10800b30095008100a9408180"
            + "d1c081c00101283c80a070b023403020"
            + "360006442100001a000000fd00324c18"
            + "5e11000a202020202020000000fc0048"
            + "50205a3234690a2020202020000000ff"
            + "00434e343432313050334b0a2020006f");

        private final String edidHex;

        DpMonitor(String edidHex) {
            this.edidHex = edidHex;
        }

        @Override
        public byte[] getBinaryEdid() {
            return hexStringToBinary(edidHex);
        }
    }

    public enum HdmiMonitor implements Monitor {
        ACI_9155_ASUS_VH238_HDMI("00ffffffffffff000469c323fccc0000"
            + "2017010380331d782add45a3554fa027"
            + "125054bfef00714f814081809500b300"
            + "d1c001010101023a801871382d40582c"
            + "4500fd1e1100001e000000ff0044384c"
            + "4d54463035323437360a000000fd0032"
            + "4b1e5011000a202020202020000000fc"
            + "00415355532056483233380a202000be"),

        DEL_61462_DELL_U2410_HDMI("00ffffffffffff0010ac16f04c4e4332"
            + "1f13010380342078ea1ec5ae4f34b126"
            + "0e5054a54b008180a940d100714f0101"
            + "010101010101283c80a070b023403020"
            + "360006442100001a000000ff00463532"
            + "354d39375332434e4c0a000000fc0044"
            + "454c4c2055323431300a2020000000fd"
            + "00384c1e5111000a202020202020012e"
            + "020329f15090050403020716011f1213"
            + "14201511062309070767030c00100038"
            + "2d83010000e3050301023a801871382d"
            + "40582c450006442100001e011d801871"
            + "1c1620582c250006442100009e011d00"
            + "7251d01e206e28550006442100001e8c"
            + "0ad08a20e02d10103e96000644210000"
            + "1800000000000000000000000000003e"),

        HP_SPECTRE32_4K_HDMI("00ffffffffffff0022f01c3201010101"
            + "04190103804728782a87d5a8554d9f25"
            + "0e5054210800d1c0a9c081c0d100b300"
            + "9500a94081804dd000a0f0703e803020"
            + "3500c48f2100001a000000fd00183c1b"
            + "873c000a202020202020000000fc0048"
            + "702053706563747265203332000000ff"
            + "00434e43393430303030310a202001fb"
            + "02033df15361605f5d101f0413031202"
            + "11010514070616152309070783010000"
            + "6c030c001000383c200040010367d85d"
            + "c401788000e40f030000e2002b047400"
            + "30f2705a80b0588a00c48f2100001a56"
            + "5e00a0a0a0295030203500c48f210000"
            + "1eef5100a0f070198030203500c48f21"
            + "00001e000000000000000000000000a8"),

        HWP_12447_HP_Z24i_HDMI("00ffffffffffff0022f09f3001010101"
            + "1a180103803420782e3c50a7544da226"
            + "105054a1080081009500b3008180a940"
            + "81c0d1c00101283c80a070b023403020"
            + "360006442100001a000000fd00324c18"
            + "5e11000a202020202020000000fc0048"
            + "50205a3234690a2020202020000000ff"
            + "00434e4b343236304c47320a202000d6");

        private final String edidHex;

        HdmiMonitor(String edidHex) {
            this.edidHex = edidHex;
        }

        @Override
        public byte[] getBinaryEdid() {
            return hexStringToBinary(edidHex);
        }
    }

    /**
     * Convert a hex string to binary data.
     *
     * @param hexString The hex string to convert
     * @return The binary data as a byte array
     */
    private static byte[] hexStringToBinary(String hexString) {
        int len = hexString.length();

        byte[] data = new byte[len / 2];

        for (int i = 0; i < len; i += 2) {
            data[i / 2] = (byte) ((Character.digit(hexString.charAt(i), 16) << 4)
                + Character.digit(hexString.charAt(i + 1), 16));
        }

        return data;
    }

    /**
     * Utility method to get the hex string representation of EDID data.
     *
     * @param monitor The monitor to get EDID hex string from
     * @return The EDID data as a hex string
     */
    public static String getEdidHexString(Monitor monitor) {
        if (monitor == null) {
            return null;
        }

        byte[] edidData = monitor.getBinaryEdid();
        StringBuilder hexString = new StringBuilder();
        for (byte b : edidData) {
            hexString.append(String.format("%02x", b));
        }
        return hexString.toString();
    }

    /**
     * Formats EDID data for use in a printf shell command with proper hex escaping.
     *
     * @param monitor The monitor to get EDID data from
     * @return A string with escaped hex values for use in printf
     */
    public static String getEdidForPrintf(Monitor monitor) {
        if (monitor == null) {
            return null;
        }

        byte[] edidData = monitor.getBinaryEdid();
        if (edidData == null || edidData.length == 0) {
            return null;
        }

        StringBuilder hexDump = new StringBuilder();
        for (byte b : edidData) {
            hexDump.append(String.format("\\\\x%02x", b & 0xFF));
        }

        return hexDump.toString();
    }
}
