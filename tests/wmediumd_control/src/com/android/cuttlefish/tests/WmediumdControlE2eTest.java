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
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.wmediumd.Wmediumd.ListStationsResponse;
import com.android.wmediumd.Wmediumd.SetCiviclocRequest;
import com.android.wmediumd.Wmediumd.SetLciRequest;
import com.android.wmediumd.Wmediumd.SetPositionRequest;
import com.android.wmediumd.Wmediumd.SetSnrRequest;
import com.android.wmediumd.Wmediumd.StationInfo;

import com.google.protobuf.TextFormat;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.FileNotFoundException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;

@RunWith(DeviceJUnit4ClassRunner.class)
public class WmediumdControlE2eTest extends CuttlefishHostTest {

    private static final String CVD_ENV_BINARY_BASENAME = "cvd_internal_env";

    private static final String MESSAGE_OK = "Rpc succeeded with OK status\n";

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

    private String getStationMacAddress(ListStationsResponse response) {
        List<String> stationMacAddressList =
                response.getStationsList().stream()
                        .map(station -> station.getMacAddress())
                        .filter(addr -> addr.startsWith("02:15:"))
                        .collect(Collectors.toList());
        Assert.assertTrue(stationMacAddressList.size() > 0);
        return stationMacAddressList.get(0);
    }

    private String getApMacAddress(ListStationsResponse response) {
        List<String> apMacAddressList =
                response.getStationsList().stream()
                        .map(station -> station.getMacAddress())
                        .filter(addr -> addr.startsWith("42:00:"))
                        .collect(Collectors.toList());
        Assert.assertTrue(apMacAddressList.size() > 0);
        return apMacAddressList.get(0);
    }

    private StationInfo getStationInfo(ListStationsResponse response, String macAddress) {
        List<StationInfo> stationInfoList =
                response.getStationsList().stream()
                        .filter(station -> station.getMacAddress().equals(macAddress))
                        .collect(Collectors.toList());
        Assert.assertEquals(1, stationInfoList.size());
        return stationInfoList.get(0);
    }

    private CommandResult runWmediumdCommand(long timeout, String... command)
            throws FileNotFoundException {
        Assert.assertNotNull(runner);

        ArrayList<String> fullCommand = new ArrayList<String>();
        fullCommand.add(runner.getHostBinaryPath(CVD_ENV_BINARY_BASENAME));
        fullCommand.add("cvd-1");
        fullCommand.add("call");
        fullCommand.add("WmediumdService");
        fullCommand.addAll(Arrays.asList(command));

        return runner.run(timeout, fullCommand.toArray(new String[0]));
    }

    public ListStationsResponse listStations() throws Exception {
        CommandResult result = runWmediumdCommand(10000, "ListStations", "");
        CLog.i("stdout:%s", result.getStdout());
        CLog.i("stderr:%s", result.getStderr());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        Assert.assertTrue(result.getStderr().contains(MESSAGE_OK));

        return TextFormat.parse(result.getStdout(), ListStationsResponse.class);
    }

    private void setSnr(String macAddress1, String macAddress2, int snr) throws Exception {
        SetSnrRequest request =
                SetSnrRequest.newBuilder()
                        .setMacAddress1(macAddress1)
                        .setMacAddress2(macAddress2)
                        .setSnr(snr)
                        .build();
        CommandResult result = runWmediumdCommand(10000, "SetSnr", request.toString());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        Assert.assertTrue(result.getStderr().contains(MESSAGE_OK));
    }

    private void setPosition(String macAddress, double xPosition, double yPosition)
            throws Exception {
        SetPositionRequest request =
                SetPositionRequest.newBuilder()
                        .setMacAddress(macAddress)
                        .setXPos(xPosition)
                        .setYPos(yPosition)
                        .build();
        CommandResult result = runWmediumdCommand(10000, "SetPosition", request.toString());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        Assert.assertTrue(result.getStderr().contains(MESSAGE_OK));
    }

    private void setLci(String macAddress, String lci) throws Exception {
        SetLciRequest request =
                SetLciRequest.newBuilder().setMacAddress(macAddress).setLci(lci).build();
        CommandResult result = runWmediumdCommand(10000, "SetLci", request.toString());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        Assert.assertTrue(result.getStderr().contains(MESSAGE_OK));
    }

    private void setCivicloc(String macAddress, String civicloc) throws Exception {
        SetCiviclocRequest request =
                SetCiviclocRequest.newBuilder()
                        .setMacAddress(macAddress)
                        .setCivicloc(civicloc)
                        .build();
        CommandResult result = runWmediumdCommand(10000, "SetCivicloc", request.toString());
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        Assert.assertTrue(result.getStderr().contains(MESSAGE_OK));
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

        ListStationsResponse stationInfoList = listStations();
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

        ListStationsResponse stationInfoList = listStations();
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
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        String apMacAddress = getApMacAddress(listStations());
        String testLci = "abcdef";

        setLci(apMacAddress, testLci);
        StationInfo station = getStationInfo(listStations(), apMacAddress);
        Assert.assertEquals(testLci, station.getLci());
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetCivicloc() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        String apMacAddress = getApMacAddress(listStations());
        String testCivicloc = "zxcvb";

        setCivicloc(apMacAddress, testCivicloc);
        StationInfo station = getStationInfo(listStations(), apMacAddress);
        Assert.assertEquals(testCivicloc, station.getCivicloc());
    }
}
