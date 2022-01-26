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
import static org.junit.Assume.assumeNotNull;

import com.google.inject.Inject;
import java.io.File;
import javax.annotation.Nullable;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(CuttlefishIntegrationTestRunner.class)
public class AcloudLocalInstanceTest {
  @Inject(optional = true)
  @SetOption("gce-driver-service-account-json-key-path")
  @Nullable
  private String gceJsonKeyPath = null;

  @Inject @Rule public GceInstanceRule gceInstance;
  @Inject private BuildChooser buildChooser;

  @Test
  public void launchAcloudPrebuilt() throws Exception {
    assumeNotNull(gceJsonKeyPath);
    gceInstance.uploadFile(new File(gceJsonKeyPath), "key.json");
    assertEquals(0, gceInstance.ssh("sudo", "apt-get", "install", "adb", "-y").returnCode());
    gceInstance.uploadBuildArtifact("acloud_prebuilt", "acloud");
    assertEquals(0, gceInstance.ssh("chmod", "+x", "acloud").returnCode());
    // TODO(schuffelen): Make this choose the current build
    assertEquals(0,
        gceInstance
            .ssh("./acloud", "create", "-y", "--local-instance", "--skip-pre-run-check",
                "--service-account-json-private-key-path=key.json",
                "--build-id=" + buildChooser.buildId(),
                "--build-target=" + buildChooser.buildFlavor())
            .returnCode());
  }

  @Test
  public void launchAcloudDev() throws Exception {
    assumeNotNull(gceJsonKeyPath);
    gceInstance.uploadFile(new File(gceJsonKeyPath), "key.json");
    assertEquals(0, gceInstance.ssh("sudo", "apt-get", "install", "adb", "-y").returnCode());
    gceInstance.uploadBuildArtifact("acloud-dev", "acloud");
    assertEquals(0, gceInstance.ssh("chmod", "+x", "acloud").returnCode());
    // TODO(schuffelen): Make this choose the current build
    assertEquals(0,
        gceInstance
            .ssh("./acloud", "create", "-y", "--local-instance", "--skip-pre-run-check",
                "--service-account-json-private-key-path=key.json",
                "--build-id=" + buildChooser.buildId(),
                "--build-target=" + buildChooser.buildFlavor())
            .returnCode());
  }
}
