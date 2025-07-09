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
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.After;
import org.junit.Before;
import org.junit.runner.RunWith;
import org.junit.Test;

@RunWith(DeviceJUnit4ClassRunner.class)
public class CfVkmsCursorTest extends BaseHostJUnit4Test {
    private static final String CURSOR_FLAG =
            "com.android.graphics.libgui.flags.cursor_plane_compatibility";
    private static final long UI_STARTUP_TIMEOUT_MS = 10 * 1000;
    private static final String DUMPSYS_COMMAND = "dumpsys SurfaceFlinger";
    private static final Pattern NO_STATS_PATTERN = Pattern.compile("No stats yet");
    private static final Pattern SUCCESS_PATTERN =
            Pattern.compile("Cursor plane frames: (\\d+)", Pattern.MULTILINE);
    private static final Pattern FAILURE_PATTERN =
            Pattern.compile("Failed cursor test commit frames: (\\d+)", Pattern.MULTILINE);

    private static class CursorStats {
        public int cursorFrames = 0;
        public int failedCursorFrames = 0;
    }

    private CfVkmsTester mVkmsTester;

    private boolean toggleFlag(String flag, boolean enable) throws Exception {
        String command = "aflags " + (enable ? "enable " : "disable ") + flag + " --immediate";
        CommandResult result = getDevice().executeShellV2Command(command);
        if (result.getStatus() != CommandStatus.SUCCESS) {
            CLog.e("Failed to %s flag: %s", enable ? "enable" : "disable", result.getStderr());
            return false;
        }

        CLog.i("Successfully %s flag", enable ? "enabled" : "disabled");
        return true;
    }

    @Before
    public void setUp() throws Exception {
        List<CfVkmsTester.VkmsConnectorSetup> connectorConfigs = List.of(
                CfVkmsTester.VkmsConnectorSetup.builder()
                        .setType(CfVkmsTester.ConnectorType.EDP)
                        .setMonitor(CfVkmsEdidHelper.EdpDisplay.REDRIX)
                        .setEnabledAtStart(true)
                        .build());

        mVkmsTester = CfVkmsTester.createWithConfig(getDevice(), connectorConfigs);
        assertNotNull("Failed to initialize VKMS tester", mVkmsTester);
    }

    @After
    public void tearDown() throws Exception {
        if (mVkmsTester != null) {
            mVkmsTester.close();
            mVkmsTester = null;
        }
    }

    @Test
    public void cursorCompositionSucceeds_withCursorFlagEnabled() throws Exception {
        assertTrue(toggleFlag(CURSOR_FLAG, true));
        assertTrue(mVkmsTester.toggleSystemUi(false));
        assertTrue(mVkmsTester.toggleSystemUi(true));

        // Wait for displays to be detected. UI might take some time to turn on.
        long startTime = System.currentTimeMillis();
        while (System.currentTimeMillis() - startTime < UI_STARTUP_TIMEOUT_MS) {}

        CursorStats results = testCursorComposition();
        if (results.cursorFrames > 0) {
            return;
        } else if (results.failedCursorFrames > 0) {
            fail("All cursor frames failed during test commit");
        } else {
            fail("No attempted cursor frames detected");
        }
    }

    @Test
    public void cursorCompositionNotAttempted_withCursorFlagDisabled() throws Exception {
        assertTrue(toggleFlag(CURSOR_FLAG, false));
        assertTrue(mVkmsTester.toggleSystemUi(false));
        assertTrue(mVkmsTester.toggleSystemUi(true));

        // Wait for displays to be detected. UI might take some time to turn on.
        long startTime = System.currentTimeMillis();
        while (System.currentTimeMillis() - startTime < UI_STARTUP_TIMEOUT_MS) {}

        CursorStats results = testCursorComposition();
        assertEquals("Detected successful cursor frames when there should be none", 0,
                results.cursorFrames);
        assertEquals("Detected failed cursor frames when there should be none", 0,
                results.failedCursorFrames);
    }

    private CursorStats testCursorComposition() throws Exception {
        CommandResult result = getDevice().executeShellV2Command(DUMPSYS_COMMAND);
        assertEquals(
                "Failed to execute dumpsys command", CommandStatus.SUCCESS, result.getStatus());
        String output = result.getStdout();

        assertTrue("Dumpsys command failed to return output", output != null && !output.isEmpty());

        Matcher noStatsMatcher = NO_STATS_PATTERN.matcher(output);
        assertFalse("No stats yet", noStatsMatcher.find());

        CursorStats results = new CursorStats();
        Matcher successMatcher = SUCCESS_PATTERN.matcher(output);
        int successMatchCount = 0;
        while (successMatcher.find()) {
            successMatchCount++;

            // There should be 2 matches per display. Odd-count matches are cumulative, while
            // even-count matches are deltas.
            if (successMatchCount % 2 == 0) {
                continue;
            }

            results.cursorFrames += Integer.parseInt(successMatcher.group(1));
        }

        Matcher failureMatcher = FAILURE_PATTERN.matcher(output);
        int failedMatchCount = 0;
        while (failureMatcher.find()) {
            failedMatchCount++;

            // There should be 2 matches per display. Odd-count matches are cumulative, while
            // even-count matches are deltas.
            if (failedMatchCount % 2 == 0) {
                continue;
            }

            results.failedCursorFrames += Integer.parseInt(failureMatcher.group(1));
        }

        assertTrue("No cursor frame stats matched in dumpsys output", successMatchCount > 0);
        assertTrue("No failed cursor frame stats matched in dumpsys output", failedMatchCount > 0);

        return results;
    }
}
