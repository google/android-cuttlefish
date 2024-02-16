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
package com.android.cuttlefish.tests;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

// Expectations from the device to run this test:
// 1. Allowed to be unlocked by carrier: true
// 2. Allowed to be unlocked by device: true
// 3. Locked: false
@RunWith(DeviceJUnit4ClassRunner.class)
public class OemlockTest extends BaseHostJUnit4Test {

    @Test
    public void testLockingUnlocking() throws Exception {
        // lock the device, verify factory reset happen and we cannot erase
        FactoryResetUtils.createTemporaryGuestFile(getDevice());
        getDevice().rebootIntoBootloader();
        setLockedState(true);
        FactoryResetUtils.assertTemporaryGuestFileExists(getDevice(), false);
        verifyDeviceIsLocked(true);
        getDevice().rebootIntoBootloader();
        verifyErasingSuccess(false);
        getDevice().reboot();

        // unlock the device, verify factory reset happen and we can erase
        FactoryResetUtils.createTemporaryGuestFile(getDevice());
        getDevice().rebootIntoBootloader();
        setLockedState(false);
        FactoryResetUtils.assertTemporaryGuestFileExists(getDevice(), false);
        verifyDeviceIsLocked(false);
        getDevice().rebootIntoBootloader();
        verifyErasingSuccess(true);
        getDevice().reboot();
    }

    private void setLockedState(boolean locked) throws DeviceNotAvailableException {
        final String flashingCommand = locked ? "lock" : "unlock";
        final CommandResult result = getDevice().executeFastbootCommand("flashing",
                flashingCommand);

        Assert.assertEquals(
                String.format("Failed to %s the device. Stdout: %s stderr: %s", flashingCommand,
                        result.getStdout(), result.getStderr()),
                result.getStatus(),
                CommandStatus.SUCCESS
        );

        getDevice().waitForDeviceAvailable();
    }

    private void verifyErasingSuccess(boolean success) throws DeviceNotAvailableException {
        final CommandResult eraseResult = getDevice().executeFastbootCommand(
                "erase", "userdata");
        Assert.assertEquals(
                String.format("Erase command must %s", success ? "succeed" : "failed"),
                eraseResult.getStatus(),
                success ? CommandStatus.SUCCESS : CommandStatus.FAILED
        );
    }

    private void verifyDeviceIsLocked(boolean locked) throws DeviceNotAvailableException {
        final String expectedState = locked ? "locked" : "unlocked";
        Assert.assertEquals(
                String.format("Checking that device locking state is: %s", expectedState),
                getDevice().getProperty("ro.boot.vbmeta.device_state"),
                expectedState
        );
    }
}
