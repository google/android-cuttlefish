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
package com.android.cuttlefish.tests;

import static com.google.common.truth.Truth.assertThat;

import android.platform.test.annotations.LargeTest;

import com.android.cuttlefish.tests.utils.CuttlefishHostTest;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.google.auto.value.AutoValue;
import com.google.common.base.Strings;
import com.google.common.collect.MapDifference;
import com.google.common.collect.Maps;
import com.google.common.collect.Range;

import java.io.BufferedReader;
import java.io.FileNotFoundException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests that a Cuttlefish device can interactively connect and disconnect displays.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishDisplayHotplugTest extends CuttlefishHostTest {

    private static final long DEFAULT_TIMEOUT_MS = 5000;

    private static final String CVD_BINARY_BASENAME = "cvd";

    private CommandResult runCvdCommand(String... command) throws FileNotFoundException {
        String cvdBinary = runner.getHostBinaryPath(CVD_BINARY_BASENAME);

        ArrayList<String> fullCommand = new ArrayList<String>(Arrays.asList(command));
        fullCommand.add(0, cvdBinary);
        return runner.run(DEFAULT_TIMEOUT_MS, fullCommand.toArray(new String[0]));
    }

    @AutoValue
    public static abstract class DisplayInfo {
        static DisplayInfo create(int id, int width, int height) {
            return new AutoValue_CuttlefishDisplayHotplugTest_DisplayInfo(id, width, height);
        }

        abstract int id();
        abstract int width();
        abstract int height();
    }

    /**
     * Expected input JSON format:
     *
     *   {
     *     "displays" : {
     *       "<display id>": {
     *         "mode": {
     *           "windowed": [
     *             <width>,
     *             <height>,
     *           ],
     *         },
     *         ...
     *       },
     *       ...
     *     }
     *   }
     *
     */
    private Map<Integer, DisplayInfo> parseDisplayInfos(String inputJson) {
        if (Strings.isNullOrEmpty(inputJson)) {
            throw new IllegalArgumentException("Null display info json.");
        }

        Map<Integer, DisplayInfo> displayInfos = new HashMap<Integer, DisplayInfo>();

        try {
            JSONObject json = new JSONObject(inputJson);
            JSONObject jsonDisplays = json.getJSONObject("displays");
            for (Iterator<String> keyIt = jsonDisplays.keys(); keyIt.hasNext(); ) {
                String displayNumberString = keyIt.next();

                JSONObject jsonDisplay = jsonDisplays.getJSONObject(displayNumberString);
                JSONObject jsonDisplayMode = jsonDisplay.getJSONObject("mode");
                JSONArray jsonDisplayModeWindowed = jsonDisplayMode.getJSONArray("windowed");

                int id = Integer.parseInt(displayNumberString);
                int w = jsonDisplayModeWindowed.getInt(0);
                int h = jsonDisplayModeWindowed.getInt(1);

                displayInfos.put(id, DisplayInfo.create(id, w, h));
            }
        } catch (JSONException e) {
            throw new IllegalArgumentException("Invalid display info json: " + inputJson, e);
        }

        return displayInfos;
    }

    public Map<Integer, DisplayInfo> getDisplays() throws FileNotFoundException {
        CommandResult listDisplaysResult =
            runCvdCommand("display",
                          "list");
        if (!CommandStatus.SUCCESS.equals(listDisplaysResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run list displays command:%s\n%s",
                                  listDisplaysResult.getStdout(),
                                  listDisplaysResult.getStderr()));
        }
        return parseDisplayInfos(listDisplaysResult.getStdout());
    }

    public void addDisplay(int width, int height) throws FileNotFoundException {
        CommandResult addDisplayResult =
            runCvdCommand("display",
                          "add",
                          String.format("--width=%d", width),
                          String.format("--height=%d", height));

        if (!CommandStatus.SUCCESS.equals(addDisplayResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run add display command:%s\n%s",
                                  addDisplayResult.getStdout(),
                                  addDisplayResult.getStderr()));
        }
    }

    public void removeDisplay(int displayId) throws FileNotFoundException {
        CommandResult removeDisplayResult =
            runCvdCommand("display",
                          "remove",
                          String.valueOf(displayId));
        if (!CommandStatus.SUCCESS.equals(removeDisplayResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run remove display command:%s\n%s",
                                  removeDisplayResult.getStdout(),
                                  removeDisplayResult.getStderr()));
        }
    }

    private void doOneConnectAndDisconnectCycle() throws Exception {
        Map<Integer, DisplayInfo> originalDisplays = getDisplays();
        assertThat(originalDisplays).isNotNull();
        assertThat(originalDisplays).isNotEmpty();

        addDisplay(600, 500);

        Map<Integer, DisplayInfo> addedDisplays = getDisplays();
        assertThat(addedDisplays).isNotNull();

        MapDifference<Integer, DisplayInfo> addedDisplaysDiff =
            Maps.difference(addedDisplays, originalDisplays);
        assertThat(addedDisplaysDiff.entriesOnlyOnLeft()).hasSize(1);
        assertThat(addedDisplaysDiff.entriesOnlyOnRight()).isEmpty();

        DisplayInfo addedDisplay = addedDisplaysDiff.entriesOnlyOnLeft().values().iterator().next();
        assertThat(addedDisplay.width()).isEqualTo(600);
        assertThat(addedDisplay.height()).isEqualTo(500);

        removeDisplay(addedDisplay.id());

        Map<Integer, DisplayInfo> removedDisplays = getDisplays();
        assertThat(removedDisplays).isNotNull();

        MapDifference<Integer, DisplayInfo> removedDisplaysDiff =
            Maps.difference(removedDisplays, originalDisplays);
        assertThat(removedDisplaysDiff.entriesOnlyOnLeft()).isEmpty();
        assertThat(removedDisplaysDiff.entriesOnlyOnRight()).isEmpty();
    }

    @Test
    public void testDisplayHotplug() throws Exception {
        doOneConnectAndDisconnectCycle();
    }

    @AutoValue
    public static abstract class MemoryInfo {
        static MemoryInfo create(int usedRam) {
            return new AutoValue_CuttlefishDisplayHotplugTest_MemoryInfo(usedRam);
        }

        abstract int usedRamBytes();
    }

    private static final String GET_USED_RAM_COMMAND = "dumpsys meminfo";

    private static final Pattern USED_RAM_PATTERN = Pattern.compile("Used RAM: (.*?)K \\(");

    private MemoryInfo getMemoryInfo() throws Exception {
        ITestDevice device = getDevice();

        CommandResult getUsedRamResult = device.executeShellV2Command(GET_USED_RAM_COMMAND);
        if (!CommandStatus.SUCCESS.equals(getUsedRamResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run |%s|: stdout: %s\n stderr: %s",
                                  GET_USED_RAM_COMMAND,
                                  getUsedRamResult.getStdout(),
                                  getUsedRamResult.getStderr()));
        }
        // Ex:
        //    ...
        //    GPU:              0K (        0K dmabuf +         0K private)
        //    Used RAM: 1,155,524K (  870,488K used pss +   285,036K kernel)
        //    Lost RAM:    59,469K
        //    ...
        String usedRamString = getUsedRamResult.getStdout();
        Matcher m = USED_RAM_PATTERN.matcher(usedRamString);
        if (!m.find()) {
            throw new IllegalStateException(
                     String.format("Failed to parse 'Used RAM' from stdout:\n%s",
                                   getUsedRamResult.getStdout()));
        }
        // Ex: "1,228,768"
        usedRamString = m.group(1);
        usedRamString = usedRamString.replaceAll(",", "");
        int usedRam = Integer.parseInt(usedRamString) * 1000;

        return MemoryInfo.create(usedRam);
    }

    private static final int MAX_ALLOWED_RAM_BYTES_DIFF = 32 * 1024 * 1024;

    private void doCheckForLeaks(MemoryInfo base) throws Exception {
        MemoryInfo current = getMemoryInfo();

        assertThat(current.usedRamBytes()).isIn(
                Range.closed(base.usedRamBytes() - MAX_ALLOWED_RAM_BYTES_DIFF,
                             base.usedRamBytes() + MAX_ALLOWED_RAM_BYTES_DIFF));
    }

    @Test
    @LargeTest
    public void testDisplayHotplugDoesNotLeakMemory() throws Exception {
        // Warm up to potentially reach any steady state memory usage.
        for (int i = 0; i < 50; i++) {
            doOneConnectAndDisconnectCycle();
        }

        MemoryInfo original = getMemoryInfo();
        for (int i = 0; i <= 500; i++) {
            doOneConnectAndDisconnectCycle();

            if (i % 100 == 0) {
                doCheckForLeaks(original);
            }
        }
    }
}
