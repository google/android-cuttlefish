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

import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;

import java.util.List;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(DeviceJUnit4ClassRunner.class)
public class WmediumdControlE2eTest extends BaseHostJUnit4Test {

    ITestDevice testDevice;
    WmediumdControlRunner runner;

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

    @Before
    public void setUp() throws Exception {
        testDevice = getDevice();
        CLog.i("Test Device Class Name: " + testDevice.getClass().getSimpleName());

        if (testDevice instanceof RemoteAndroidVirtualDevice) {
            runner = new WmediumdControlRemoteRunner((RemoteAndroidVirtualDevice)testDevice);
        }
        else {
            runner = new WmediumdControlLocalRunner(testDevice, getTestInformation());
        }
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlListStations() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        runner.listStations();
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetSnr() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        List<StationInfo> stationInfoList = runner.listStations();
        Assert.assertTrue(stationInfoList.size() >= 2);
        int rssiDefault = getRSSI();
        int rssiSnr11, rssiSnr88;

        runner.setSnr(stationInfoList.get(0).macAddress, stationInfoList.get(1).macAddress, 11);
        while ((rssiSnr11 = getRSSI()) == rssiDefault) {
            Thread.sleep(1000);
        }

        runner.setSnr(stationInfoList.get(0).macAddress, stationInfoList.get(1).macAddress, 88);
        while ((rssiSnr88 = getRSSI()) == rssiSnr11) {
            Thread.sleep(1000);
        }

        Assert.assertTrue(rssiSnr11 < rssiSnr88);
    }

    @Test(timeout = 60 * 1000)
    public void testWmediumdControlSetPosition() throws Exception {
        if (!testDevice.connectToWifiNetwork("VirtWifi", "")) return;

        List<StationInfo> stationInfoList = runner.listStations();
        Assert.assertTrue(stationInfoList.size() >= 2);
        int rssiDefault = getRSSI();
        int rssiDistance1000, rssiDistance100, rssiDistance10;

        runner.setPosition(stationInfoList.get(0).macAddress, 0.0, 0.0);
        runner.setPosition(stationInfoList.get(1).macAddress, 0.0, -1000.0);
        while ((rssiDistance1000 = getRSSI()) == rssiDefault) {
            Thread.sleep(1000);
        }

        runner.setPosition(stationInfoList.get(1).macAddress, 0.0, 100.0);
        while ((rssiDistance100 = getRSSI()) == rssiDistance1000) {
            Thread.sleep(1000);
        }

        runner.setPosition(stationInfoList.get(1).macAddress, -10.0, 0.0);
        while ((rssiDistance10 = getRSSI()) == rssiDistance100) {
            Thread.sleep(1000);
        }

        Assert.assertTrue(rssiDistance1000 < rssiDistance100);
        Assert.assertTrue(rssiDistance100 < rssiDistance10);
    }
}
