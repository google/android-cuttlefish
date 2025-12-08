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

import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import java.io.Closeable;
import java.io.IOException;
import java.util.EnumSet;
import java.util.List;

/**
 * Manages setup and configuration of Virtual KMS (VKMS) for display emulation
 * through shell commands. Provides an interface for creating and managing
 * displays in CF
 */
public class CfVkmsTester implements Closeable {
    private static final String VKMS_BASE_DIR = "/config/vkms/my-vkms";
    public static final long POLL_INTERVAL_MS = 500;
    public static final long DISPLAY_BRINGUP_TIMEOUT_MS = 10_000;

    // DRM resource types
    private enum DrmResource {
        CONNECTOR("connectors/CON_"),
        CRTC("crtcs/CRTC_"),
        ENCODER("encoders/ENC_"),
        PLANE("planes/PLA_");

        private final String basePath;

        DrmResource(String basePath) {
            this.basePath = basePath;
        }

        public String getBasePath() {
            return basePath;
        }
    }

    /**
     * Connector types as defined in libdrm's drm_mode.h.
     * @see <a
     *     href="https://cs.android.com/android/platform/superproject/main/+/main:external/libdrm/include/drm/drm_mode.h;l=403">drm_mode.h</a>
     */
    public enum ConnectorType {
        UNKNOWN(0),
        VGA(1),
        DISPLAY_PORT(10),
        HDMI_A(11),
        HDMI_B(12),
        EDP(14),
        VIRTUAL(15),
        DSI(16),
        DPI(17),
        WRITEBACK(18);

        private final int value;

        ConnectorType(int value) {
            this.value = value;
        }

        public int getValue() {
            return value;
        }

        /**
         * Converts a string representation to a ConnectorType.
         *
         * @param typeStr String representation of connector type
         * @return The corresponding ConnectorType, or UNKNOWN if not recognized
         */
        public static ConnectorType fromString(String typeStr) {
            if (typeStr == null) {
                return UNKNOWN;
            }

            switch (typeStr.toUpperCase(java.util.Locale.ROOT)) {
                case "DP":
                    return DISPLAY_PORT;
                case "HDMIA":
                    return HDMI_A;
                case "HDMIB":
                    return HDMI_B;
                case "EDP":
                    return EDP;
                case "VGA":
                    return VGA;
                case "DSI":
                    return DSI;
                case "DPI":
                    return DPI;
                case "VIRTUAL":
                    return VIRTUAL;
                case "WRITEBACK":
                    return WRITEBACK;
                default:
                    return UNKNOWN;
            }
        }
    }

    /**
     * https://cs.android.com/android/platform/superproject/main/+/main:external/libdrm/xf86drmMode.h;l=190
     */
    private enum ConnectorStatus {
        CONNECTED(1),
        DISCONNECTED(2),
        UNKNOWN(3);

        private final int value;

        ConnectorStatus(int value) {
            this.value = value;
        }

        public int getValue() {
            return value;
        }
    }

    /**
     * Plane types as defined in libdrm's xf86drmMode.h.
     * @see <a
     *     href="https://cs.android.com/android/platform/superproject/main/+/main:external/libdrm/xf86drmMode.h;l=225">xf86drmMode.h</a>
     */
    private enum PlaneType {
        OVERLAY(0),
        PRIMARY(1),
        CURSOR(2);

        private final int value;

        PlaneType(int value) {
            this.value = value;
        }

        public int getValue() {
            return value;
        }
    }

    private enum PixelFormat {
        NONE("-*"),
        ALL("+*"),
        AR24("+AR24"); // More formats can be added as needed.

        private final String code;

        private PixelFormat(String code) {
            this.code = code;
        }

        public static String toStringFromSet(EnumSet<PixelFormat> formats) {
            if (formats == null || formats.isEmpty()) {
                return NONE.code;
            }

            if (formats.contains(ALL)) {
                return ALL.code;
            }

            // Start by adding NONE to clear any prior formats.
            StringBuilder sb = new StringBuilder(NONE.code);
            for (PixelFormat format : formats) {
                if (format == NONE) {
                    continue;
                }
                sb.append(format.code);
            }
            return sb.toString();
        }
    }

    /**
     * Configuration for a VKMS connector using the builder pattern.
     */
    public static class VkmsConnectorSetup {
        private ConnectorType type;
        private boolean enabledAtStart;
        private int additionalOverlayPlanes;
        private CfVkmsEdidHelper.Monitor monitor;

        public static Builder builder() {
            return new Builder();
        }

