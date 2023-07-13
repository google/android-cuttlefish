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

import static org.junit.Assert.fail;

import com.android.tradefed.config.GlobalConfiguration;
import com.android.tradefed.config.Option;
import com.android.tradefed.config.OptionCopier;
import com.android.tradefed.device.DeviceManager;
import com.android.tradefed.device.FreeDeviceState;
import com.android.tradefed.device.IDeviceManager;
import com.android.tradefed.device.RemoteAvdIDevice;
import com.android.tradefed.device.cloud.RemoteAndroidVirtualDevice;
import com.android.tradefed.device.connection.AdbSshConnection;
import com.android.tradefed.device.connection.DefaultConnection;
import com.android.tradefed.device.connection.DefaultConnection.ConnectionBuilder;
import com.android.tradefed.log.ITestLogger;
import com.android.tradefed.log.LogUtil.CLog;
import com.android.tradefed.result.InputStreamSource;
import com.android.tradefed.result.LogDataType;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.DeviceJUnit4ClassRunner.TestLogData;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.util.RunUtil;

import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

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

    @Rule public TestLogData mLogs = new TestLogData();

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

    public class ForwardLogger implements ITestLogger {
        @Override
        public void testLog(String dataName, LogDataType dataType, InputStreamSource dataStream) {
            mLogs.addTestLog(dataName, dataType, dataStream);
        }
    }

    public void tryBringupCuttlefish() throws Exception {
        String initialSerial = String.format("gce-device-tmp-%s", UUID.randomUUID().toString());
        CLog.v("initialSerial: " + initialSerial);
        // TODO: Modify to a temporary null-device to avoid creating placeholders
        RemoteAvdIDevice idevice = new RemoteAvdIDevice(initialSerial);
        IDeviceManager manager = GlobalConfiguration.getDeviceManagerInstance();
        ((DeviceManager) manager).addAvailableDevice(idevice);
        RemoteAndroidVirtualDevice device =
                (RemoteAndroidVirtualDevice)
                        manager.forceAllocateDevice(idevice.getSerialNumber());
        OptionCopier.copyOptions(getDevice().getOptions(), device.getOptions());
        try {
            ConnectionBuilder builder = new ConnectionBuilder(
                new RunUtil(), device, getBuild(), new ForwardLogger());
            DefaultConnection con = DefaultConnection.createConnection(builder);
            if (!(con instanceof AdbSshConnection)) {
                throw new RuntimeException(
                        String.format(
                            "Something went wrong, excepted AdbSshConnection got %s", con));
            }
            AdbSshConnection connection = (AdbSshConnection) con;
            try {
                connection.initializeConnection();
            } finally {
                connection.tearDownConnection();
            }
        } finally {
            manager.freeDevice(device, FreeDeviceState.AVAILABLE);
        }
    }
}
