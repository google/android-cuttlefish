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

import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class FastbootFlashingTest extends BaseHostJUnit4Test {

    @Test
    public void testFastbootUserdataEraseClearsTheDevice() throws Exception {
        FactoryResetUtils.createTemporaryGuestFile(getDevice());

        getDevice().rebootIntoBootloader();
        // TODO(b/306232265) use fastboot -w instead of fastboot erase userspace
        getDevice().executeFastbootCommand("erase", "userdata");
        getDevice().reboot();

        FactoryResetUtils.assertTemporaryGuestFileExists(getDevice(), false);
    }
}
