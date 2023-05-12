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

package com.android.cuttlefish.tests.utils;

import com.android.tradefed.device.TestDeviceOptions;
import com.android.tradefed.device.cloud.GceAvdInfo;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.device.cloud.RemoteFileUtil;
import com.android.tradefed.device.cloud.RemoteSshUtil;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.IRunUtil;
import com.android.tradefed.util.RunUtil;

import com.google.common.collect.Iterables;

import java.io.FileNotFoundException;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.stream.Collectors;

public class CuttlefishControlRemoteRunner implements CuttlefishControlRunner {

    private static final String OXYGEN_CUTTLEFISH_RUNTIME_DIRECTORY = "/tmp/cfbase/3";

    private static final long DEFAULT_TIMEOUT_MILLIS = 5 * 1000;

    private final IRunUtil runUtil = new RunUtil();

    private final TestDeviceOptions testDeviceOptions;

    private final GceAvdInfo testDeviceAvdInfo;

    private final String basePath;

    public CuttlefishControlRemoteRunner(RemoteAndroidVirtualDevice testDevice)
            throws FileNotFoundException {
        this.testDeviceOptions = testDevice.getOptions();
        this.testDeviceAvdInfo = testDevice.getAvdInfo();

        List<String> basePathCandidates =
                Arrays.asList(
                        "/home/" + this.testDeviceOptions.getInstanceUser(),
                        OXYGEN_CUTTLEFISH_RUNTIME_DIRECTORY);

        Optional<String> basePath =
                basePathCandidates.stream().filter(x -> remoteFileExists(x)).findFirst();
        if (!basePath.isPresent()) {
            throw new FileNotFoundException("Failed to find Cuttlefish runtime directory.");
        }

        this.basePath = basePath.get();
    }

    private boolean remoteFileExists(String path) {
        return RemoteFileUtil.doesRemoteFileExist(
                testDeviceAvdInfo, testDeviceOptions, runUtil, DEFAULT_TIMEOUT_MILLIS, path);
    }

    @Override
    public CommandResult run(long timeout, String... originalCommand) {
        // Note: IRunUtil has setEnvVariable() but that ends up setting the environment
        // variable for the ssh command and not the environment variable on the ssh target.
        List<String> command =
                Arrays.stream(originalCommand)
                        .map(arg -> "\'" + arg + "\'")
                        .collect(Collectors.toCollection(ArrayList::new));

        command.add(0, String.format("HOME=%s", this.basePath));
        String[] commandArray = Iterables.toArray(command, String.class);

        return RemoteSshUtil.remoteSshCommandExec(
                testDeviceAvdInfo, testDeviceOptions, runUtil, timeout, commandArray);
    }

    @Override
    public String getHostBinaryPath(String basename) throws FileNotFoundException {
        return Paths.get(this.basePath, "bin", basename).toAbsolutePath().toString();
    }

    @Override
    public String getHostRuntimePath(String basename) throws FileNotFoundException {
        return Paths.get(this.basePath, "cuttlefish_runtime", basename).toAbsolutePath().toString();
    }
}
