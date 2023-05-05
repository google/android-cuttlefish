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

import com.android.tradefed.config.GlobalConfiguration;
import com.android.tradefed.config.Option;
import com.android.tradefed.device.DeviceManager;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.IDeviceManager;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.ITestDevice.RecoveryMode;
import com.android.tradefed.device.cloud.GceAvdInfo;
import com.android.tradefed.device.cloud.GceAvdInfo.GceStatus;
import com.android.tradefed.device.cloud.GceManager;
import com.android.tradefed.device.cloud.GceSshTunnelMonitor;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.device.RemoteAvdIDevice;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.result.error.DeviceErrorIdentifier;
import com.android.tradefed.result.error.ErrorIdentifier;
import com.android.tradefed.result.proto.TestRecordProto.FailureStatus;
import com.android.tradefed.targetprep.TargetSetupError;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.CommandStatus;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.fail;

/**
 * This test runs a reliability test for a Cuttlefish bringup by checking if the cuttlefish
 * successfully boots and passes a set of stability tests.
 *
 * <p>* This test resets the device thus it should not run with other tests in the same test suite
 * to avoid unexpected behavior.
 *
 * <p>* The test logic relies on Cuttlefish(launch_cvd, powerwash_cvd) tools, so it can only run in
 * a test lab setup. Note that this code should only be run in delegated-tf mode to avoid device
 * manager cleanup.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class CuttlefishReliabilityTest extends BaseHostJUnit4Test {
    @Option(
            name = "cuttlefish-bringup-count",
            description = "Number of times to lease cuttlefish device.")
    private int mTestCount = 10;

    @Option(
            name = "cuttlefish-concurrent-bringup",
            description = "cuttlefish-concurrent-bringup.")
    private int mConcurrentBringup = 10;

    @Option(
            name = "cuttlefish-max-error-ratio",
            description = "cuttlefish-max-error-ratio."
    )
    private double mMaxErrorRate = 0.02;

    @Test
    public void testCuttlefishBootReliability() throws Exception {
        ExecutorService executorService = Executors.newFixedThreadPool(mConcurrentBringup);
        AtomicInteger numErrors = new AtomicInteger(0);
        AtomicInteger count = new AtomicInteger(0);

        // Create a list to store the returned Futures
        List<Future<?>> futures = new ArrayList<>();
        for (int i = 0; i < mTestCount; i++) {
            futures.add(executorService.submit(() -> {
                CLog.v("Attempt #" + count.incrementAndGet());
                try {
                    // Try to bring up cuttlefish
                    tryBringupCuttlefish();
                } catch (Exception e) {
                    // if an error occurs, increment the error count
                    numErrors.incrementAndGet();
                    CLog.e(e);
                }
            }));
        }
        // Wait for all tasks to complete and check their results
        for (Future<?> future : futures) {
            try {
                future.get();
            } catch (Exception e) {
                // Handle any exceptions thrown by the task
                CLog.e(e);
            }
        }

        executorService.shutdown();
        // The maximum time limit is calculated based on the number of tests to run concurrently,
        // with a minimum of 60 minutes
        executorService.awaitTermination(
                Math.max(60, mTestCount / mConcurrentBringup * 10), TimeUnit.MINUTES);

        double errorRatio = (double) numErrors.get() / mTestCount;
        CLog.v("Error num: %d, Total runs: %d, Error ratio: %.2f",
                numErrors.get(), mTestCount, errorRatio);
        if (errorRatio > mMaxErrorRate) {
            fail("Stability test failed. ");
        }
    }

    public void tryBringupCuttlefish() throws Exception {
        GceAvdInfo gceAvd;
        GceSshTunnelMonitor gceSshMonitor = null;
        RemoteAndroidVirtualDevice device = null;
        GceManager gceHandler = new GceManager(
                        getDevice().getDeviceDescriptor(),
                        getDevice().getOptions(),
                        getBuild());
        boolean cvdExceptionOccurred = false;

        String initialSerial = String.format("gce-device-tmp-%s", UUID.randomUUID().toString());
        CLog.v("initialSerial: " + initialSerial);

        try {
            // Attempts to lease a cuttlefish.
            try {
                gceAvd = gceHandler.startGce();
            } catch (TargetSetupError e) {
                CLog.e("Failed to lease device: %s", e.getMessage());
                throw new DeviceNotAvailableException(
                        "Exception during AVD GCE startup",
                        e,
                        initialSerial,
                        e.getErrorId());
            }

            // Checks if the GCE AVD failed to boot on the first attempt.
            // If so, throws a DeviceNotAvailableException with details of the error.
            if (GceStatus.BOOT_FAIL.equals(gceAvd.getStatus())) {
                cvdExceptionOccurred = true;
                throw new DeviceNotAvailableException(
                        initialSerial + " first time boot error: " + gceAvd.getErrors());
            }
            RemoteAvdIDevice idevice = new RemoteAvdIDevice(initialSerial);
            IDeviceManager manager = GlobalConfiguration.getDeviceManagerInstance();
            ((DeviceManager) manager).addAvailableDevice(idevice);
            device =
                    (RemoteAndroidVirtualDevice)
                            manager.forceAllocateDevice(idevice.getSerialNumber());
            device.setAvdInfo(gceAvd);

            // Manage the ssh bridge to the avd device.
            gceSshMonitor = new GceSshTunnelMonitor(
                            device, getBuild(), gceAvd.hostAndPort(), getDevice().getOptions());
            gceSshMonitor.start();
            device.setGceSshMonitor(gceSshMonitor);

            try {
                // check if device is online after GCE bringup, i.e. if adb is broken
                CLog.v("Waiting for device %s online", device.getSerialNumber());
                device.setRecoveryMode(RecoveryMode.ONLINE);
                device.waitForDeviceOnline();
                CLog.i(
                        "Device is still online, version: %s",
                        device.getProperty("ro.system.build.version.incremental"));
            } catch (DeviceNotAvailableException dnae) {
                String message = "AVD GCE not online after startup";
                cvdExceptionOccurred = true;
                throw new DeviceNotAvailableException(initialSerial + ": " + message);
            }

            // TODO: Invoke powerwash after the device boots up.
        } finally {
            // Checks if an exception occurred during the cuttlefish boot process and attempts to
            // collect logs if it did.
            if (cvdExceptionOccurred) {
                // TODO: Collect logs
            }
            // tear down cuttlefish
            if (gceHandler != null) {
                // Stop the bridge
                if (gceSshMonitor != null) {
                    gceSshMonitor.shutdown();
                    try {
                        gceSshMonitor.joinMonitor();
                    } catch (InterruptedException e1) {
                        CLog.i("Interrupted while waiting for GCE SSH monitor to shutdown.");
                    }
                }
                gceHandler.shutdownGce();
                gceHandler.cleanUp();
            }
            // This test is running in delegated-tf mode, there's no need for extra clean-up steps.
        }
        return;
    }
}
