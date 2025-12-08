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

import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.tradefed.util.StreamUtil;
import java.awt.Color;
import java.awt.image.BufferedImage;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.util.Collections;
import java.util.List;
import javax.imageio.ImageIO;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests for VKMS writeback functionality in Cuttlefish.
 *
 * This test uses CfVkmsTester to set up a virtual display with writeback enabled
 * and reads it back using the `screencap` command to process it.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CfWritebackTest extends BaseHostJUnit4Test {
    private CfVkmsTester mVkmsTester;

    @Before
    public void setUp() throws Exception {
        List<CfVkmsTester.VkmsConnectorSetup> connectorConfigs =
                Collections.singletonList(
                        CfVkmsTester.VkmsConnectorSetup.builder()
                                .setType(CfVkmsTester.ConnectorType.EDP)
                                .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                                .setEnabledAtStart(true)
                                .build());

        mVkmsTester = CfVkmsTester.createWithConfig(getDevice(), connectorConfigs);
        assertNotNull("Failed to initialize VKMS tester", mVkmsTester);

        mVkmsTester.waitForDisplaysToBeOn(1, CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);
        // After the display is on, wait for the login screen to find a colored image to read back.
        Thread.sleep(5000);
    }

    @After
    public void tearDown() throws Exception {
        if (mVkmsTester != null) {
            mVkmsTester.close();
            mVkmsTester = null;
        }
    }

    @Test
    public void testUiIsntBlackReadback() throws Exception {
        final String imagePath = "/data/local/tmp/writeback_test.png";
        File localFile = null;
        try {
            getDevice().executeShellV2Command("mkdir -p /data/local/tmp");

            // Capture the screen content. Because we have configured the device with VKMS and
            // enabled writeback, this will exercise the desired HWC readback path.
            CommandResult screencapResult =
                    getDevice().executeShellV2Command("screencap -p " + imagePath);
            assertTrue(
                    "Failed to take screencap. Stderr: " + screencapResult.getStderr(),
                    screencapResult.getStatus() == CommandStatus.SUCCESS);

            localFile = getDevice().pullFile(imagePath);
            assertNotNull("Failed to pull screenshot file from device", localFile);

            try (InputStream is = new FileInputStream(localFile)) {
                BufferedImage image = ImageIO.read(is);
                assertNotNull("Failed to read screenshot image from file", image);

                verifyImageIsNotBlank(image, 10 /* tolerance */);
            }
        } catch (Exception e) {
            CLog.e("Exception during test execution: %s", e.getMessage());
            throw e;
        }
    }

    /**
     * Verifies that the average color of a BufferedImage is not black.
     *
     * @param image The image to check.
     * @param tolerance The acceptable value for an RGB component to be considered not black.
     */
    private void verifyImageIsNotBlank(BufferedImage image, int tolerance) {
        long totalRed = 0;
        long totalGreen = 0;
        long totalBlue = 0;
        int width = image.getWidth();
        int height = image.getHeight();
        int pixelCount = width * height;

        // Sum up the color components of all pixels.
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                Color pixelColor = new Color(image.getRGB(x, y));
                totalRed += pixelColor.getRed();
                totalGreen += pixelColor.getGreen();
                totalBlue += pixelColor.getBlue();
            }
        }

        int avgRed = (int) (totalRed / pixelCount);
        int avgGreen = (int) (totalGreen / pixelCount);
        int avgBlue = (int) (totalBlue / pixelCount);

        // Check if the average color is not black (i.e., at least one component is above tolerance).
        boolean isNotBlack = avgRed > tolerance || avgGreen > tolerance || avgBlue > tolerance;
        assertTrue(String.format("Image is black. Average color: R=%d, G=%d, B=%d. Tolerance: %d",
                                 avgRed, avgGreen, avgBlue, tolerance),
                   isNotBlack);
    }
}