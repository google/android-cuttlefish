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

import static com.google.common.truth.Truth.assertThat;

import com.android.cuttlefish.tests.utils.CuttlefishHostTest;
import com.android.cuttlefish.tests.utils.UnlockScreenRule;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import java.awt.Color;
import java.awt.image.BufferedImage;
import java.util.Arrays;
import java.util.List;
import javax.imageio.ImageIO;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

/**
 * Tests that a Cuttlefish device can interactively connect and disconnect displays.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishDisplayTests extends CuttlefishHostTest {

    @Rule
    public TestLogData mLogs = new TestLogData();

    @Rule
    public final UnlockScreenRule mUnlockScreenRule = new UnlockScreenRule(this);

    private static final String FULLSCREEN_COLOR_APK =
        "CuttlefishVulkanSamplesFullscreenColor.apk";
    private static final String FULLSCREEN_COLOR_PKG =
        "com.android.cuttlefish.vulkan_samples.fullscreen_color";
    private static final String FULLSCREEN_COLOR_PKG_MAIN_ACTIVITY =
        "android.app.NativeActivity";

    @Before
    public void setUp() throws Exception {
        getDevice().uninstallPackage(FULLSCREEN_COLOR_PKG);
        installPackage(FULLSCREEN_COLOR_APK);
    }

    @After
    public void tearDown() throws Exception {
        getDevice().uninstallPackage(FULLSCREEN_COLOR_PKG);
    }

    @Test
    public void testBasicDisplayOutput() throws Exception {
        getDevice().executeShellCommand(
            String.format("am start -n %s/%s", FULLSCREEN_COLOR_PKG,
                          FULLSCREEN_COLOR_PKG_MAIN_ACTIVITY));

        final WaitForColorsResult result =
            waitForColors(Arrays.asList(ExpectedColor.create(0.5f, 0.5f, Color.RED)));
        if (!result.succeeded()) {
            saveScreenshotToTestResults("screenshot", result.failureImage(), mLogs);
        }
        assertThat(result.succeeded()).isTrue();
    }

}
