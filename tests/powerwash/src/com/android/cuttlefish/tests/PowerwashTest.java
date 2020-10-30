/*
 * Copyright (C) 2020 The Android Open Source Project
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

import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.testtype.DeviceTestCase;

import java.io.File;

/**
 * Test powerwash function.
 *
 * <p>* This test resets the device thus it should not run with other tests in the same test suite
 * to avoid unexpected behavior.
 *
 * <p>* The test logic relies on powerwash_cvd tool, so it can only run in a test lab setup.
 */
public class PowerwashTest extends DeviceTestCase {

    public void testPowerwash() throws Exception {
        // Create a file in tmp directory
        final String tmpFile = "/data/local/tmp/powerwash_tmp";
        getDevice().executeShellCommand("touch " + tmpFile);

        // Reboot the device to make sure the file persits.
        getDevice().reboot();
        getDevice().waitForDeviceAvailable();
        File file = getDevice().pullFile(tmpFile);
        if (file == null) {
            fail("Setup failed: tmp file failed to persist after device reboot.");
        }

        if (getDevice() instanceof RemoteAndroidVirtualDevice) {
            ((RemoteAndroidVirtualDevice) getDevice()).powerwashGce();
        } else {
            fail("This test only supports running in test lab setup.");
        }

        // Verify that the device is back online and pre-xisting file is gone.
        file = getDevice().pullFile(tmpFile);
        if (file != null) {
            fail("Powerwash failed: pre-existing file still exists.");
        }
    }
}
