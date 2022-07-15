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

import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.tradefed.util.IRunUtil;
import com.android.tradefed.util.RunUtil;

import java.util.ArrayList;
import java.util.List;

import org.junit.Assert;

abstract class WmediumdControlRunner {
    protected String wmediumdControlCommand;
    protected IRunUtil runUtil;

    public WmediumdControlRunner() {
        this.runUtil = new RunUtil();
    }

    abstract protected CommandResult run(long timeout, String... command);

    public List<StationInfo> listStations() throws Exception {
        CommandResult result = run(10000, wmediumdControlCommand, "list_stations");
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
        CLog.i(result.getStdout());

        String[] lines = result.getStdout().split("\n");
        String[] parsedTotalStationsLine = lines[0].split(" ");
        Assert.assertEquals(lines.length - 3, Integer.parseInt(parsedTotalStationsLine[parsedTotalStationsLine.length-1]));

        List<StationInfo> stationInfoList = new ArrayList<>();
        for (int idx = 3; idx < lines.length; ++idx) {
            stationInfoList.add(StationInfo.getStationInfo(lines[idx].split("\t")));
        }
        return stationInfoList;
    }

    public void setSnr(String macAddress1, String macAddress2, int snr) throws Exception {
        CommandResult result = run(10000, wmediumdControlCommand, "set_snr", macAddress1, macAddress2, Integer.toString(snr));
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }

    public void setPosition(String macAddress, double xPosition, double yPosition) throws Exception {
        CommandResult result = run(10000, wmediumdControlCommand, "--", "set_position", macAddress, Double.toString(xPosition), Double.toString(yPosition));
        Assert.assertEquals(CommandStatus.SUCCESS, result.getStatus());
    }
}