        public static class Builder {
            private ConnectorType type = ConnectorType.DISPLAY_PORT;
            private boolean enabledAtStart = true;
            private int additionalOverlayPlanes = 0;
            private CfVkmsEdidHelper.Monitor monitor = null;

            /**
             * Sets the connector type.
             *
             * @param type The connector type
             */
            public Builder setType(ConnectorType type) {
                this.type = type;
                return this;
            }

            /**
             * Sets whether the connector is initially enabled.
             *
             * @param enabled True if the connector should be enabled at startup
             */
            public Builder setEnabledAtStart(boolean enabled) {
                this.enabledAtStart = enabled;
                return this;
            }

            /**
             * Sets the number of additional overlay planes.
             *
             * @param count Number of additional overlay planes
             */
            public Builder setAdditionalOverlayPlanes(int count) {
                if (count < 0) {
                    throw new IllegalArgumentException("Overlay plane count must be non-negative");
                }
                this.additionalOverlayPlanes = count;
                return this;
            }

            /**
             * Sets the monitor (defines EDID).
             *
             * @param monitor The monitor to use its EDID for this connector
             */
            public Builder setMonitor(CfVkmsEdidHelper.Monitor monitor) {
                this.monitor = monitor;
                return this;
            }

            public VkmsConnectorSetup build() {
                VkmsConnectorSetup setup = new VkmsConnectorSetup();
                setup.type = this.type;
                setup.enabledAtStart = this.enabledAtStart;
                setup.additionalOverlayPlanes = this.additionalOverlayPlanes;
                setup.monitor = this.monitor;
                return setup;
            }
        }

        // Private constructor - use builder
        private VkmsConnectorSetup() {}

        public ConnectorType getType() {
            return type;
        }

        public boolean isEnabledAtStart() {
            return enabledAtStart;
        }

        public int getAdditionalOverlayPlanes() {
            return additionalOverlayPlanes;
        }

        public CfVkmsEdidHelper.Monitor getMonitor() {
            return monitor;
        }
    }

    private final ITestDevice device; // Used to execute shell commands
    private int latestPlaneId = 0;
    private boolean initialized = false;

    /**
     * Creates a VKMS configuration with a specified number of virtual displays,
     * each with a default setup.
     *
     * @param device The test device to run commands on
     * @param displaysCount The number of virtual displays to configure
     * @return A new instance of CfVkmsTester, or null if creation failed
     */
    public static CfVkmsTester createWithGenericConnectors(ITestDevice device, int displaysCount) {
        if (displaysCount < 0) {
            CLog.e("Invalid number of displays: %d. At least one connector must be specified.",
                displaysCount);
            return null;
        }

        CfVkmsTester tester = new CfVkmsTester(device, displaysCount);

        if (!tester.initialized) {
            CLog.e("Failed to initialize CfVkmsTester with Generic Connectors");
            return null;
        }

        return tester;
    }

    /**
     * Creates a VKMS configuration based on a provided list of VkmsConnectorSetup.
     *
     * @param device The test device to run commands on
     * @param config A list of VkmsConnectorSetup objects defining the displays
     * @return A new instance of CfVkmsTester, or null if creation failed
     */
    public static CfVkmsTester createWithConfig(
        ITestDevice device, List<VkmsConnectorSetup> config) {
        if (config == null || config.isEmpty()) {
            CLog.e("Empty configuration provided. At least one connector must be specified.");
            return null;
        }

        CfVkmsTester tester = new CfVkmsTester(device, config.size(), config);

        if (!tester.initialized) {
            CLog.e("Failed to initialize CfVkmsTester with Config");
            return null;
        }

        return tester;
    }

    /**
     * Private constructor to initialize VKMS configuration.
     */
    private CfVkmsTester(ITestDevice device, int displaysCount) {
        this(device, displaysCount, null);
    }

    /**
     * Private constructor with explicit configuration.
     */
    private CfVkmsTester(
        ITestDevice device, int displaysCount, List<VkmsConnectorSetup> explicitConfig) {
        this.device = device;
        boolean success = false;
        try {
            success = toggleSystemUi(false) && toggleVkmsAsDisplayDriver(true)
                && setupDisplayConnectors(displaysCount, explicitConfig) && toggleVkms(true)
                && toggleSystemUi(true);
        } catch (Exception e) {
            CLog.e("Failed to set up VKMS: %s", e.toString());
        }

        if (!success) {
            CLog.e("Failed to set up VKMS");
            try {
                shutdownAndCleanUpVkms();
            } catch (Exception e) {
                CLog.e("Error during cleanup: %s", e.toString());
            }
            return;
        }

        initialized = true;
    }

