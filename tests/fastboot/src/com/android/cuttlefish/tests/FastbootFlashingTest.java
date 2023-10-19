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
package com.android.cuttlefish.tests;

import static org.junit.Assert.assertEquals;

import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class FastbootFlashingTest extends BaseHostJUnit4Test {

    private static final String TMP_GUEST_FILE_PATH = "/data/check_factory_reset.txt";

    @Test
    public void testFastbootUserdataEraseClearsTheDevice() throws Exception {
        createTemporaryGuestFile();

        getDevice().rebootIntoBootloader();
        // TODO(b/306232265) use fastboot -w instead of fastboot erase userspace
        getDevice().executeFastbootCommand("erase", "userdata");
        getDevice().reboot();

        assertTemporaryGuestFileExists(false);
    }

    private void createTemporaryGuestFile() throws DeviceNotAvailableException {
        getDevice().enableAdbRoot();
        getDevice().executeShellV2Command("touch " + TMP_GUEST_FILE_PATH);

        assertTemporaryGuestFileExists(true);
    }

    private void assertTemporaryGuestFileExists(boolean exists) throws DeviceNotAvailableException {
        assertEquals(
                String.format(
                        "Expected file %s %s",
                        TMP_GUEST_FILE_PATH,
                        (exists ? "exists" : "not exists")),
                exists,
                getDevice().doesFileExist(TMP_GUEST_FILE_PATH));
    }
}
