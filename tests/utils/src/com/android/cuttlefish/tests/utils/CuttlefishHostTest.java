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

package com.android.cuttlefish.tests.utils;

import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;

import org.junit.Before;
import org.junit.runner.RunWith;

/**
 * Base test class for interacting with a Cuttlefish device with host binaries.
 */
public abstract class CuttlefishHostTest extends BaseHostJUnit4Test {

    protected CuttlefishControlRunner runner;

    @Before
    public void cuttlefishHostTestSetUp() throws Exception {
        ITestDevice device = getDevice();
        CLog.i("Test Device Class Name: " + device.getClass().getSimpleName());
        if (device instanceof RemoteAndroidVirtualDevice) {
            runner = new CuttlefishControlRemoteRunner((RemoteAndroidVirtualDevice)device);
        } else {
            runner = new CuttlefishControlLocalRunner(getTestInformation());
        }
    }

}
