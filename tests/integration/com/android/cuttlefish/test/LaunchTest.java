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
import static org.junit.Assert.assertNotNull;

import javax.inject.Inject;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(CuttlefishIntegrationTestRunner.class)
public class LaunchTest {
  @Inject @Rule public GceInstanceRule gceInstance;
  @Inject private BuildChooser buildChooser;

  @Before
  public void downloadInstanceFiles() throws Exception {
    gceInstance.uploadBuildArtifact("fetch_cvd", "fetch_cvd");
    assertEquals(0, gceInstance.ssh("chmod", "+x", "fetch_cvd").returnCode());
    // TODO(schuffelen): Make this fetch the current build
    assertEquals(0,
        gceInstance.ssh("./fetch_cvd", "-default_build=" + buildChooser.fetchCvdBuild())
            .returnCode());
  }

  @Test
  public void launchDaemon() throws Exception {
    assertEquals(0,
        gceInstance.ssh("bin/launch_cvd", "--daemon", "--report_anonymous_usage_stats=y")
            .returnCode());
  }

  @Test
  public void launchDaemonStop() throws Exception {
    assertEquals(0,
        gceInstance.ssh("bin/launch_cvd", "--daemon", "--report_anonymous_usage_stats=y")
            .returnCode());
    assertEquals(0, gceInstance.ssh("bin/stop_cvd").returnCode());
  }

  @Test
  public void launchDaemonStopLaunch() throws Exception {
    assertEquals(0,
        gceInstance.ssh("bin/launch_cvd", "--daemon", "--report_anonymous_usage_stats=y")
            .returnCode());
    assertEquals(0, gceInstance.ssh("bin/stop_cvd").returnCode());
    assertEquals(0,
        gceInstance.ssh("bin/launch_cvd", "--daemon", "--report_anonymous_usage_stats=y")
            .returnCode());
  }
}
