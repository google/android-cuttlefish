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

import javacard.framework.Util;

import java.io.ByteArrayOutputStream;
import java.io.IOException;

/**
 * This class provisions the KeyMint applet by preparing the APDU header and trasmitting the APDU to
 * the jcardsim.
 */
public class KeymintProvision {
    public static final byte APDU_CLS = (byte) 0x80;
    public static final byte APDU_P1 = (byte) 0x60;
    public static final byte APDU_P2 = (byte) 0x00;
    public static final short APDU_RESP_STATUS_OK = (short) 0x9000;
    private CardSimulator simulator;

    public KeymintProvision(CardSimulator simulator) {
        this.simulator = simulator;
    }

    public byte[] addApduHeader(byte ins, byte[] apdu) {
        try {
            ByteArrayOutputStream bao = new ByteArrayOutputStream();
            bao.write(APDU_CLS);
            bao.write(ins);
            bao.write(APDU_P1);
            bao.write(APDU_P2);
            int apduLen = (apdu != null) ? apdu.length : 0;
            if (Short.MAX_VALUE >= apduLen) {
                if (apduLen > 0) {
                    bao.write(0x00);
                    bao.write((byte) ((apduLen >> 8) & 0xFF));
                    bao.write((byte) (apduLen & 0xFF));
                    bao.write(apdu);
                } else {
                    bao.write(0x00);
                }
                // Expected length of the output
                bao.write(0x00);
                bao.write(0x00);
            }
            return bao.toByteArray();
        } catch (IOException e) {
            e.printStackTrace();
            throw new RuntimeException("Failed to add APDU header");
        }
    }

    public void transmit(byte ins, byte[] apduData) {
        byte[] apdu = addApduHeader(ins, apduData);
        byte[] resp = simulator.transmitCommand(apdu);
        if (resp == null) {
            throw new RuntimeException("Response is null for INS: " + ins);
        }
        int respLen = resp.length;
        if (respLen != 4) {
            throw new RuntimeException("Response is not equal to 4 for INS: " + ins);
        }

        if (resp[0] != 0x81 && resp[1] != 0x00) {
            throw new RuntimeException(
                    "Error response code for INS: "
                            + ins
                            + " expected:0x8100"
                            + ", but got: "
                            + Util.getShort(resp, (short) 0));
        }

        if (resp[respLen - 2] != 0x90 && resp[respLen - 1] != 0x00) {
            throw new RuntimeException(
                    "ISO error code for INS: "
                            + ins
                            + " expected:0x9000"
                            + ", but got: "
                            + Util.getShort(resp, (short) (respLen - 2)));
        }
    }
}
