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

import com.android.tradefed.device.TestDeviceState;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.After;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class FastbootRebootTest extends BaseHostJUnit4Test {

    @Before
    public void rebootToBootloader() throws Exception {
        getDevice().rebootIntoBootloader();
    }

    @After
    public void rebootToAndroid() throws Exception {
        getDevice().reboot();
    }

    @Test
    public void testReboot() throws Exception {
        getDevice().reboot();
        assertEquals(TestDeviceState.ONLINE, getDevice().getDeviceState());
    }

    @Test
    public void testRebootRecovery() throws Exception {
        getDevice().rebootIntoRecovery();
        assertEquals(TestDeviceState.RECOVERY, getDevice().getDeviceState());
    }

    @Test
    public void testRebootBootloader() throws Exception {
        getDevice().rebootIntoBootloader();
        assertEquals(TestDeviceState.FASTBOOT, getDevice().getDeviceState());
    }

    @Test
    public void testRebootFastboot() throws Exception {
        getDevice().rebootIntoFastbootd();
        assertEquals(TestDeviceState.FASTBOOTD, getDevice().getDeviceState());
    }
}
