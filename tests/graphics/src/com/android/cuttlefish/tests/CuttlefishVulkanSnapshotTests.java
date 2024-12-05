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
import com.android.tradefed.config.Option;
import com.android.tradefed.device.internal.DeviceResetHandler;
import com.android.tradefed.device.internal.DeviceSnapshotHandler;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import java.awt.Color;
import java.awt.image.BufferedImage;
import java.io.File;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import javax.annotation.Nullable;
import javax.imageio.ImageIO;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TestRule;
import org.junit.runner.RunWith;

/**
 * Test snapshot/restore function.
 *
 * <p>* This test resets the device thus it should not run with other tests in the same test suite
 * to avoid unexpected behavior.
 *
 * <p>* The test logic relies on cvd and snapshot_util_cvd tools, so it can only run in a test lab
 * setup.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishVulkanSnapshotTests extends CuttlefishHostTest {
    private static final String VK_SAMPLES_MAIN_ACTIVITY = "android.app.NativeActivity";

    private static final String VK_SAMPLES_FULLSCREEN_COLOR_APK =
        "CuttlefishVulkanSamplesFullscreenColor.apk";
    private static final String VK_SAMPLES_FULLSCREEN_COLOR_PKG =
        "com.android.cuttlefish.vulkan_samples.fullscreen_color";

    private static final String VK_SAMPLES_FULLSCREEN_TEXTURE_APK =
        "CuttlefishVulkanSamplesFullscreenTexture.apk";
    private static final String VK_SAMPLES_FULLSCREEN_TEXTURE_PKG =
        "com.android.cuttlefish.vulkan_samples.fullscreen_texture";

    private static final String VK_SAMPLES_SECONDARY_COMMAND_BUFFER_APK =
        "CuttlefishVulkanSamplesSecondaryCommandBuffer.apk";
    private static final String VK_SAMPLES_SECONDARY_COMMAND_BUFFER_PKG =
        "com.android.cuttlefish.vulkan_samples.secondary_command_buffer";

    private static final List<String> VK_SAMPLE_APKS =
        Arrays.asList(VK_SAMPLES_FULLSCREEN_COLOR_APK, //
            VK_SAMPLES_FULLSCREEN_TEXTURE_APK, //
            VK_SAMPLES_SECONDARY_COMMAND_BUFFER_APK);

    private static final List<String> VK_SAMPLE_PKGS =
        Arrays.asList(VK_SAMPLES_FULLSCREEN_COLOR_PKG, //
            VK_SAMPLES_FULLSCREEN_TEXTURE_PKG, //
            VK_SAMPLES_SECONDARY_COMMAND_BUFFER_PKG);

    @Rule
    public TestLogData mLogs = new TestLogData();

    @Rule
    public final UnlockScreenRule mUnlockScreenRule = new UnlockScreenRule(this);

    @Before
    public void setUp() throws Exception {
        for (String apk : VK_SAMPLE_PKGS) {
            getDevice().uninstallPackage(apk);
        }
        for (String apk : VK_SAMPLE_APKS) {
            installPackage(apk);
        }
    }

    @After
    public void tearDown() throws Exception {
        for (String apk : VK_SAMPLE_PKGS) {
            getDevice().uninstallPackage(apk);
        }
    }

    private void runOneSnapshotTest(String pkg, List<ExpectedColor> expectedColors)
        throws Exception {
        final String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        // Reboot to make sure device isn't dirty from previous tests.
        getDevice().reboot();

        mUnlockScreenRule.unlockDevice();

        getDevice().executeShellCommand(
            String.format("am start -n %s/%s", pkg, VK_SAMPLES_MAIN_ACTIVITY));

        final WaitForColorsResult beforeSnapshotResult = waitForColors(expectedColors);
        if (!beforeSnapshotResult.succeeded()) {
            saveScreenshotToTestResults(
                "before_snapshot_restore_screenshot", beforeSnapshotResult.failureImage(), mLogs);
        }
        assertThat(beforeSnapshotResult.succeeded()).isTrue();

        // Snapshot the device
        new DeviceSnapshotHandler().snapshotDevice(getDevice(), snapshotId);

        try {
            new DeviceSnapshotHandler().restoreSnapshotDevice(getDevice(), snapshotId);
        } finally {
            new DeviceSnapshotHandler().deleteSnapshot(getDevice(), snapshotId);
        }

        final WaitForColorsResult afterSnapshotRestoreResult = waitForColors(expectedColors);
        if (!afterSnapshotRestoreResult.succeeded()) {
            saveScreenshotToTestResults(
                "after_snapshot_restore_screenshot", afterSnapshotRestoreResult.failureImage(), mLogs);
        }
        assertThat(afterSnapshotRestoreResult.succeeded()).isTrue();
    }

    @Test
    public void testFullscreenColorSample() throws Exception {
        final List<ExpectedColor> expectedColors =
            Arrays.asList(ExpectedColor.create(0.5f, 0.5f, Color.RED));
        runOneSnapshotTest(VK_SAMPLES_FULLSCREEN_COLOR_PKG, expectedColors);
    }

    @Test
    public void testFullscreenTextureSample() throws Exception {
        final List<ExpectedColor> expectedColors = Arrays.asList(
            // clang-format off
                ExpectedColor.create(0.25f, 0.25f, Color.RED),    // bottomLeft
                ExpectedColor.create(0.75f, 0.25f, Color.GREEN),  // bottomRight
                ExpectedColor.create(0.25f, 0.75f, Color.BLUE),   // topLeft
                ExpectedColor.create(0.75f, 0.75f, Color.WHITE)   // topRight
            // clang-format on
        );
        runOneSnapshotTest(VK_SAMPLES_FULLSCREEN_TEXTURE_PKG, expectedColors);
    }

    @Test
    public void testSecondaryCommandBufferSample() throws Exception {
        final List<ExpectedColor> expectedColors = Arrays.asList(
            // clang-format off
                ExpectedColor.create(0.5f, 0.5f, Color.RED)
            // clang-format on
        );
        runOneSnapshotTest(VK_SAMPLES_SECONDARY_COMMAND_BUFFER_PKG, expectedColors);
    }
}