    public boolean toggleSystemUi(boolean enable) throws Exception {
        if (enable) {
            if (executeCommand("start vendor.hwcomposer-3").getStatus() != CommandStatus.SUCCESS) {
                CLog.e("Failed to start vendor.hwcomposer-3 service");
                return false;
            }
            if (executeCommand("start").getStatus() != CommandStatus.SUCCESS) {
                CLog.e("Failed to start zygote");
                return false;
            }
        } else {
            if (executeCommand("stop").getStatus() != CommandStatus.SUCCESS) {
                CLog.w("Failed to stop zygote. This may be expected if it was already stopped.");
            }
            if (executeCommand("stop vendor.hwcomposer-3").getStatus() != CommandStatus.SUCCESS) {
                CLog.w("Failed to stop vendor.hwcomposer-3. This may be expected if it was already "
                    + "stopped.");
            }
        }

        CLog.i("Successfully %s UI service", enable ? "started" : "stopped");
        return true;
    }

    private boolean toggleVkms(boolean enable) throws Exception {
        String path = VKMS_BASE_DIR + "/enabled";
        String value = enable ? "1" : "0";
        String command = "echo " + value + " > " + path;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to toggle VKMS: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully toggled VKMS to %s", enable ? "enabled" : "disabled");
        return true;
    }

    private boolean toggleVkmsAsDisplayDriver(boolean enable) throws Exception {
        String command =
            "setprop vendor.hwc.drm.device " + (enable ? "/dev/dri/card1" : "/dev/dri/card0");
        CommandResult result = executeCommand(command);

        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set vendor.hwc.drm.device property: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set vendor.hwc.drm.device property");
        // On Disabling VKMS, we don't need to do anything else.
        if (!enable)
            return true;

        // Create VKMS directory if we're enabling VKMS.
        command = "mkdir " + VKMS_BASE_DIR;
        result = executeCommand(command);

        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to create VKMS directory: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully created directory %s", VKMS_BASE_DIR);
        return true;
    }

    private boolean setupDisplayConnectors(
        int displaysCount, List<VkmsConnectorSetup> explicitConfig) throws Exception {
        boolean isExplicitConfig = explicitConfig != null && !explicitConfig.isEmpty();
        if (isExplicitConfig && displaysCount != explicitConfig.size()) {
            CLog.e("Mismatch between requested displays count and explicit config size");
            return false;
        }

        for (int i = 0; i < displaysCount; i++) {
            createResource(DrmResource.CRTC, i);
            setCrtcWriteback(i, true);

            createResource(DrmResource.ENCODER, i);
            linkToCrtc(DrmResource.ENCODER, i, i);

            createResource(DrmResource.CONNECTOR, i);
            // Configure connector based on explicit config or defaults
            VkmsConnectorSetup config = null;
            if (isExplicitConfig) {
                config = explicitConfig.get(i);
                setConnectorStatus(i, config.isEnabledAtStart());
                setConnectorType(i, config.getType());
                if (config.getMonitor() != null) {
                    setConnectorEdid(i, config.getMonitor());
                }
            } else {
                setConnectorStatus(i, false); // Default to disconnected
                setConnectorType(i, i == 0 ? ConnectorType.EDP : ConnectorType.DISPLAY_PORT);
            }

            linkConnectorToEncoder(i, i);

            // Create planes for each connector
            int additionalOverlays = isExplicitConfig ? config.getAdditionalOverlayPlanes() : 0;
            setupPlanesForConnector(i, additionalOverlays);

            CLog.i("Successfully set up display %d", i);
        }

        return true;
    }

    /**
     * Creates a DRM resource directory.
     *
     * @param resource The type of resource to create
     * @param index The index of the resource
     * @return true if successful, false otherwise
     * @throws Exception If an error occurs during directory creation
     */
    private boolean createResource(DrmResource resource, int index) throws Exception {
        String resourceDir = VKMS_BASE_DIR + "/" + resource.getBasePath() + index;
        String command = "mkdir " + resourceDir;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to create directory %s: %s", resourceDir, result.getStderr());
            return false;
        }

