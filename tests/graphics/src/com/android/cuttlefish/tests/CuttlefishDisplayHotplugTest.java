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
import com.android.tradefed.util.AbiUtils;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.google.auto.value.AutoValue;
import com.google.common.base.Splitter;
import com.google.common.base.Strings;
import com.google.common.collect.Lists;
import com.google.common.collect.MapDifference;
import com.google.common.collect.Maps;
import com.google.common.collect.Range;
import com.google.common.truth.Correspondence;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.junit.After;
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

    private CommandResult runCvdCommand(Collection<String> commandArgs) throws FileNotFoundException {
        String cvdBinary = runner.getHostBinaryPath(CVD_BINARY_BASENAME);

        List<String> fullCommand = new ArrayList<String>(commandArgs);
        fullCommand.add(0, cvdBinary);
        return runner.run(DEFAULT_TIMEOUT_MS, fullCommand.toArray(new String[0]));
    }

    private static final String HELPER_APP_APK = "CuttlefishDisplayHotplugHelperApp.apk";

    private static final String HELPER_APP_PKG = "com.android.cuttlefish.displayhotplughelper";

    private static final String HELPER_APP_ACTIVITY = "com.android.cuttlefish.displayhotplughelper/.DisplayHotplugHelperApp";

    private static final String HELPER_APP_UUID_FLAG = "display_hotplug_uuid";

    private static final int HELPER_APP_LOG_CHECK_ATTEMPTS = 5;

    private static final int HELPER_APP_LOG_CHECK_TIMEOUT_MILLISECONDS = 200;

    private static final Splitter LOGCAT_NEWLINE_SPLITTER = Splitter.on('\n').trimResults();

    @Before
    public void setUp() throws Exception {
        getDevice().uninstallPackage(HELPER_APP_PKG);
        installPackage(HELPER_APP_APK);
    }

    @After
    public void tearDown() throws Exception {
        getDevice().uninstallPackage(HELPER_APP_PKG);
    }

    /**
     * Display information as seen from the host (i.e. from Crosvm via a `cvd display` command).
     */
    @AutoValue
    public static abstract class HostDisplayInfo {
        static HostDisplayInfo create(int id, int width, int height) {
            return new AutoValue_CuttlefishDisplayHotplugTest_HostDisplayInfo(id, width, height);
        }

        abstract int id();
        abstract int width();
        abstract int height();
    }

    /**
     * Display information as seen from the guest (i.e. from SurfaceFlinger/DisplayManager).
     */
    @AutoValue
    public static abstract class GuestDisplayInfo {
        static GuestDisplayInfo create(int id, int width, int height) {
            return new AutoValue_CuttlefishDisplayHotplugTest_GuestDisplayInfo(id, width, height);
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
    private Map<Integer, HostDisplayInfo> parseHostDisplayInfos(String inputJson) {
        if (Strings.isNullOrEmpty(inputJson)) {
            throw new IllegalArgumentException("Null display info json.");
        }

        Map<Integer, HostDisplayInfo> displayInfos = new HashMap<Integer, HostDisplayInfo>();

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

                displayInfos.put(id, HostDisplayInfo.create(id, w, h));
            }
        } catch (JSONException e) {
            throw new IllegalArgumentException("Invalid display info json: " + inputJson, e);
        }

        return displayInfos;
    }


    /**
     * Expected input JSON format:
     *
     *   {
     *     "displays" : [
     *       {
     *           "id": <id>,
     *           "name": <name>,
     *           "width": <width>,
     *           "height": <height>,
     *       },
     *       ...
     *     ]
     *   }
     */
    private Map<Integer, GuestDisplayInfo> parseGuestDisplayInfos(String inputJson) {
        if (Strings.isNullOrEmpty(inputJson)) {
            throw new NullPointerException("Null display info json.");
        }

        Map<Integer, GuestDisplayInfo> displayInfos = new HashMap<Integer, GuestDisplayInfo>();

        try {
            JSONObject json = new JSONObject(inputJson);
            JSONArray jsonDisplays = json.getJSONArray("displays");
            for (int i = 0; i < jsonDisplays.length(); i++) {
                JSONObject jsonDisplay = jsonDisplays.getJSONObject(i);
                int id = jsonDisplay.getInt("id");
                int w = jsonDisplay.getInt("width");
                int h = jsonDisplay.getInt("height");
                displayInfos.put(id, GuestDisplayInfo.create(id, w, h));
            }
        } catch (JSONException e) {
            throw new IllegalArgumentException("Invalid display info json: " + inputJson, e);
        }

        return displayInfos;
    }

    private String getDisplayHotplugHelperAppOutput() throws Exception {
        final String uuid = UUID.randomUUID().toString();

        final Pattern guestDisplayInfoPattern =
            Pattern.compile(
                String.format("^.*DisplayHotplugHelper.*%s.* displays: (\\{.*\\})", uuid));

        getDevice().executeShellCommand(
            String.format("am start -n %s --es %s %s", HELPER_APP_ACTIVITY, HELPER_APP_UUID_FLAG, uuid));

        for (int attempt = 0; attempt < HELPER_APP_LOG_CHECK_ATTEMPTS; attempt++) {
            String logcat = getDevice().executeAdbCommand("logcat", "-d", "DisplayHotplugHelper:E", "*:S");

            List<String> logcatLines = Lists.newArrayList(LOGCAT_NEWLINE_SPLITTER.split(logcat));

            // Inspect latest first:
            Collections.reverse(logcatLines);

            for (String logcatLine : logcatLines) {
                Matcher matcher = guestDisplayInfoPattern.matcher(logcatLine);
                if (matcher.find()) {
                    return matcher.group(1);
                }
            }

            Thread.sleep(HELPER_APP_LOG_CHECK_TIMEOUT_MILLISECONDS);
        }

        throw new IllegalStateException("Failed to find display info from helper app using uuid:" + uuid);
    }

    private Map<Integer, GuestDisplayInfo> getGuestDisplays() throws Exception {
        return parseGuestDisplayInfos(getDisplayHotplugHelperAppOutput());
    }

    public Map<Integer, HostDisplayInfo> getHostDisplays() throws FileNotFoundException {
        CommandResult listDisplaysResult = runCvdCommand(Lists.newArrayList("display", "list"));
        if (!CommandStatus.SUCCESS.equals(listDisplaysResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run list displays command:%s\n%s",
                                  listDisplaysResult.getStdout(),
                                  listDisplaysResult.getStderr()));
        }
        return parseHostDisplayInfos(listDisplaysResult.getStdout());
    }

    @AutoValue
    public static abstract class AddDisplayParams {
        static AddDisplayParams create(int width, int height) {
            return new AutoValue_CuttlefishDisplayHotplugTest_AddDisplayParams(width, height);
        }

        abstract int width();
        abstract int height();
    }

    /* As supported by `cvd display add` */
    private static final int MAX_ADD_DISPLAYS = 4;

    public void addDisplays(List<AddDisplayParams> params) throws FileNotFoundException {
        if (params.size() > MAX_ADD_DISPLAYS) {
            throw new IllegalArgumentException(
                "`cvd display add` only supports adding up to " + MAX_ADD_DISPLAYS +
                " at once but was requested to add " + params.size() + " displays.");
        }

        List<String> addDisplaysCommand = Lists.newArrayList("display", "add");
        for (int i = 0; i < params.size(); i++) {
            AddDisplayParams display = params.get(i);

            addDisplaysCommand.add(String.format(
                "--display%d=width=%d,height=%d", i, display.width(), display.height()));
        }

        CommandResult addDisplayResult = runCvdCommand(addDisplaysCommand);
        if (!CommandStatus.SUCCESS.equals(addDisplayResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run add display command:%s\n%s",
                                  addDisplayResult.getStdout(),
                                  addDisplayResult.getStderr()));
        }
    }

    public void addDisplay(int width, int height) throws FileNotFoundException {
        addDisplays(List.of(AddDisplayParams.create(width, height)));
    }

    public void removeDisplays(List<Integer> displayIds) throws FileNotFoundException {
        List<String> removeDisplaysCommand = Lists.newArrayList("display", "remove");
        for (Integer displayId : displayIds) {
            removeDisplaysCommand.add(displayId.toString());
        }

        CommandResult removeDisplayResult = runCvdCommand(removeDisplaysCommand);
        if (!CommandStatus.SUCCESS.equals(removeDisplayResult.getStatus())) {
            throw new IllegalStateException(
                    String.format("Failed to run remove display command:%s\n%s",
                                  removeDisplayResult.getStdout(),
                                  removeDisplayResult.getStderr()));
        }
    }

    public void removeDisplay(int displayId) throws FileNotFoundException {
        removeDisplays(List.of(displayId));
    }

    Correspondence<GuestDisplayInfo, AddDisplayParams> GUEST_DISPLAY_MATCHES =
        Correspondence.from((GuestDisplayInfo lhs, AddDisplayParams rhs) -> {
            return lhs.width() == rhs.width() &&
                   lhs.height() == rhs.height();
        }, "matches the display info of");

    Correspondence<HostDisplayInfo, AddDisplayParams> HOST_DISPLAY_MATCHES =
        Correspondence.from((HostDisplayInfo lhs, AddDisplayParams rhs) -> {
            return lhs.width() == rhs.width() &&
                   lhs.height() == rhs.height();
        }, "matches the display info of");

    private void doOneConnectAndDisconnectCycle(List<AddDisplayParams> params) throws Exception {
        // Check which displays Crosvm is aware of originally.
        Map<Integer, HostDisplayInfo> originalHostDisplays = getHostDisplays();
        assertThat(originalHostDisplays).isNotNull();
        assertThat(originalHostDisplays).isNotEmpty();

        // Check which displays SurfaceFlinger and DisplayManager are aware of originally.
        Map<Integer, GuestDisplayInfo> originalGuestDisplays = getGuestDisplays();
        assertThat(originalGuestDisplays).isNotNull();
        assertThat(originalGuestDisplays).isNotEmpty();

        // Perform the hotplug connect.
        addDisplays(params);

        // Check that Crosvm is aware of the new display.
        Map<Integer, HostDisplayInfo> afterAddHostDisplays = getHostDisplays();
        assertThat(afterAddHostDisplays).isNotNull();

        MapDifference<Integer, HostDisplayInfo> addedHostDisplaysDiff =
            Maps.difference(afterAddHostDisplays, originalHostDisplays);
        assertThat(addedHostDisplaysDiff.entriesOnlyOnLeft()).hasSize(params.size());
        assertThat(addedHostDisplaysDiff.entriesOnlyOnRight()).isEmpty();

        Map<Integer, HostDisplayInfo> addedHostDisplays =
            addedHostDisplaysDiff.entriesOnlyOnLeft();
        assertThat(addedHostDisplays.values())
            .comparingElementsUsing(HOST_DISPLAY_MATCHES)
            .containsExactlyElementsIn(params);

        // Check that SurfaceFlinger and DisplayManager are aware of the new display.
        Map<Integer, GuestDisplayInfo> afterAddGuestDisplays = getGuestDisplays();
        assertThat(afterAddGuestDisplays).isNotNull();

        MapDifference<Integer, GuestDisplayInfo> addedGuestDisplaysDiff =
            Maps.difference(afterAddGuestDisplays, originalGuestDisplays);
        assertThat(addedGuestDisplaysDiff.entriesOnlyOnLeft()).hasSize(params.size());
        assertThat(addedGuestDisplaysDiff.entriesOnlyOnRight()).isEmpty();

        Map<Integer, GuestDisplayInfo> addedGuestDisplays =
            addedGuestDisplaysDiff.entriesOnlyOnLeft();
        assertThat(addedGuestDisplays.values())
            .comparingElementsUsing(GUEST_DISPLAY_MATCHES)
            .containsExactlyElementsIn(params);

        // Perform the hotplug disconnect.
        List<Integer> addedHostDisplayIds = new ArrayList<Integer>();
        for (HostDisplayInfo addedHostDisplay : addedHostDisplays.values()) {
            addedHostDisplayIds.add(addedHostDisplay.id());
        }
        removeDisplays(addedHostDisplayIds);

        // Check that Crosvm does not show the removed display.
        Map<Integer, HostDisplayInfo> afterRemoveHostDisplays = getHostDisplays();
        assertThat(afterRemoveHostDisplays).isNotNull();

        MapDifference<Integer, HostDisplayInfo> removedHostDisplaysDiff =
            Maps.difference(afterRemoveHostDisplays, originalHostDisplays);
        assertThat(removedHostDisplaysDiff.entriesDiffering()).isEmpty();

        // Check that SurfaceFlinger and DisplayManager do not show the removed display.
        Map<Integer, GuestDisplayInfo> afterRemoveGuestDisplays = getGuestDisplays();
        assertThat(afterRemoveGuestDisplays).isNotNull();

        MapDifference<Integer, GuestDisplayInfo> removedGuestDisplaysDiff =
            Maps.difference(afterRemoveGuestDisplays, originalGuestDisplays);
        assertThat(removedGuestDisplaysDiff.entriesDiffering()).isEmpty();
    }

    @Test
    public void testDisplayHotplug() throws Exception {
        doOneConnectAndDisconnectCycle(
            List.of(AddDisplayParams.create(600, 500)));
    }

    @Test
    public void testDisplayHotplugMultipleDisplays() throws Exception {
        doOneConnectAndDisconnectCycle(
            List.of(
                AddDisplayParams.create(1920, 1080),
                AddDisplayParams.create(1280, 720)));
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
        List<AddDisplayParams> toAdd = List.of(AddDisplayParams.create(600, 500));

        // Warm up to potentially reach any steady state memory usage.
        for (int i = 0; i < 50; i++) {
            doOneConnectAndDisconnectCycle(toAdd);
        }

        MemoryInfo original = getMemoryInfo();
        for (int i = 0; i <= 500; i++) {
            doOneConnectAndDisconnectCycle(toAdd);

            if (i % 100 == 0) {
                doCheckForLeaks(original);
            }
        }
    }
}
