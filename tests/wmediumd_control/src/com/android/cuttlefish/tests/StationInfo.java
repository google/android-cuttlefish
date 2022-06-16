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

import java.util.regex.Matcher;
import java.util.regex.Pattern;

import org.junit.Assert;

public class StationInfo {
    public String macAddress;
    public double xPosition;
    public double yPosition;
    public int txPower;

    public StationInfo(String macAddress, double xPosition, double yPosition, int txPower) throws Exception {
        Assert.assertTrue(isValidMacAddr(macAddress));
        this.macAddress = macAddress;
        this.xPosition = xPosition;
        this.yPosition = yPosition;
        this.txPower = txPower;
    }

    private static boolean isValidMacAddr(String str) {
        Pattern pattern = Pattern.compile("^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$");
        Matcher matcher = pattern.matcher(str);
        return matcher.find();
    }

    public static StationInfo getStationInfo(String[] stationInfoLine) throws Exception {
        return new StationInfo(stationInfoLine[0], Double.parseDouble(stationInfoLine[1]), Double.parseDouble(stationInfoLine[2]), Integer.parseInt(stationInfoLine[3]));
    }
}