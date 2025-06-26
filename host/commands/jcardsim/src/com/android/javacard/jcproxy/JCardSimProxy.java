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

/**
 * A utility class for managing interactions with the JCardsimulator. This class handles simulator
 * initialization and APDU command transmission
 */
public class JCardSimProxy {

    private JCardSimulator jcardSimulator;

    public void initialize() {
        try {
            jcardSimulator = new JCardSimulator();
            jcardSimulator.initializeSimulator();
            jcardSimulator.setupSimulator();
        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    public byte[] transmit(byte[] apdu) {
        try {
            jcardSimulator.executeApdu(apdu);
            return jcardSimulator.formatApduResponse();
        } catch (Exception e) {
            e.printStackTrace();
        }
        return new byte[0];
    }
}
