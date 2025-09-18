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
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests for VKMS display ID uniqueness and related properties in Cuttlefish.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CfVkmsDisplaysTest extends BaseHostJUnit4Test {
    private CfVkmsTester mVkmsTester;

    private static class DisplayInfo {
        public String id;
        public String hwcId;
        public String port;
        public String pnpId;
        public String displayName;

        @Override
        public String toString() {
            return String.format(
                "ID=%s, HWC=%s, port=%s, pnpId=%s, name=%s", id, hwcId, port, pnpId, displayName);
        }
    }

    @After
    public void tearDown() throws Exception {
        if (mVkmsTester != null) {
            mVkmsTester.close();
            mVkmsTester = null;
        }
    }

    /**
     * Test to verify that all display IDs are unique.
     * Parses the output of "dumpsys SurfaceFlinger --display-id" to extract display IDs.
     */
    @Test
    public void testDisplayIdsAreUnique() throws Exception {
        // Setup the VKMS configuration for this test
        List<CfVkmsTester.VkmsConnectorSetup> connectorConfigs = new ArrayList<>();
        connectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.EDP)
                .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                .setEnabledAtStart(true)
                .build());
        connectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(1)
                .setMonitor(CfVkmsEdidHelper.DpMonitor.HP_SPECTRE32_4K_DP)
                .build());
        connectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.HDMI_A)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(2)
                .setMonitor(CfVkmsEdidHelper.HdmiMonitor.ACI_9155_ASUS_VH238_HDMI)
                .build());
        connectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.HDMI_A)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(3)
                .setMonitor(CfVkmsEdidHelper.HdmiMonitor.HWP_12447_HP_Z24i_HDMI)
                .build());
        connectorConfigs.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setEnabledAtStart(true)
                .setAdditionalOverlayPlanes(4)
                .setMonitor(CfVkmsEdidHelper.DpMonitor.DEL_61463_DELL_U2410_DP)
                .build());

        // Initialize VKMS with our configuration
        mVkmsTester = CfVkmsTester.createWithConfig(getDevice(), connectorConfigs);
        assertNotNull("Failed to initialize VKMS tester", mVkmsTester);

        mVkmsTester.waitForDisplaysToBeOn(
            connectorConfigs.size(), CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);
        String command = "dumpsys SurfaceFlinger --display-id";
        CommandResult result = getDevice().executeShellV2Command(command);
        assertEquals(
            "Failed to execute dumpsys command", CommandStatus.SUCCESS, result.getStatus());
        List<DisplayInfo> displays = parseDisplayInfo(result.getStdout());
        // Verify we have the expected number of displays
        assertEquals("Number of displays does not match expected count", connectorConfigs.size(),
            displays.size());

        // Check that all display IDs are unique
        Set<String> displayIds = new HashSet<>();
        for (DisplayInfo info : displays) {
            boolean wasAdded = displayIds.add(info.id);
            assertTrue("Display ID is not unique: " + info.id, wasAdded);
        }
    }

    /**
     * Test to verify that the same monitor maintains the same display ID across different
     * configurations. This ensures that the display ID is determined by the monitor's EDID
     * rather than by connection order or port number.
     *
     * Note: Android currently requires that the primary display is marked as internal,
     * otherwise SurfaceFlinger will crash. This is a requirement for all Hardware
     * Abstraction Layer (HAL) implementations, which means that the first
     * connected display will always report itself as internal (regardless of
     * its true type).
     * See:
     * https://source.android.com/docs/core/display/multi_display/displays#more_displays
     *
     * Because EDID-based IDs are only enabled on external displays, we must
     * test this case in any display position other than first.
     */
    @Test
    public void testDisplayIdConsistencyAtDifferentPorts() throws Exception {
        // We'll use the HP Spectre 32 as our reference monitor to track across configurations
        CfVkmsEdidHelper.Monitor referenceMonitor = CfVkmsEdidHelper.DpMonitor.HP_SPECTRE32_4K_DP;
        String referenceDisplayName = "HP Spectre 32";

        // Map to store the display ID for each configuration
        Map<String, String> displayIdsByConfig = new HashMap<>();

        // Test multiple configurations
        String configName = "unknown";
        try {
            // First configuration: Reference monitor in second position.
            configName = "second_position";
            List<CfVkmsTester.VkmsConnectorSetup> config = new ArrayList<>();
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.EDP)
                    .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                    .setEnabledAtStart(true)
                    .build());
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());

            String displayId =
                testConfigurationAndGetDisplayId(config, referenceDisplayName, configName);
            displayIdsByConfig.put(configName, displayId);

            // Second configuration: Reference monitor with different connector type in second
            // position
            configName = "second_position+different_connector";
            config = new ArrayList<>();
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.EDP)
                    .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                    .setEnabledAtStart(true)
                    .build());
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A)
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());

            displayId = testConfigurationAndGetDisplayId(config, referenceDisplayName, configName);
            displayIdsByConfig.put(configName, displayId);

            // Third configuration: Many displays including reference
            configName = "many_displays";
            config = new ArrayList<>();
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.EDP)
                    .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                    .setEnabledAtStart(true)
                    .build());
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A)
                    .setMonitor(CfVkmsEdidHelper.HdmiMonitor.ACI_9155_ASUS_VH238_HDMI)
                    .setEnabledAtStart(true)
                    .build());
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A)
                    .setMonitor(CfVkmsEdidHelper.HdmiMonitor.HWP_12447_HP_Z24i_HDMI)
                    .setEnabledAtStart(true)
                    .build());

            displayId = testConfigurationAndGetDisplayId(config, referenceDisplayName, configName);
            displayIdsByConfig.put(configName, displayId);
        } catch (Exception e) {
            CLog.e("Exception during configuration %s: %s", configName, e.toString());
            throw e;
        }

        // Verify all display IDs for the reference monitor are the same
        CLog.i("Display IDs by configuration:");
        String referenceId = null;
        for (Map.Entry<String, String> entry : displayIdsByConfig.entrySet()) {
            CLog.i("  %s: %s", entry.getKey(), entry.getValue());
            if (referenceId == null) {
                referenceId = entry.getValue();
            } else {
                assertEquals("Display ID should be consistent across configurations", referenceId,
                    entry.getValue());
            }
        }
    }

    /**
     * Test to verify that identical monitors (same EDID) still receive unique display IDs.
     * This tests the collision handling in Android's display ID generation system.
     */
    @Test
    public void testIdenticalMonitorsGetUniqueIds() throws Exception {
        // Use the HP Spectre 32 monitor for our test
        CfVkmsEdidHelper.Monitor referenceMonitor = CfVkmsEdidHelper.DpMonitor.HP_SPECTRE32_4K_DP;
        String referenceDisplayName = "HP Spectre 32";

        // Create a configuration with multiple identical monitors
        List<CfVkmsTester.VkmsConnectorSetup> collisionConfig = new ArrayList<>();

        // Add an internal panel
        collisionConfig.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.EDP)
                .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                .setEnabledAtStart(true)
                .build());

        // Add three identical monitors on different ports
        // First on DisplayPort
        collisionConfig.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setMonitor(referenceMonitor)
                .setEnabledAtStart(true)
                .build());

        // Second on HDMI-A
        collisionConfig.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.HDMI_A)
                .setMonitor(referenceMonitor)
                .setEnabledAtStart(true)
                .build());

        // Third on a different DisplayPort
        collisionConfig.add(CfVkmsTester.VkmsConnectorSetup.builder()
                .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                .setMonitor(referenceMonitor)
                .setEnabledAtStart(true)
                .build());

        // Initialize VKMS with our collision test configuration
        try {
            mVkmsTester = CfVkmsTester.createWithConfig(getDevice(), collisionConfig);
            assertNotNull("Failed to initialize VKMS for collision test", mVkmsTester);
            mVkmsTester.waitForDisplaysToBeOn(
                collisionConfig.size(), CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);

            // Get all displays with the reference name
            List<String> identicalDisplayIds = getDisplayIdsForName(referenceDisplayName);

            // Log what we found
            CLog.i("Found %d displays with name '%s'", identicalDisplayIds.size(),
                referenceDisplayName);
            for (int i = 0; i < identicalDisplayIds.size(); i++) {
                CLog.i("  Display #%d ID: %s", i + 1, identicalDisplayIds.get(i));
            }

            // Verify that we found the expected number of displays
            assertEquals("Number of identical displays does not match expected count", 3,
                identicalDisplayIds.size());

            // Verify that all display IDs are unique even for identical monitors
            Set<String> uniqueIds = new HashSet<>(identicalDisplayIds);
            assertEquals("Identical monitors should still get unique display IDs",
                identicalDisplayIds.size(), uniqueIds.size());
        } finally {
            if (mVkmsTester != null) {
                mVkmsTester.close();
                mVkmsTester = null;
            }
        }
    }

    /**
     * Test to verify that a display maintains the same display ID when connected
     * to the same port (index) across different configurations.
     */
    @Test
    public void testDisplayIdConsistencyAtSamePort() throws Exception {
        // We'll use the HP Spectre 32 as our reference monitor to track across configurations
        CfVkmsEdidHelper.Monitor referenceMonitor = CfVkmsEdidHelper.DpMonitor.HP_SPECTRE32_4K_DP;
        String referenceDisplayName = "HP Spectre 32";

        // The constant port position for our reference monitor (index 1, which is the second
        // position)
        final int referencePortIndex = 1;

        // Map to store the display ID for each configuration
        Map<String, String> displayIdsByConfig = new HashMap<>();

        // Test multiple configurations
        String configName = "unknown";
        try {
            // Configuration 1: Two displays with reference at port index 1
            configName = "two_displays";
            List<CfVkmsTester.VkmsConnectorSetup> config = new ArrayList<>();

            // First display (port 0)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.EDP)
                    .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                    .setEnabledAtStart(true)
                    .build());

            // Reference display at port 1
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());

            String displayId = testConfigurationAndGetDisplayIdAtPort(
                config, referenceDisplayName, configName, referencePortIndex);
            displayIdsByConfig.put(configName, displayId);

            // Configuration 2: Three displays with reference at port index 1
            configName = "three_displays";
            config = new ArrayList<>();

            // First display (port 0)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                    .setMonitor(CfVkmsEdidHelper.DpMonitor.DEL_61463_DELL_U2410_DP)
                    .setEnabledAtStart(true)
                    .build());

            // Reference display at port 1
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT) // Same type
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());

            // Third display (port 2)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A)
                    .setMonitor(CfVkmsEdidHelper.HdmiMonitor.ACI_9155_ASUS_VH238_HDMI)
                    .setEnabledAtStart(true)
                    .build());

            displayId = testConfigurationAndGetDisplayIdAtPort(
                config, referenceDisplayName, configName, referencePortIndex);
            displayIdsByConfig.put(configName, displayId);

            // Configuration 3: Four displays with reference at port index 1 with different
            // connector type
            configName = "four_displays_different_connector";
            config = new ArrayList<>();

            // First display (port 0)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A)
                    .setMonitor(CfVkmsEdidHelper.HdmiMonitor.HWP_12447_HP_Z24i_HDMI)
                    .setEnabledAtStart(true)
                    .build());

            // Reference display at port 1 (different connector type from previous configs)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.HDMI_A) // Different connector type
                    .setMonitor(referenceMonitor)
                    .setEnabledAtStart(true)
                    .build());

            // Third display (port 2)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.DISPLAY_PORT)
                    .setMonitor(CfVkmsEdidHelper.DpMonitor.ACI_9713_ASUS_VE258_DP)
                    .setEnabledAtStart(true)
                    .build());

            // Fourth display (port 3)
            config.add(CfVkmsTester.VkmsConnectorSetup.builder()
                    .setType(CfVkmsTester.ConnectorType.EDP)
                    .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                    .setEnabledAtStart(true)
                    .build());

            displayId = testConfigurationAndGetDisplayIdAtPort(
                config, referenceDisplayName, configName, referencePortIndex);
            displayIdsByConfig.put(configName, displayId);

        } catch (Exception e) {
            CLog.e("Exception during configuration %s: %s", configName, e.toString());
            throw e;
        }

        // Verify all display IDs for the reference monitor at the same port are the same
        CLog.i("Display IDs by configuration (reference at port %d):", referencePortIndex);
        String referenceId = null;
        for (Map.Entry<String, String> entry : displayIdsByConfig.entrySet()) {
            CLog.i("  %s: %s", entry.getKey(), entry.getValue());
            if (referenceId == null) {
                referenceId = entry.getValue();
            } else {
                assertEquals("Display ID should be consistent at the same port", referenceId,
                    entry.getValue());
            }
        }
    }

    /**
     * Parses the output of "dumpsys SurfaceFlinger --display-id" to extract display information.
     *
     * Example output:
     * Display 0 (HWC display 0): invalid EDID
     * Display 4621520188814754049 (HWC display 1): port=1 pnpId=HWP displayName="HP Spectre 32"
     *
     * @param output The output of the dumpsys command
     * @return A list of DisplayInfo objects containing the parsed information
     */
    private List<DisplayInfo> parseDisplayInfo(String output) {
        List<DisplayInfo> result = new ArrayList<>();

        if (output == null || output.isEmpty()) {
            return result;
        }

        // This pattern matches the display ID line format
        Pattern pattern =
            Pattern.compile("Display (\\d+|\\w+) \\(HWC display (\\d+)\\): (?:port=(\\d+) "
                    + "pnpId=(\\w+).*displayName=\"([^\"]*)\"|.*)",
                Pattern.MULTILINE);

        Matcher matcher = pattern.matcher(output);

        while (matcher.find()) {
            DisplayInfo info = new DisplayInfo();
            info.id = matcher.group(1);
            info.hwcId = matcher.group(2);

            // The port, pnpId, and displayName may not be present for all displays (e.g., invalid
            // EDID)
            if (matcher.groupCount() >= 5 && matcher.group(3) != null) {
                info.port = matcher.group(3);
                info.pnpId = matcher.group(4);
                info.displayName = matcher.group(5);
            }

            result.add(info);
        }

        return result;
    }

    /**
     * Tests a specific display configuration and returns the display ID for a given display name.
     *
     * @param config The VKMS connector configuration to test
     * @param displayName The display name to look for
     * @param configName A name for this configuration (for logging)
     * @return The display ID for the given display name
     * @throws Exception If an error occurs during testing
     */
    private String testConfigurationAndGetDisplayId(List<CfVkmsTester.VkmsConnectorSetup> config,
        String displayName, String configName) throws Exception {
        CLog.i("Testing configuration: %s", configName);

        CfVkmsTester tester = null;
        try {
            tester = CfVkmsTester.createWithConfig(getDevice(), config);
            assertNotNull("Failed to initialize VKMS configuration: " + configName, tester);
            tester.waitForDisplaysToBeOn(config.size(), CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);
            // Get the display ID for our reference monitor
            String displayId = getDisplayIdForName(displayName);
            assertNotNull(
                "Display ID not found for " + displayName + " in config: " + configName, displayId);
            return displayId;
        } finally {
            if (tester != null) {
                tester.close();
            }
        }
    }

    /**
     * Helper method to get the display ID for a display with a specific name.
     *
     * @param displayName The name of the display to find
     * @return The display ID, or null if not found
     * @throws Exception If there's an error executing the command
     */
    private String getDisplayIdForName(String displayName) throws Exception {
        // Run the command to get display IDs
        String command = "dumpsys SurfaceFlinger --display-id";
        CommandResult result = getDevice().executeShellV2Command(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to execute dumpsys command: %s", result.getStderr());
            return null;
        }

        // Parse the output
        List<DisplayInfo> displays = parseDisplayInfo(result.getStdout());

        // Find the display with the matching name
        for (DisplayInfo info : displays) {
            if (info.displayName != null && info.displayName.contains(displayName)) {
                return info.id;
            }
        }

        return null;
    }

    /**
     * Helper method to get all display IDs for displays with a specific name.
     *
     * @param displayName The display name to find
     * @return A list of display IDs for displays with the given name
     * @throws Exception If there's an error executing the command
     */
    private List<String> getDisplayIdsForName(String displayName) throws Exception {
        List<String> displayIds = new ArrayList<>();

        // Run the command to get display IDs
        String command = "dumpsys SurfaceFlinger --display-id";
        CommandResult result = getDevice().executeShellV2Command(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to execute dumpsys command: %s", result.getStderr());
            return displayIds;
        }

        // Parse the output
        List<DisplayInfo> displays = parseDisplayInfo(result.getStdout());

        // Find all displays with the matching name
        for (DisplayInfo info : displays) {
            if (info.displayName != null && info.displayName.contains(displayName)) {
                displayIds.add(info.id);
            }
        }

        return displayIds;
    }

    /**
     * Tests a specific display configuration and returns the display ID for the display at a given
     * port.
     *
     * @param config The VKMS connector configuration to test
     * @param displayName The display name to look for
     * @param configName A name for this configuration (for logging)
     * @param portIndex The port index to check
     * @return The display ID for the display at the given port
     * @throws Exception If an error occurs during testing
     */
    private String testConfigurationAndGetDisplayIdAtPort(
        List<CfVkmsTester.VkmsConnectorSetup> config, String displayName, String configName,
        int portIndex) throws Exception {
        CLog.i("Testing configuration: %s (reference at port %d)", configName, portIndex);

        CfVkmsTester tester = null;
        try {
            tester = CfVkmsTester.createWithConfig(getDevice(), config);
            assertNotNull("Failed to initialize VKMS configuration: " + configName, tester);
            tester.waitForDisplaysToBeOn(config.size(), CfVkmsTester.DISPLAY_BRINGUP_TIMEOUT_MS);

            // Run the command to get display IDs
            String command = "dumpsys SurfaceFlinger --display-id";
            CommandResult result = getDevice().executeShellV2Command(command);
            assertEquals(
                "Failed to execute dumpsys command", CommandStatus.SUCCESS, result.getStatus());

            // Parse the output to extract display IDs
            List<DisplayInfo> displays = parseDisplayInfo(result.getStdout());

            // Log the output for debugging
            CLog.i(
                "Found %d displays in SurfaceFlinger for config %s", displays.size(), configName);
            for (DisplayInfo info : displays) {
                CLog.i("Display: %s", info);
            }

            // Find the display with the matching name and port
            for (DisplayInfo info : displays) {
                if (info.displayName != null && info.displayName.contains(displayName)) {
                    // Verify this is the correct port (HWC IDs generally match port indices + 1)
                    int hwcPortIndex = Integer.parseInt(info.hwcId) - 1;
                    if (hwcPortIndex == portIndex) {
                        return info.id;
                    }
                }
            }

            // If we didn't find the display with matching port, look by name only
            String displayId = getDisplayIdForName(displayName);
            assertNotNull(
                "Display ID not found for " + displayName + " in config: " + configName, displayId);
            return displayId;
        } finally {
            if (tester != null) {
                tester.close();
            }
        }
    }
}
