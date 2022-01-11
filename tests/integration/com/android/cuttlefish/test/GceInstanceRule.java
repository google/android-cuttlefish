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
package com.android.cuttlefish.test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assume.assumeNoException;
import static org.junit.Assume.assumeTrue;

import com.android.tradefed.invoker.TestInformation;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import java.io.File;
import java.io.IOException;
import java.util.UUID;
import javax.annotation.Nullable;
import org.junit.rules.TestRule;
import org.junit.runner.Description;
import org.junit.runners.model.Statement;

/**
 * Manager for a dedicated GCE instance for every @Test function.
 *
 * Must be constructed through Guice injection. Calls out to the cvd_test_gce_driver binary to
 * create the GCE instances.
 */
public final class GceInstanceRule implements TestRule {
  @Inject(optional = true)
  @SetOption("gce-driver-service-account-json-key-path")
  @Nullable
  private String gceJsonKeyPath = null;

  @Inject private TestInformation testInfo;

  private File gceDriver;
  private Process driverProcess;

  private Process launchDriver() throws IOException {
    ImmutableList.Builder<String> cmdline = new ImmutableList.Builder();
    cmdline.add(gceDriver.toString());
    cmdline.add("--service-account-json-private-key-path=" + gceJsonKeyPath);
    cmdline.add("--instance-name=cuttlefish-integration-" + UUID.randomUUID());
    ProcessBuilder processBuilder = new ProcessBuilder(cmdline.build());
    processBuilder.redirectInput(ProcessBuilder.Redirect.PIPE);
    processBuilder.redirectOutput(ProcessBuilder.Redirect.PIPE);
    processBuilder.redirectError(ProcessBuilder.Redirect.INHERIT);
    return processBuilder.start();
  }

  @Override
  public Statement apply(Statement base, Description description) {
    assumeTrue("gce-driver-service-account-json-key-path not set", gceJsonKeyPath != null);
    try {
      gceDriver = testInfo.getDependencyFile("cvd_test_gce_driver", false);
    } catch (Exception e) {
      assumeNoException("Could not find cvd_test_gce_driver", e);
    }
    assumeTrue("cvd_test_gce_driver file did not exist", gceDriver.exists());
    return new Statement() {
      @Override
      public void evaluate() throws Throwable {
        driverProcess = launchDriver();
        base.evaluate();
        assertEquals(0, driverProcess.waitFor());
        driverProcess = null;
      }
    };
  }
}
