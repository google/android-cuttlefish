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

import com.android.tradefed.device.TestDeviceOptions;
import com.android.tradefed.device.cloud.GceAvdInfo;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.device.cloud.RemoteFileUtil;
import com.android.tradefed.device.cloud.RemoteSshUtil;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;

import java.util.Arrays;
import java.util.List;
import java.util.Optional;

import org.junit.Assert;

public class WmediumdControlRemoteRunner extends WmediumdControlRunner {
    private RemoteAndroidVirtualDevice testDevice;
    private GceAvdInfo gceAvd;
    private TestDeviceOptions options;

    private static final String WMEDIUMD_CONTROL_SUBPATH = "/bin/wmediumd_control";
    private static final String WMEDIUMD_API_SERVER_SUBPATH =
            "/cuttlefish_runtime/internal/wmediumd_api_server";
    private static final int TIMEOUT_MILLIS = 10000;

    public WmediumdControlRemoteRunner(RemoteAndroidVirtualDevice testDevice) throws Exception {
        super();
        this.testDevice = testDevice;
        this.gceAvd = testDevice.getAvdInfo();
        this.options = testDevice.getOptions();

        List<String> basePathCandidates =
                Arrays.asList("/home/" + this.options.getInstanceUser(), "/tmp/cfbase/3");
        Optional<String> basePath =
                basePathCandidates.stream().filter(x -> remoteExists(x)).findFirst();
        Assert.assertTrue(basePath.isPresent());

        this.wmediumdControlCommand =
                basePath.get()
                        + WMEDIUMD_CONTROL_SUBPATH
                        + " --wmediumd_api_server="
                        + basePath.get()
                        + WMEDIUMD_API_SERVER_SUBPATH;
        CLog.i("Wmediumd Control Command Path: " + this.wmediumdControlCommand);
    }

    private boolean remoteExists(String path) {
        return RemoteFileUtil.doesRemoteFileExist(
                        gceAvd, options, runUtil, TIMEOUT_MILLIS, path + WMEDIUMD_CONTROL_SUBPATH)
                && RemoteFileUtil.doesRemoteFileExist(
                        gceAvd,
                        options,
                        runUtil,
                        TIMEOUT_MILLIS,
                        path + WMEDIUMD_API_SERVER_SUBPATH);
    }

    @Override
    protected CommandResult run(long timeout, String... command) {
        return RemoteSshUtil.remoteSshCommandExec(gceAvd, options, runUtil, timeout, command);
    }
}
