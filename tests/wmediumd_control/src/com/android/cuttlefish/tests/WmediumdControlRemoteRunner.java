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
import com.android.tradefed.device.cloud.RemoteSshUtil;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;

public class WmediumdControlRemoteRunner extends WmediumdControlRunner {
    private RemoteAndroidVirtualDevice testDevice;
    private GceAvdInfo gceAvd;
    private TestDeviceOptions options;

    public WmediumdControlRemoteRunner(RemoteAndroidVirtualDevice testDevice) throws Exception {
        super();
        this.testDevice = testDevice;
        this.gceAvd = testDevice.getAvdInfo();
        this.options = testDevice.getOptions();

        String username = this.options.getInstanceUser();
        this.wmediumdControlCommand = "/home/" + username + "/bin/wmediumd_control --wmediumd_api_server=/home/" + username + "/cuttlefish_runtime/internal/wmediumd_api_server";
        CLog.i("Wmediumd Control Command Path: " + this.wmediumdControlCommand);
    }

    @Override
    protected CommandResult run(long timeout, String... command) {
        return RemoteSshUtil.remoteSshCommandExec(gceAvd, options, runUtil, timeout, command);
    }
}