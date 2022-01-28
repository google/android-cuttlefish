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
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(CuttlefishIntegrationTestRunner.class)
public class GceInstanceRuleTest {
  @Inject @Rule public GceInstanceRule gceInstance;

  @Test
  public void createInstance() {}

  @Test
  public void sshToInstance() throws Exception {
    assertEquals(0, gceInstance.ssh("ls", "/").returnCode());
  }

  @Test
  public void uploadBuildArtifact() throws Exception {
    gceInstance.uploadBuildArtifact("fetch_cvd", "/home/vsoc-01/fetch_cvd");
    assertEquals(0, gceInstance.ssh("chmod", "+x", "/home/vsoc-01/fetch_cvd").returnCode());
  }
}
