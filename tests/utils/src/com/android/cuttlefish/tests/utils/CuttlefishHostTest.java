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

import static com.google.common.truth.Truth.assertThat;

import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.result.ByteArrayInputStreamSource;
import com.android.tradefed.result.InputStreamSource;
import com.android.tradefed.result.LogDataType;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.google.auto.value.AutoValue;

import java.awt.image.BufferedImage;
import java.awt.Color;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.util.ArrayList;
import java.util.List;

import javax.annotation.Nullable;
import javax.imageio.ImageIO;

import org.junit.Before;
import org.junit.runner.RunWith;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runner.RunWith;
import org.junit.runners.model.Statement;

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

    private static final long DEFAULT_COMMAND_TIMEOUT_MS = 5000;

    private static final int SCREENSHOT_CHECK_ATTEMPTS = 5;

    private static final int SCREENSHOT_CHECK_TIMEOUT_MILLISECONDS = 1000;

    private static final String CVD_DISPLAY_BINARY_BASENAME = "cvd_internal_display";

    protected BufferedImage getDisplayScreenshot() throws Exception {
        File screenshotTempFile =  File.createTempFile("screenshot", ".png");
        screenshotTempFile.deleteOnExit();

        // TODO: Switch back to using `cvd` after either:
        //  * Commands under `cvd` can be used with instances launched through `launch_cvd`.
        //  * ATP launches instances using `cvd start` instead of `launch_cvd`.
        String cvdDisplayBinary = runner.getHostBinaryPath(CVD_DISPLAY_BINARY_BASENAME);

        List<String> fullCommand = new ArrayList<String>();
        fullCommand.add(cvdDisplayBinary);
        fullCommand.add("screenshot");
        fullCommand.add("--screenshot_path=" + screenshotTempFile.getAbsolutePath());

        CommandResult result = runner.run(DEFAULT_COMMAND_TIMEOUT_MS, fullCommand.toArray(new String[0]));
        if (!CommandStatus.SUCCESS.equals(result.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run display screenshot command:\nstdout: %s\nstderr: %s",
                                  result.getStdout(),
                                  result.getStderr()));
        }

        BufferedImage screenshot = ImageIO.read(runner.getFile(screenshotTempFile.getAbsolutePath()));
        if (screenshot == null) {
            throw new IllegalStateException(String.format("Failed to read screenshot from %s", screenshotTempFile));
        }

        return screenshot;
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
        public static ExpectedColor create(float u, float v, Color color) {
            return new AutoValue_CuttlefishHostTest_ExpectedColor(u, v, color);
        }

        public abstract float u();
        public abstract float v();
        public abstract Color color();
    }

    @AutoValue
    public static abstract class WaitForColorsResult {
        public static WaitForColorsResult create(@Nullable BufferedImage image) {
            return new AutoValue_CuttlefishHostTest_WaitForColorsResult(image);
        }

        public @Nullable abstract BufferedImage failureImage();

        public boolean succeeded() { return failureImage() == null; }
    }

    protected WaitForColorsResult waitForColors(List<ExpectedColor> expectedColors) throws Exception {
        assertThat(expectedColors).isNotEmpty();

        BufferedImage screenshot = null;

        for (int attempt = 0; attempt < SCREENSHOT_CHECK_ATTEMPTS; attempt++) {
            CLog.i("Grabbing screenshot (attempt %d of %d)", attempt, SCREENSHOT_CHECK_ATTEMPTS);

            screenshot = getDisplayScreenshot();

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

    protected void saveScreenshotToTestResults(String name, BufferedImage screenshot,
            TestLogData testLogs) throws Exception {
        ByteArrayOutputStream bytesOutputStream = new ByteArrayOutputStream();
        ImageIO.write(screenshot, "png", bytesOutputStream);
        byte[] bytes = bytesOutputStream.toByteArray();
        ByteArrayInputStreamSource bytesInputStream = new ByteArrayInputStreamSource(bytes);
        testLogs.addTestLog(name, LogDataType.PNG, bytesInputStream);
    }

}