        CLog.i("Successfully created directory %s", resourceDir);
        return true;
    }

    private void setupPlanesForConnector(int connectorIndex, int additionalOverlays)
        throws Exception {
        // Basic planes: cursor (0) and primary (1)
        for (int j = 0; j < 2 + additionalOverlays; j++) {
            createResource(DrmResource.PLANE, latestPlaneId);

            // Set plane type
            PlaneType type;
            EnumSet<PixelFormat> formats;
            switch (j) {
                case 0:
                    type = PlaneType.CURSOR;
                    formats = EnumSet.of(PixelFormat.AR24);
                    break;
                case 1:
                    type = PlaneType.PRIMARY;
                    formats = EnumSet.of(PixelFormat.ALL);
                    break;
                default:
                    type = PlaneType.OVERLAY;
                    formats = EnumSet.of(PixelFormat.ALL);
                    break;
            }

            setPlaneType(latestPlaneId, type);
            setPlaneFormat(latestPlaneId, formats);
            linkToCrtc(DrmResource.PLANE, latestPlaneId, connectorIndex);

            latestPlaneId++;
        }
    }

    private boolean setCrtcWriteback(int index, boolean enable) throws Exception {
        String crtcDir = VKMS_BASE_DIR + "/" + DrmResource.CRTC.getBasePath() + index;
        String writebackPath = crtcDir + "/writeback";
        String value = enable ? "1" : "0";
        String command = "echo " + value + " > " + writebackPath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set crtc writeback: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set crtc %d writeback to %s", index, enable ? "enabled" : "disabled");
        return true;
    }

    private boolean setConnectorStatus(int index, boolean enable) throws Exception {
        String connectorDir = VKMS_BASE_DIR + "/" + DrmResource.CONNECTOR.getBasePath() + index;
        String statusPath = connectorDir + "/status";
        ConnectorStatus status = enable ? ConnectorStatus.CONNECTED : ConnectorStatus.DISCONNECTED;
        String command = "echo " + status.getValue() + " > " + statusPath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set connector status: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set connector %d status to %s", index,
            enable ? "connected" : "disconnected");
        return true;
    }

    private boolean setConnectorType(int index, ConnectorType type) throws Exception {
        String connectorDir = VKMS_BASE_DIR + "/" + DrmResource.CONNECTOR.getBasePath() + index;
        String typePath = connectorDir + "/type";
        String command = "echo " + type.getValue() + " > " + typePath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set connector type: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set connector %d type to %d", index, type.getValue());
        return true;
    }

    private boolean setConnectorEdid(int index, CfVkmsEdidHelper.Monitor monitor) throws Exception {
        if (monitor == null) {
            CLog.e("Monitor is null for connector %d", index);
            return false;
        }

        String connectorDir = VKMS_BASE_DIR + "/" + DrmResource.CONNECTOR.getBasePath() + index;
        String edidPath = connectorDir + "/edid";

        // Get the formatted EDID data from the helper
        String edidHexEscaped = CfVkmsEdidHelper.getEdidForPrintf(monitor);
        if (edidHexEscaped == null || edidHexEscaped.isEmpty()) {
            CLog.e("Failed to get formatted EDID data for connector %d", index);
            return false;
        }

        // Create the command to write EDID data
        String command = String.format("printf \"%s\" > %s", edidHexEscaped, edidPath);

        // Execute the command
        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to write EDID data: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully wrote EDID data to connector %d", index);
        return true;
    }

    private boolean setPlaneType(int index, PlaneType type) throws Exception {
        String planeDir = VKMS_BASE_DIR + "/" + DrmResource.PLANE.getBasePath() + index;
        String typePath = planeDir + "/type";
        String command = "echo " + type.getValue() + " > " + typePath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set plane type: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set plane %d type to %d", index, type.getValue());
        return true;
    }

    private boolean setPlaneFormat(int index, EnumSet<PixelFormat> formats) throws Exception {
        String planeDir = VKMS_BASE_DIR + "/" + DrmResource.PLANE.getBasePath() + index;
        String formatPath = planeDir + "/supported_formats";
        String command = "echo " + PixelFormat.toStringFromSet(formats) + " > " + formatPath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to set plane format: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully set plane %d format", index);
        return true;
    }

    private boolean linkToCrtc(DrmResource resource, int resourceIdx, int crtcIdx)
        throws Exception {
        String crtcName = DrmResource.CRTC.getBasePath() + crtcIdx;
        String resourceDir = VKMS_BASE_DIR + "/" + resource.getBasePath() + resourceIdx;
        String possibleCrtcPath = resourceDir + "/possible_crtcs";
        String crtcDir = VKMS_BASE_DIR + "/" + crtcName;

        String command = "ln -s " + crtcDir + " " + possibleCrtcPath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            String err = result.getStderr();
            CLog.e("Failed to link to CRTC: %s", err);
            return false;
        }

        CLog.i("Successfully linked %s to %s", possibleCrtcPath, crtcDir);
        return true;
    }

    private boolean linkConnectorToEncoder(int connectorIdx, int encoderIdx) throws Exception {
        String encoderName = DrmResource.ENCODER.getBasePath() + encoderIdx;
        String connectorDir =
            VKMS_BASE_DIR + "/" + DrmResource.CONNECTOR.getBasePath() + connectorIdx;
        String possibleEncoderPath = connectorDir + "/possible_encoders";
        String encoderDir = VKMS_BASE_DIR + "/" + encoderName;

        String command = "ln -s " + encoderDir + " " + possibleEncoderPath;

        CommandResult result = executeCommand(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to link connector to encoder: %s", result.getStderr());
            return false;
        }

        CLog.i("Successfully linked %s to %s", possibleEncoderPath, encoderDir);
        return true;
    }

    private void shutdownAndCleanUpVkms() throws Exception {
        toggleSystemUi(false);
        toggleVkms(false);

        // Remove all links first (possible_crtcs and possible_encoders)
        device.executeShellCommand("rm -f " + VKMS_BASE_DIR + "/planes/*/possible_crtcs/*");
        device.executeShellCommand("rm -f " + VKMS_BASE_DIR + "/encoders/*/possible_crtcs/*");
        device.executeShellCommand("rm -f " + VKMS_BASE_DIR + "/connectors/*/possible_encoders/*");

        // Remove resource directories in order
        device.executeShellCommand("rmdir " + VKMS_BASE_DIR + "/planes/*");
        device.executeShellCommand("rmdir " + VKMS_BASE_DIR + "/crtcs/*");
        device.executeShellCommand("rmdir " + VKMS_BASE_DIR + "/encoders/*");
        device.executeShellCommand("rmdir " + VKMS_BASE_DIR + "/connectors/*");

        // Remove the base directory
        device.executeShellCommand("rmdir " + VKMS_BASE_DIR);

        toggleVkmsAsDisplayDriver(false);
        CLog.i("VKMS cleanup completed");
    }

    private CommandResult executeCommand(String command) throws Exception {
        CommandResult result = null;
        long startTime = System.currentTimeMillis();
        long maxDurationMs = 500;

        while (System.currentTimeMillis() - startTime < maxDurationMs) {
            result = device.executeShellV2Command(command);
            if (result.getStatus() == CommandStatus.SUCCESS) {
                return result;
            }
        }
        CLog.w("Command '%s' failed after %dms", command, maxDurationMs);
        return result;
    }

    /**
     * Implements the close method required by Closeable interface.
     * Cleans up VKMS resources when the tester is closed.
     */
    @Override
    public void close() throws IOException {
        try {
            shutdownAndCleanUpVkms();
        } catch (Exception e) {
            throw new IOException("Failed to clean up VKMS: " + e.getMessage(), e);
        }
    }

    /**
     * Helper method to wait for displays to be online by periodically checking SurfaceFlinger.
     *
     * @param minimumExpectedDisplays The minimum number of displays expected to be detected
     * @param waitTimeoutMs The maximum time to wait in milliseconds
     * @throws Exception If displays are not detected in time or a command fails
     */
    public void waitForDisplaysToBeOn(int minimumExpectedDisplays, long waitTimeoutMs)
        throws Exception {
        long startTime = System.currentTimeMillis();
        int displayCount = 0;
        while (displayCount < minimumExpectedDisplays
            && System.currentTimeMillis() - startTime < waitTimeoutMs) {
            String command = "dumpsys SurfaceFlinger --displays | grep -c '^Display '";
            CommandResult result = device.executeShellV2Command(command);
            if (result.getStatus() == CommandStatus.SUCCESS) {
                try {
                    displayCount = Integer.parseInt(result.getStdout().trim());
                } catch (NumberFormatException e) {
                    CLog.w("Could not parse display count from dumpsys: %s", result.getStdout());
                    displayCount = 0;
                }
            } else {
                CLog.d("dumpsys SurfaceFlinger failed, UI likely not ready yet. Retrying...");
            }

            // Wait a poll interval
            long pollStartTime = System.currentTimeMillis();
            while (System.currentTimeMillis() - pollStartTime < POLL_INTERVAL_MS) {}
        }
        if (displayCount < minimumExpectedDisplays) {
            throw new Exception("Displays were not detected in time. Expected at least "
                + minimumExpectedDisplays + ", found " + displayCount);
        }
    }
}
