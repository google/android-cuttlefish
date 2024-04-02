/*
 * Copyright (C) 2023 The Android Open Source Project
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

import static org.junit.Assert.assertTrue;

import com.android.tradefed.config.Option;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.internal.DeviceSnapshotHandler;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import java.io.File;
import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Test snapshot/restore function.
 *
 * <p>* This test resets the device thus it should not run with other tests in the same test suite
 * to avoid unexpected behavior.
 *
 * <p>* The test logic relies on cvd and snapshot_util_cvd tools, so it can only run in a test lab
 * setup.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class SnapshotTest extends BaseHostJUnit4Test {

  @Option(
      name = "test-count",
      description = "Number of times to restore the device back to snapshot state.")
  private int mTestCount = 1;

  @Test
  public void testSnapshot() throws Exception {
    // Snapshot the device
    DeviceSnapshotHandler handler = new DeviceSnapshotHandler();
    boolean snapshotRes = false;
    snapshotRes = handler.snapshotDevice(getDevice(), String.format("snapshot_img%d", mTestCount));

    if (!snapshotRes) {
      Assert.fail("failed to snapshot.");
    }

    // Create a file in tmp directory
    final String tmpFile = "/data/local/tmp/snapshot_tmp";
    getDevice().executeShellCommand("touch " + tmpFile);

    // Reboot the device to make sure the file persits.
    getDevice().reboot();
    File file = getDevice().pullFile(tmpFile);
    if (file == null) {
      Assert.fail("Setup failed: tmp file failed to persist after device reboot.");
    }

    long startAllRuns = System.currentTimeMillis();
    for (int i = 0; i < mTestCount; i++) {
      CLog.d("Restore snapshot attempt #%d", i);
      long start = System.currentTimeMillis();
      // We don't usually expect tests to use our feature server, but in this case we are
      // validating the feature itself so it's fine
      boolean restoreRes = false;
      try {
        handler = new DeviceSnapshotHandler();
        restoreRes = handler.restoreSnapshotDevice(getDevice(), String.format("snapshot_img%d", mTestCount));
      } catch (DeviceNotAvailableException e) {
        CLog.e(e);
      }
      assertTrue(
          String.format("Restore snapshot for device reset failed during attempt #%d", i), restoreRes);
      long duration = System.currentTimeMillis() - start;
      CLog.d("Restore snapshot took %dms to finish", duration);
    }
    CLog.d(
        "%d Restore snapshot runs finished successfully, with average time of %dms",
        mTestCount, (System.currentTimeMillis() - startAllRuns) / mTestCount);

    // Verify that the device is back online and pre-existing file is gone.
    file = getDevice().pullFile(tmpFile);
    if (file != null) {
      Assert.fail("Restore snapshot failed: pre-existing file still exists.");
    }
  }

  // Make sure reboots work correctly after a restore.
  //
  // There is some overlap between the cuttleifsh's support for restore and
  // reboot and so it can be easy for change to one to break the other.
  @Test
  public void testSnapshotReboot() throws Exception {
    DeviceSnapshotHandler handler = new DeviceSnapshotHandler();
    // Snapshot the device>
    boolean snapshotRes = handler.snapshotDevice(getDevice(), "snapshot_img");
    assertTrue("failed to snapshot", snapshotRes);
    // Restore the device.
    boolean restoreRes = handler.restoreSnapshotDevice(getDevice(), "snapshot_img");
    assertTrue("Restore snapshot for device reset failed", restoreRes);
    // Reboot the device.
    getDevice().reboot();
    // Verify that the device is back online.
    getDevice().executeShellCommand("echo test");
  }
}
