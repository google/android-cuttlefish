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

import static org.junit.Assert.assertTrue;

import com.android.tradefed.config.Option;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.device.internal.DeviceResetHandler;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;

import org.junit.Assert;
import org.junit.Assume;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;

/**
 * Test powerwash function.
 *
 * <p>* This test resets the device thus it should not run with other tests in the same test suite
 * to avoid unexpected behavior.
 *
 * <p>* The test logic relies on powerwash_cvd tool, so it can only run in a test lab setup.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class PowerwashTest extends BaseHostJUnit4Test {

    @Option(
            name = "test-count",
            description = "Number of times to powerwash the device and reboot.")
    private int mTestCount = 1;

    @Option(
            name = "force-remote-avd-type",
            description = "Force to check device type of RemoteAndroidVirtualDevice.")
    private boolean mForceRemoteAvdType = false;

    @Test
    public void testPowerwash() throws Exception {
        if (mForceRemoteAvdType) {
            Assume.assumeTrue(
                    "`force-remote-avd-type` is set to true, so device type must be"
                        + " RemoteAndroidVirtualDevice to run the test. Set the option to false to"
                        + " reset the device through Reset feature server.",
                    getDevice() instanceof RemoteAndroidVirtualDevice);
        }
        // Create a file in tmp directory
        final String tmpFile = "/data/local/tmp/powerwash_tmp";
        getDevice().executeShellCommand("touch " + tmpFile);

        // Reboot the device to make sure the file persits.
        getDevice().reboot();
        File file = getDevice().pullFile(tmpFile);
        if (file == null) {
            Assert.fail("Setup failed: tmp file failed to persist after device reboot.");
        }

        long startAllRuns = System.currentTimeMillis();
        for (int i = 0; i < mTestCount; i++) {
            CLog.d("Powerwash attempt #%d", i);
            long start = System.currentTimeMillis();
            boolean success = false;
            if (getDevice() instanceof RemoteAndroidVirtualDevice) {
                CommandResult res = ((RemoteAndroidVirtualDevice) getDevice()).powerwash();
                success = res.getStatus() == CommandStatus.SUCCESS;
            } else {
                // We don't usually expect tests to use our feature server, but in this case we are
                // validating the feature itself so it's fine
                DeviceResetHandler handler = new DeviceResetHandler(getInvocationContext());
                try {
                    success = handler.resetDevice(getDevice());
                } catch (DeviceNotAvailableException e) {
                    CLog.e(e);
                }
            }
            assertTrue(String.format("Powerwash reset failed during attemt #%d", i), success);
            long duration = System.currentTimeMillis() - start;
            CLog.d("Powerwash took %dms to finish", duration);
        }
        CLog.d(
                "%d powerwash runs finished successfully, with average time of %dms",
                mTestCount, (System.currentTimeMillis() - startAllRuns) / mTestCount);

        // Verify that the device is back online and pre-existing file is gone.
        file = getDevice().pullFile(tmpFile);
        if (file != null) {
            Assert.fail("Powerwash failed: pre-existing file still exists.");
        }
    }
}
