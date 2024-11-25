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

import com.android.tradefed.config.Option;
import com.android.tradefed.device.internal.DeviceResetHandler;
import com.android.tradefed.device.internal.DeviceSnapshotHandler;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.result.ByteArrayInputStreamSource;
import com.android.tradefed.result.InputStreamSource;
import com.android.tradefed.result.LogDataType;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.google.auto.value.AutoValue;
import java.awt.Color;
import java.awt.image.BufferedImage;
import java.io.ByteArrayOutputStream;
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
import org.junit.runner.Description;
import org.junit.runner.RunWith;
import org.junit.runners.model.Statement;

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
public class CuttlefishVulkanSnapshotTests extends BaseHostJUnit4Test {
    private static final String VK_SAMPLES_MAIN_ACTIVITY = "android.app.NativeActivity";

    private static final String VK_SAMPLES_FULLSCREEN_COLOR_APK =
        "CuttlefishVulkanSamplesFullscreenColor.apk";
    private static final String VK_SAMPLES_FULLSCREEN_COLOR_PKG =
        "com.android.cuttlefish.vulkan_samples.fullscreen_color";

    private static final String VK_SAMPLES_FULLSCREEN_TEXTURE_APK =
        "CuttlefishVulkanSamplesFullscreenTexture.apk";
    private static final String VK_SAMPLES_FULLSCREEN_TEXTURE_PKG =
        "com.android.cuttlefish.vulkan_samples.fullscreen_texture";

    private static final List<String> VK_SAMPLE_APKS =
        Arrays.asList(VK_SAMPLES_FULLSCREEN_COLOR_APK, VK_SAMPLES_FULLSCREEN_TEXTURE_APK);
    private static final List<String> VK_SAMPLE_PKGS =
        Arrays.asList(VK_SAMPLES_FULLSCREEN_COLOR_PKG, VK_SAMPLES_FULLSCREEN_TEXTURE_PKG);

    private static final int SCREENSHOT_CHECK_ATTEMPTS = 5;

    private static final int SCREENSHOT_CHECK_TIMEOUT_MILLISECONDS = 1000;

    @Rule
    public TestLogData mLogs = new TestLogData();

    // TODO: Move this into `device/google/cuttlefish/tests/utils` if it works?
    @Rule
    public final TestRule mUnlockScreenRule = new TestRule() {
        @Override
        public Statement apply(Statement base, Description description) {
            return new Statement() {
                @Override
                public void evaluate() throws Throwable {
                    getDevice().unlockDevice();

                    base.evaluate();
                }
            };
        }
    };

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

    private void saveScreenshotToTestResults(String name, BufferedImage screenshot) throws Exception {
        ByteArrayOutputStream bytesOutputStream = new ByteArrayOutputStream();
        ImageIO.write(screenshot, "png", bytesOutputStream);
        byte[] bytes = bytesOutputStream.toByteArray();
        ByteArrayInputStreamSource bytesInputStream = new ByteArrayInputStreamSource(bytes);
        mLogs.addTestLog(name, LogDataType.PNG, bytesInputStream);
    }

    private BufferedImage getScreenshot() throws Exception {
        InputStreamSource screenshotStream = getDevice().getScreenshot();

        assertThat(screenshotStream).isNotNull();

        return ImageIO.read(screenshotStream.createInputStream());
    }

    // Vulkan implementations can support different levels of precision which can
    // result in slight pixel differences. This threshold should be small but was
    // otherwise chosen arbitrarily to allow for small differences.
    private static final int PIXEL_DIFFERENCE_THRESHOLD = 16;

    private boolean isApproximatelyEqual(Color actual, Color expected) {
        int diff = Math.abs(actual.getRed() - expected.getRed())
            + Math.abs(actual.getGreen() - expected.getGreen())
            + Math.abs(actual.getBlue() - expected.getBlue());
        return diff <= PIXEL_DIFFERENCE_THRESHOLD;
    }

    @AutoValue
    public static abstract class ExpectedColor {
        static ExpectedColor create(float u, float v, Color color) {
            return new AutoValue_CuttlefishVulkanSnapshotTests_ExpectedColor(u, v, color);
        }

        abstract float u();
        abstract float v();
        abstract Color color();
    }

    @AutoValue
    public static abstract class WaitForColorsResult {
        static WaitForColorsResult create(@Nullable BufferedImage image) {
            return new AutoValue_CuttlefishVulkanSnapshotTests_WaitForColorsResult(image);
        }

        @Nullable abstract BufferedImage failureImage();

        boolean succeeded() { return failureImage() == null; }
    }


    private WaitForColorsResult waitForColors(List<ExpectedColor> expectedColors) throws Exception {
        assertThat(expectedColors).isNotEmpty();

        BufferedImage screenshot = null;

        for (int attempt = 0; attempt < SCREENSHOT_CHECK_ATTEMPTS; attempt++) {
            CLog.i("Grabbing screenshot (attempt %d of %d)", attempt, SCREENSHOT_CHECK_ATTEMPTS);

            screenshot = getScreenshot();

            final int screenshotW = screenshot.getWidth();
            final int screenshotH = screenshot.getHeight();

            boolean foundAllExpectedColors = true;
            for (ExpectedColor expected : expectedColors) {
                final float sampleU = expected.u();

                // Images from `getDevice().getScreenshot()` seem to use the top left as the
                // the origin. Flip-y here for what is (subjectively) the more natural origin.
                final float sampleV = 1.0f - expected.v();

                final int sampleX = (int) (sampleU * (float) screenshotW);
                final int sampleY = (int) (sampleV * (float) screenshotH);

                final Color sampledColor = new Color(screenshot.getRGB(sampleX, sampleY));
                final Color expectedColor = expected.color();

                if (!isApproximatelyEqual(sampledColor, expectedColor)) {
                    CLog.i("Screenshot check %d failed at u:%f v:%f (x:%d y:%d with w:%d h:%d) "
                            + "expected:%s actual:%s",
                        attempt, sampleU, sampleV, sampleX, sampleY, screenshotW, screenshotH,
                        expectedColor, sampledColor);
                    foundAllExpectedColors = false;
                }
            }

            if (foundAllExpectedColors) {
                CLog.i("Screenshot attempt %d found all expected colors.", attempt);
                return WaitForColorsResult.create(null);
            }

            CLog.i("Screenshot attempt %d did not find all expected colors. Sleeping for %d ms and "
                    + "trying again.",
                attempt, SCREENSHOT_CHECK_TIMEOUT_MILLISECONDS);

            Thread.sleep(SCREENSHOT_CHECK_TIMEOUT_MILLISECONDS);
        }

        return WaitForColorsResult.create(screenshot);
    }

    private void runOneSnapshotTest(String pkg, List<ExpectedColor> expectedColors)
        throws Exception {
        final String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        // Reboot to make sure device isn't dirty from previous tests.
        getDevice().reboot();

        getDevice().executeShellCommand(
            String.format("am start -n %s/%s", pkg, VK_SAMPLES_MAIN_ACTIVITY));

        final WaitForColorsResult beforeSnapshotResult = waitForColors(expectedColors);
        if (!beforeSnapshotResult.succeeded()) {
            saveScreenshotToTestResults("before_snapshot_restore_screenshot", beforeSnapshotResult.failureImage());
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
            saveScreenshotToTestResults("after_snapshot_restore_screenshot", afterSnapshotRestoreResult.failureImage());
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
}
