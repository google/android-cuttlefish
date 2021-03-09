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

import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests that Gfxstream was enabled as the virtual device's OpenGL and Vulkan driver.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class GfxstreamEnabledTest extends BaseHostJUnit4Test {

    @Test
    public void testGfxstreamEnabled() throws Exception {
        if (!(getDevice() instanceof RemoteAndroidVirtualDevice)) {
            return;
        }

        assertThat(getDevice().getProperty("ro.hardware.egl")).isEqualTo("emulation");
        assertThat(getDevice().getProperty("ro.hardware.vulkan")).isEqualTo("ranchu");
    }
}