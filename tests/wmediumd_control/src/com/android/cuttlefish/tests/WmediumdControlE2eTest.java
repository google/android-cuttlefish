/*
 * Copyright (C) 2022 The Android Open Source Project
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

import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.log.LogUtil.CLog;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class WmediumdControlE2eTest extends BaseHostJUnit4Test {

    ITestDevice testDevice;
    WmediumdControlRunner runner;

    @Before
    public void setUp() throws Exception {
        testDevice = getDevice();
        CLog.i("Test Device Class Name: " + testDevice.getClass().getSimpleName());

        if (testDevice instanceof RemoteAndroidVirtualDevice) {
            runner = new WmediumdControlRemoteRunner((RemoteAndroidVirtualDevice)testDevice);
        }
        else {
            runner = new WmediumdControlLocalRunner(testDevice, getTestInformation());
        }
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlListStations() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        runner.listStations();
    }
}
