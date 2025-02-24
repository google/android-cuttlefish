/*
 * Copyright (C) 2025 The Android Open Source Project
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
import java.util.Arrays;
import java.util.List;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

/**
 * Tests that exercises HWC operations. By using `simply_red` binary, test that the HWC is showing a
 * red color on the screen by taking a screenshot through Cuttlefish to verify the HWC output.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishHwcTests extends CuttlefishHostTest {
    @Rule public TestLogData mLogs = new TestLogData();

    private static final String HWC_TEST_BINARY = "simply_red";
    private static final String DEVICE_TEST_DIR =
        "/data/cf_display_tests/" + CuttlefishHwcTests.class.getSimpleName();
    private Thread binaryRunThread;

    @Before
    public void setUp() throws Exception {
        // The binary runs indefinitely to maintain the color on the screen.
        // Host test doesn't allow commands to run in the background using `&`, so we start the
        // binary in a separate thread here.
        binaryRunThread = new Thread(() -> {
            try {
                getDevice().executeShellCommand(DEVICE_TEST_DIR + "/" + HWC_TEST_BINARY);
            } catch (Exception e) {
                CLog.e("Error running HWC_TEST_BINARY: " + e.toString());
            }
        });
        binaryRunThread.start();
    }

    @After
    public void tearDown() throws Exception {
        getDevice().executeShellCommand("pkill " + HWC_TEST_BINARY);
        binaryRunThread.interrupt();
    }

    @Test
    public void testHwcRedDisplay() throws Exception {
        final WaitForColorsResult result =
            waitForColors(Arrays.asList(ExpectedColor.create(0.5f, 0.5f, Color.RED)));

        if (!result.succeeded()) {
            saveScreenshotToTestResults("screenshot", result.failureImage(), mLogs);
        }

        assertThat(result.succeeded()).isTrue();
    }
}
