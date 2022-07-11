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
import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.util.CommandResult;

import java.io.File;

import org.junit.Assert;

public class WmediumdControlLocalRunner extends WmediumdControlRunner {
    private ITestDevice testDevice;

    public WmediumdControlLocalRunner(ITestDevice testDevice, TestInformation testInformation) throws Exception {
        super();
        this.testDevice = testDevice;

        String configPath = System.getProperty("user.home") + "/cuttlefish_runtime/cuttlefish_config.json";
        File file = new File(configPath);
        if (!file.exists()) {
            configPath = "/tmp/acloud_cvd_temp/local-instance-1/cuttlefish_runtime/instances/cvd-1/cuttlefish_config.json";
            file = new File(configPath);
            Assert.assertTrue(file.exists());
        }
        runUtil.setEnvVariable("CUTTLEFISH_CONFIG_FILE", configPath);
        this.wmediumdControlCommand = testInformation.getDependencyFile("wmediumd_control", false).getAbsolutePath();
        CLog.i("Wmediumd Control Command Path: " + this.wmediumdControlCommand);
    }

    @Override
    protected CommandResult run(long timeout, String... command) {
        return runUtil.runTimedCmd(timeout, command);
    }
}