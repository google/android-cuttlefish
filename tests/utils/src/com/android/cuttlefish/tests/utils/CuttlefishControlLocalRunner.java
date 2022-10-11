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

import com.android.tradefed.invoker.TestInformation;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.IRunUtil;
import com.android.tradefed.util.RunUtil;

import java.io.File;
import java.io.FileNotFoundException;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;

import org.junit.Assert;

public class CuttlefishControlLocalRunner implements CuttlefishControlRunner {

    private static final String CVD_CUTTLEFISH_CONFIG =
        System.getProperty("user.home") + "/cuttlefish_runtime/cuttlefish_config.json";

    private static final String ACLOUD_CUTTLEFISH_CONFIG =
        "/tmp/acloud_cvd_temp/local-instance-1/cuttlefish_runtime/instances/cvd-1/cuttlefish_config.json";

    private static final List<String> CUTTLEFISH_CONFIG_CANDIDATES =
        Arrays.asList(CVD_CUTTLEFISH_CONFIG, ACLOUD_CUTTLEFISH_CONFIG);

    private final IRunUtil runUtil = new RunUtil();

    private final TestInformation testInformation;

    private final String runtimeDirectoryPath;

    public CuttlefishControlLocalRunner(TestInformation testInformation) throws FileNotFoundException {
        this.testInformation = testInformation;

        Optional<String> configPath =
            CUTTLEFISH_CONFIG_CANDIDATES.stream().filter(x -> new File(x).exists()).findFirst();
        if (!configPath.isPresent()) {
            throw new FileNotFoundException("Failed to find Cuttlefish config file.");
        }

        runUtil.setEnvVariable("CUTTLEFISH_CONFIG_FILE", configPath.get());

        this.runtimeDirectoryPath = Path.of(configPath.get()).getParent().toString();
    }

    @Override
    public CommandResult run(long timeout, String... command) {
        return runUtil.runTimedCmd(timeout, command);
    }

    @Override
    public String getHostBinaryPath(String basename) throws FileNotFoundException {
        return testInformation.getDependencyFile(basename, false).getAbsolutePath();
    }

    @Override
    public String getHostRuntimePath(String basename) throws FileNotFoundException {
        return Paths.get(this.runtimeDirectoryPath, basename).toAbsolutePath().toString();
    }
}