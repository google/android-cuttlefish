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

import com.android.cuttlefish.tests.utils.CuttlefishHostTest;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.google.common.base.Splitter;

import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.stream.Collectors;
import java.util.List;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class WmediumdControlE2eTest extends CuttlefishHostTest {

    private static final String WMEDIUMD_BINARY_BASENAME = "wmediumd_control";

    private static final String WMEDIUMD_SERVER_BASENAME = "internal/wmediumd_api_server";

    private static final Splitter NEWLINE_SPLITTER = Splitter.on('\n');

    private static final Splitter TAB_SPLITTER = Splitter.on('\t');

    private static final Splitter SPACE_SPLITTER = Splitter.on(' ');

    private ITestDevice testDevice;

    private int getRSSI() throws Exception {
        CommandResult wifiScanCommandResult = testDevice.executeShellV2Command("cmd wifi status");
        Assert.assertEquals(CommandStatus.SUCCESS, wifiScanCommandResult.getStatus());

        String[] parsedResult = wifiScanCommandResult.getStdout().split(",");
        for (String chunk : parsedResult) {
            if (chunk.contains("RSSI:")) {
                String[] parsedChunk = chunk.trim().split(" ");
                Assert.assertEquals(parsedChunk.length, 2);
                return Integer.parseInt(parsedChunk[1]);
            }
        }
        return 0;
    }

    private String getStationMacAddress(List<StationInfo> stationInfoList) {
        List<String> stationMacAddressList = stationInfoList.stream().map(x -> x.macAddress).filter(addr -> addr.substring(0, 6).equals("02:15:")).collect(Collectors.toList());
        Assert.assertTrue(stationMacAddressList.size() > 0);
        return stationMacAddressList.get(0);
    }

    private String getApMacAddress(List<StationInfo> stationInfoList) {
        List<String> apMacAddressList = stationInfoList.stream().map(x -> x.macAddress).filter(addr -> addr.substring(0, 6).equals("42:00:")).collect(Collectors.toList());
        Assert.assertTrue(apMacAddressList.size() > 0);
        return apMacAddressList.get(0);
    }

    private CommandResult runWmediumdCommand(long timeout, String... command) throws FileNotFoundException {
        String wmediumdBinary;
        String wmediumdServer;

        Assert.assertNotNull(runner);

        wmediumdBinary = runner.getHostBinaryPath(WMEDIUMD_BINARY_BASENAME);
        wmediumdServer = runner.getHostRuntimePath(WMEDIUMD_SERVER_BASENAME);

        ArrayList<String> fullCommand = new ArrayList<String>(Arrays.asList(command));
        fullCommand.add(0, wmediumdBinary);
        fullCommand.add(1, String.format("--wmediumd_api_server=%s", wmediumdServer));

        return runner.run(timeout, fullCommand.toArray(new String[0]));
    }

    /** One line for "Total Stations" and one line for the "tsv header". */
    private static final int NUMBER_OF_NONEMPTY_INFO_LINES = 2;

    public List<StationInfo> listStations() throws Exception {
        CommandResult result = runWmediumdCommand(10000, "list_stations");
        CLog.i("stdout:%s", result.getStdout());
        CLog.i("stderr:%s", result.getStderr());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());

        List<String> lines = NEWLINE_SPLITTER.omitEmptyStrings().splitToList(result.getStdout());
        List<String> parsedTotalStationsLine = SPACE_SPLITTER.splitToList(lines.get(0));
        String lastLine = parsedTotalStationsLine.get(parsedTotalStationsLine.size() - 1);
        Assert.assertEquals(lines.size() - NUMBER_OF_NONEMPTY_INFO_LINES, Integer.parseInt(lastLine));

        List<StationInfo> stationInfoList = new ArrayList<>();
        for (int idx = NUMBER_OF_NONEMPTY_INFO_LINES; idx < lines.size(); ++idx) {
            stationInfoList.add(StationInfo.getStationInfo(TAB_SPLITTER.splitToList(lines.get(idx))));
        }
        return stationInfoList;
    }

    public StationInfo getStation(String macAddress) throws Exception {
        List<StationInfo> stationInfoList = listStations();
        for (StationInfo station : stationInfoList) {
            if (station.macAddress.equals(macAddress)) {
                return station;
            }
        }
        return null;
    }

    private void setSnr(String macAddress1, String macAddress2, int snr) throws Exception {
        CommandResult result = runWmediumdCommand(10000, "set_snr", macAddress1, macAddress2, Integer.toString(snr));
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }

    private void setPosition(String macAddress, double xPosition, double yPosition) throws Exception {
        CommandResult result = runWmediumdCommand(10000, "--", "set_position", macAddress, Double.toString(xPosition), Double.toString(yPosition));
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }

    private void setLci(String macAddress, String lci) throws Exception {
        CommandResult result = runWmediumdCommand(10000, "set_lci", macAddress, lci);
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }

    private void setCivicloc(String macAddress, String civicloc) throws Exception {
        CommandResult result = runWmediumdCommand(10000, "set_civicloc", macAddress, civicloc);
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }

    @Before
    public void setUp() throws Exception {
        this.testDevice = getDevice();
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlListStations() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        listStations();
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetSnr() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        List<StationInfo> stationInfoList = listStations();
        String stationMacAddress = getStationMacAddress(stationInfoList);
        String apMacAddress = getApMacAddress(stationInfoList);
        int rssiDefault = getRSSI();
        int rssiSnr11, rssiSnr88;

        setSnr(apMacAddress, stationMacAddress, 11);
        while ((rssiSnr11 = getRSSI()) == rssiDefault) {
            Thread.sleep(1000);
        }

        setSnr(apMacAddress, stationMacAddress, 88);
        while ((rssiSnr88 = getRSSI()) == rssiSnr11) {
            Thread.sleep(1000);
        }

        Assert.assertTrue(rssiSnr11 < rssiSnr88);
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetPosition() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        List<StationInfo> stationInfoList = listStations();
        String stationMacAddress = getStationMacAddress(stationInfoList);
        String apMacAddress = getApMacAddress(stationInfoList);
        int rssiDefault = getRSSI();
        int rssiDistance1000, rssiDistance100, rssiDistance10;

        setPosition(apMacAddress, 0.0, 0.0);
        setPosition(stationMacAddress, 0.0, -1000.0);
        while ((rssiDistance1000 = getRSSI()) == rssiDefault) {
            Thread.sleep(1000);
        }

        setPosition(stationMacAddress, 0.0, 100.0);
        while ((rssiDistance100 = getRSSI()) == rssiDistance1000) {
            Thread.sleep(1000);
        }

        setPosition(stationMacAddress, -10.0, 0.0);
        while ((rssiDistance10 = getRSSI()) == rssiDistance100) {
            Thread.sleep(1000);
        }

        Assert.assertTrue(rssiDistance1000 < rssiDistance100);
        Assert.assertTrue(rssiDistance100 < rssiDistance10);
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetLci() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", ""))
            return;

        List<StationInfo> stationInfoList = listStations();
        String apMacAddress = getApMacAddress(stationInfoList);

        String testLci = "abcdef";

        setLci(apMacAddress, testLci);

        StationInfo apStation = getStation(apMacAddress);

        String trimmedLci = apStation.lci.substring(1, apStation.lci.length() - 1);
        Assert.assertEquals(testLci, trimmedLci);
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetCivicloc() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", ""))
            return;

        List<StationInfo> stationInfoList = listStations();
        String apMacAddress = getApMacAddress(stationInfoList);

        String testCivicloc = "zxcvb";

        setCivicloc(apMacAddress, testCivicloc);

        StationInfo apStation = getStation(apMacAddress);

        String trimmedCivicloc = apStation.civicloc.substring(1, apStation.civicloc.length() - 1);
        Assert.assertEquals(testCivicloc, trimmedCivicloc);
    }
}
