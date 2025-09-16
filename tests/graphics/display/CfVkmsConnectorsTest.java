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

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.android.cuttlefish.tests.utils.CuttlefishHostTest;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests for VKMS display connector configuration and detection in Cuttlefish.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CfVkmsConnectorsTest extends BaseHostJUnit4Test {
    private CfVkmsTester mVkmsTester;
    private String mSurfaceFlingerDumpsys;
    private int mExpectedDisplayCount;

    @Before
    public void setUp() throws Exception {
        List<CfVkmsTester.VkmsConnectorSetup> mConnectorConfigs = new ArrayList<>();
        mConnectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.EDP)
                .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                .setEnabledAtStart(true)
                .build());
        mConnectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(1)
                .setMonitor(CfVkmsEdidHelper.DpMonitor.HP_SPECTRE32_4K_DP)
                .build());
        mConnectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.HDMI_A)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(2)
                .setMonitor(CfVkmsEdidHelper.HdmiMonitor.ACI_9155_ASUS_VH238_HDMI)
                .build());
        mConnectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.HDMI_A)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(3)
                .setMonitor(CfVkmsEdidHelper.HdmiMonitor.HWP_12447_HP_Z24i_HDMI)
                .build());
        mConnectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(4)
                .setMonitor(CfVkmsEdidHelper.DpMonitor.DEL_61463_DELL_U2410_DP)
                .build());
        mExpectedDisplayCount = mConnectorConfigs.size();

        // Initialize VKMS with our configuration
        mVkmsTester = CfVkmsTester.createWithConfig(getDevice(), mConnectorConfigs);
        assertNotNull("Failed to initialize VKMS tester", mVkmsTester);

        // Wait for displays to be detected.
        mVkmsTester.waitForDisplaysToBeOn(
            mExpectedDisplayCount, CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);

        // Get the final dumpsys output for the tests
        String command = "dumpsys SurfaceFlinger --displays";
        CommandResult result = getDevice().executeShellV2Command(command);
        assertEquals(
            "Failed to execute dumpsys command", CommandStatus.SUCCESS, result.getStatus());
        mSurfaceFlingerDumpsys = result.getStdout();
    }

    @After
    public void tearDown() throws Exception {
        if (mVkmsTester != null) {
            mVkmsTester.close();
            mVkmsTester = null;
        }
    }

    /**
     * Test to verify that all configured displays are detected by SurfaceFlinger.
     * Parses the output of "dumpsys SurfaceFlinger --displays" to count the number of displays.
     */
    @Test
    public void testConnectorDisplayCountCheck() throws Exception {
        // Count the number of displays in the output
        int displayCount = getNumberOfDisplays(mSurfaceFlingerDumpsys);

        // Log the output for debugging
        CLog.i("Found %d displays in SurfaceFlinger", displayCount);

        // Verify the number of displays matches the expected count
        assertEquals("Number of displays does not match expected count", mExpectedDisplayCount,
            displayCount);
    }

    /**
     * Test to verify that all expected display names are present in the SurfaceFlinger output.
     * Parses the output of "dumpsys SurfaceFlinger --displays" to extract display names.
     */
    @Test
    public void testConnectorDisplayNamesCheck() throws Exception {
        // Define expected display names based on the configured monitors
        Set<String> expectedDisplayNames = new HashSet<>();
        // No name present in CfVkmsEdidHelper.EdpDisplay.REDRIX's EDID
        expectedDisplayNames.add("");
        expectedDisplayNames.add("HP Spectre 32");
        expectedDisplayNames.add("ASUS VH238");
        expectedDisplayNames.add("HP Z24i");
        expectedDisplayNames.add("DELL U2410");

        // Extract display names from the output
        Set<String> actualDisplayNames = new HashSet<>();
        if (mSurfaceFlingerDumpsys != null && !mSurfaceFlingerDumpsys.isEmpty()) {
            // Pattern to match name="DisplayName" in the output
            Pattern pattern = Pattern.compile("name=\"([^\"]*)\"", Pattern.MULTILINE);
            Matcher matcher = pattern.matcher(mSurfaceFlingerDumpsys);

            while (matcher.find()) {
                actualDisplayNames.add(matcher.group(1));
            }
        }

        // Log the found display names for debugging
        CLog.i("Found display names: %s", actualDisplayNames);

        // Verify that all expected display names are present
        for (String expectedName : expectedDisplayNames) {
            boolean found = false;
            for (String actualName : actualDisplayNames) {
                if (actualName.contains(expectedName)) {
                    found = true;
                    break;
                }
            }
            assertTrue("Expected display name not found: " + expectedName, found);
        }

        // Verify the count matches
        assertEquals("Number of displays does not match expected count",
            expectedDisplayNames.size(), actualDisplayNames.size());
    }

    private int getNumberOfDisplays(String dumpsysOutput) {
        int displayCount = 0;
        if (dumpsysOutput != null && !dumpsysOutput.isEmpty()) {
            // Use regex to find lines starting with "Display"
            // This pattern matches lines like "Display 0" or "Display 4621520188814754049"
            Pattern pattern = Pattern.compile("^Display\\s+(\\d+|\\w+)", Pattern.MULTILINE);
            Matcher matcher = pattern.matcher(dumpsysOutput);
            while (matcher.find()) {
                displayCount++;
            }
        }
        return displayCount;
    }
}
