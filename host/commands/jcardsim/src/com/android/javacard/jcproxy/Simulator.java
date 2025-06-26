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
 * This interface provides methods to manage and interact with a secure element simulation
 * environment, typically used for testing APDU communication in secure element development.
 */
public interface Simulator {
    /**
     * Initializes the simulator environment. This method must be called before any other operation
     * to ensure the simulator is ready for use.
     */
    void initializeSimulator() throws Exception;

    /** Disconnects and cleans up the simulator. */
    void disconnectSimulator() throws Exception;

    /** Configures the simulator by installing and personalizing necessary applets. */
    void setupSimulator() throws Exception;

    /**
     * Executes an APDU command on the simulator and returns the response.
     *
     * @param apdu byte array containing the apdu
     * @return apdu response
     */
    byte[] executeApdu(byte[] apdu) throws Exception;

    /*
     * Formats the return response.
     * @return formatted response
     */
    byte[] formatApduResponse();
}
