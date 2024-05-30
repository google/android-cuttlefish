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
import com.android.tradefed.device.internal.DeviceResetHandler;
import com.android.tradefed.device.internal.DeviceSnapshotHandler;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.util.UUID;

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
    private int mTestCount = 10;

    @Test
    public void testSnapshot() throws Exception {
        String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        // Reboot to make sure device isn't dirty from previous tests.
        getDevice().reboot();
        // Snapshot the device
        new DeviceSnapshotHandler().snapshotDevice(getDevice(), snapshotId);

        // Create a file in tmp directory
        final String tmpFile = "/data/local/tmp/snapshot_tmp";
        getDevice().executeShellCommand("touch " + tmpFile);

        // Reboot the device to make sure the file persists.
        getDevice().reboot();
        File file = getDevice().pullFile(tmpFile);
        if (file == null) {
            Assert.fail("Setup failed: tmp file failed to persist after device reboot.");
        }

        long startAllRuns = System.currentTimeMillis();
        for (int i = 0; i < mTestCount; i++) {
            CLog.d("Restore snapshot attempt #%d", i);
            long start = System.currentTimeMillis();
            new DeviceSnapshotHandler().restoreSnapshotDevice(getDevice(), snapshotId);
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
        String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        // Reboot to make sure device isn't dirty from previous tests.
        getDevice().reboot();
        // Snapshot the device.
        new DeviceSnapshotHandler().snapshotDevice(getDevice(), snapshotId);
        // Restore the device.
        new DeviceSnapshotHandler().restoreSnapshotDevice(getDevice(), snapshotId);
        // Reboot the device.
        getDevice().reboot();
        // Verify that the device is back online.
        getDevice().executeShellCommand("echo test");
    }

    // Test powerwash after restoring
    @Test
    public void testSnapshotPowerwash() throws Exception {
        String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        // Reboot to make sure device isn't dirty from previous tests.
        getDevice().reboot();
        // Snapshot the device.
        new DeviceSnapshotHandler().snapshotDevice(getDevice(), snapshotId);
        // Restore the device.
        new DeviceSnapshotHandler().restoreSnapshotDevice(getDevice(), snapshotId);
        CLog.d("Powerwash attempt after restore");
        long start = System.currentTimeMillis();
        boolean success = new DeviceResetHandler(getInvocationContext()).resetDevice(getDevice());
        assertTrue(String.format("Powerwash reset failed during attempt after restore"), success);
        long duration = System.currentTimeMillis() - start;
        CLog.d("Powerwash took %dms to finish", duration);
        // Verify that the device is back online.
        getDevice().executeShellCommand("echo test");
    }

    // Test powerwash the device, then snapshot and restore
    @Test
    public void testPowerwashSnapshot() throws Exception {
        String snapshotId = "snapshot_" + UUID.randomUUID().toString();

        CLog.d("Powerwash attempt before restore");
        long start = System.currentTimeMillis();
        boolean success = new DeviceResetHandler(getInvocationContext()).resetDevice(getDevice());
        assertTrue(String.format("Powerwash reset failed during attempt before snapshot"), success);
        long duration = System.currentTimeMillis() - start;
        CLog.d("Powerwash took %dms to finish", duration);
        // Verify that the device is back online.
        getDevice().executeShellCommand("echo test");
        // Snapshot the device>
        new DeviceSnapshotHandler().snapshotDevice(getDevice(), snapshotId);
        // Restore the device.
        new DeviceSnapshotHandler().restoreSnapshotDevice(getDevice(), snapshotId);
        // Verify that the device is back online.
        getDevice().executeShellCommand("echo test");
    }
}
