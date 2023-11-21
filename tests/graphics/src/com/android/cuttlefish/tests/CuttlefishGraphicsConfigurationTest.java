/*
 * Copyright (C) 2021 The Android Open Source Project
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

import static com.google.common.truth.Truth.assertThat;

import com.android.tradefed.config.Option;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests that Cuttlefish is using a specified EGL and VK implementation.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishGraphicsConfigurationTest extends BaseHostJUnit4Test {

    private static final String EGL_SYSPROP = "ro.hardware.egl";

    private static final String VULKAN_SYSPROP = "ro.hardware.vulkan";

    @Option(name="expected-egl",
            description="The expected EGL implementation enabled on the device.")
    private String mExpectedEgl = "";

    @Option(name="expected-vulkan",
            description="The expected Vulkan implementation enabled on the device.")
    private String mExpectedVulkan = "";

    @Test
    public void testGraphicsConfiguration() throws Exception {
        final ITestDevice device = getDevice();

        final String deviceEgl = device.getProperty(EGL_SYSPROP);
        CLog.i("Device's %s is \"%s\"", EGL_SYSPROP, deviceEgl);
        if (!mExpectedEgl.isEmpty()) {
            assertThat(deviceEgl).isEqualTo(mExpectedEgl);
        }

        final String deviceVulkan = device.getProperty(VULKAN_SYSPROP);
        CLog.i("Device's %s is \"%s\"", VULKAN_SYSPROP, deviceVulkan);
        if (!mExpectedVulkan.isEmpty()) {
            assertThat(deviceVulkan).isEqualTo(mExpectedVulkan);
        }
    }
}