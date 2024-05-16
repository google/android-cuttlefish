/*
 * Copyright (C) 2019 The Android Open Source Project
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

package com.android.tests.tombstoneTransmit;

import com.android.tradefed.testtype.DeviceJUnit4ClassRunner;
import com.android.tradefed.testtype.junit4.BaseHostJUnit4Test;
import com.android.tradefed.device.ITestDevice;
import com.android.tradefed.device.DeviceNotAvailableException;
import com.android.tradefed.device.TestDeviceOptions;
import com.android.tradefed.device.TestDeviceOptions.InstanceType;
import com.android.tradefed.util.CommandResult;
import com.android.tradefed.util.CommandStatus;
import com.android.tradefed.util.FileUtil;
import com.android.tradefed.util.StreamUtil;
import com.android.tradefed.log.LogUtil.CLog;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.IOException;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;

/**
 * Tests the tombstone transfer feature available on cuttlefish devices. This
 * feature is used to transfer tombstones off the guest as they are created.
 */
@RunWith(DeviceJUnit4ClassRunner.class)
public class TombstoneTransmitTest extends BaseHostJUnit4Test {
    /** Path on the device containing the tombstones */
    private static final String TOMBSTONE_PATH = "/data/tombstones/";
    private static final String TOMBSTONE_PRODUCER = "tombstone_producer";
    private static final int NUM_TOMBSTONES_IN_TEST = 1000;

    /**
     * Creates 15 tombstones on the virtual device of varying lengths.
     * Each tombstone is expected to be sync'd to the host and checked for integrity.
     */
    @Test
    public void testTombstonesOfVaryingLengths() throws Exception {
        InstanceType type = getDevice().getOptions().getInstanceType();
        // It can't be guaranteed that this test is run on a virtual device.
        if(!InstanceType.CUTTLEFISH.equals(type) && !InstanceType.REMOTE_NESTED_AVD.equals(type)) {
            CLog.i("This test must be run on a Cuttlefish device. Aborting.");
            return;
        } else {
            CLog.i("This test IS being run on a Cuttlefish device.");
        }

        clearTombstonesFromCuttlefish();
        List<String> hostTombstoneListPreTest = convertFileListToStringList(getDevice().getTombstones());
        List<String> guestTombstoneListPreTest = convertFileListToStringList(getTombstonesViaAdb());

        // Generate tombstones in doubling sizes from 1k to 16M
        for(int i = 0; i < 15; i++) {
            generateTombstoneOfLengthInKb((int) Math.pow(2,i));
        }

        List<String> hostTombstoneListPostTest =
            convertFileListToStringList(getDevice().getTombstones());
        List<String> guestTombstoneListPostTest =
            convertFileListToStringList(getTombstonesViaAdb());

        // Clear out all tombstones pretest.
        hostTombstoneListPostTest.removeAll(hostTombstoneListPreTest);
        guestTombstoneListPostTest.removeAll(guestTombstoneListPreTest);

        CLog.i("===========Host Tombstone Statistics===========");
        printTombstoneListStats(hostTombstoneListPostTest);
        CLog.i("===========Guest Tombstone Statistics===========");
        printTombstoneListStats(guestTombstoneListPostTest);

        Assert.assertTrue("Tombstones on guest and host do not match",
            hostTombstoneListPostTest.containsAll(guestTombstoneListPostTest));
        Assert.assertEquals("Host does not have expected tombstone count in this iteration",
            hostTombstoneListPostTest.size(), 15);
        Assert.assertEquals("Guest does not have expected tombstone count in this iteration",
            guestTombstoneListPostTest.size(), 15);
    }

    /**
     * Triggers 1000 tombstones on the virtual device and verifies the integrity of each one.
     * Note that the tombstone generation is chunk'd since the virtual device overwrites the oldest
     * tombstone once the 500th is created (or 50th in the case of most physical devices).
     */
    private static final int NUM_TOMBSTONES_PER_LOOP = 500;
    @Test
    public void testTombstoneTransmitIntegrity() throws Exception {
        InstanceType type = getDevice().getOptions().getInstanceType();
        // It can't be guaranteed that this test is run on a virtual device.
        if(!InstanceType.CUTTLEFISH.equals(type) && !InstanceType.REMOTE_NESTED_AVD.equals(type)) {
            CLog.i("This test must be run on a Cuttlefish device. Aborting.");
            return;
        } else {
            CLog.i("This test IS being run on a Cuttlefish device.");
        }

        for(int i = 0; i < 2; i++) {
            clearTombstonesFromCuttlefish();
            List<String> hostTombstoneListPreCrash = convertFileListToStringList(
                getDevice().getTombstones());
            List<String> guestTombstoneListPreCrash = convertFileListToStringList(
                getTombstonesViaAdb());

            for(int j = 0; j < NUM_TOMBSTONES_PER_LOOP; j++) {
                CommandResult commandResult = getDevice().executeShellV2Command(TOMBSTONE_PRODUCER);
                Assert.assertEquals(CommandStatus.FAILED, commandResult.getStatus());
            }

            List<String> hostTombstoneListPostCrash = 
                convertFileListToStringList(getDevice().getTombstones());
            List<String> guestTombstoneListPostCrash = 
                convertFileListToStringList(getTombstonesViaAdb());

            // Clear out all tombstones pretest.
            hostTombstoneListPostCrash.removeAll(hostTombstoneListPreCrash);
            guestTombstoneListPostCrash.removeAll(guestTombstoneListPreCrash);

            CLog.i("===========Host Tombstone Statistics===========");
            printTombstoneListStats(hostTombstoneListPostCrash);
            CLog.i("===========Guest Tombstone Statistics===========");
            printTombstoneListStats(guestTombstoneListPostCrash);

            Assert.assertTrue("Tombstones on guest and host do not match",
                hostTombstoneListPostCrash.containsAll(guestTombstoneListPostCrash));
            Assert.assertEquals("Host does not have expected tombstone count in this iteration",
                hostTombstoneListPostCrash.size(), NUM_TOMBSTONES_PER_LOOP);
            Assert.assertEquals("Guest does not have expected tombstone count in this iteration",
                guestTombstoneListPostCrash.size(), NUM_TOMBSTONES_PER_LOOP);
        }
    }

    public static void printTombstoneListStats(List<String> tList) {
        CLog.i("List contains %d tombstones.", tList.size());

        int averageTombstoneLength = 0;
        for(String tombstone: tList) {
            averageTombstoneLength += tombstone.length();
        }

        if(tList.size() != 0) {
            CLog.i("Average tombstone size is %d.", averageTombstoneLength / tList.size());
        }
    }

    public void clearTombstonesFromCuttlefish() throws DeviceNotAvailableException {
        if (!getDevice().isAdbRoot()) {
            throw new DeviceNotAvailableException("Device was not root, cannot collect tombstones."
                , getDevice().getSerialNumber());
        }

        // Clear all tombstones on AVD
        CommandResult commandResult = getDevice().
            executeShellV2Command("rm -rf " + TOMBSTONE_PATH + "*");
        Assert.assertEquals(CommandStatus.SUCCESS, commandResult.getStatus());
    }

    // This is blatantly copied from tradefed class NativeDevice's version of getTombstones
    private List<File> getTombstonesViaAdb() throws DeviceNotAvailableException {
        List<File> tombstones = new ArrayList<>();
        if (!getDevice().isAdbRoot()) {
            throw new DeviceNotAvailableException("Device was not root, cannot collect tombstones."
                , getDevice().getSerialNumber());
        }

        for (String tombName : getDevice().getChildren(TOMBSTONE_PATH)) {
            File tombFile = getDevice().pullFile(TOMBSTONE_PATH + tombName);
            if (tombFile != null) {
                tombstones.add(tombFile);
            }
        }
        return tombstones;
    }

    private List<String> convertFileListToStringList(List<File> inputList) throws IOException {
        List<String> output = new ArrayList<String>();
        for(File f: inputList) {
            output.add(convertFileContentsToString(f));
        }

        return output;
    }

    private String convertFileContentsToString(File f) throws IOException {
        StringBuilder stringBuilder = new StringBuilder();
        BufferedReader br = new BufferedReader(new InputStreamReader(new FileInputStream(f)));
        String line;
        while ((line = br.readLine()) != null) {
            stringBuilder.append(line).append('\n');
        }

        return stringBuilder.toString();
    }

    private void generateTombstoneOfLengthInKb(int requestedLengthInKb) throws DeviceNotAvailableException {
        if (!getDevice().isAdbRoot()) {
            throw new DeviceNotAvailableException("Device was not root, cannot generate tombstone."
                , getDevice().getSerialNumber());
        }

        // Generate file in directory not monitored by tombstone daemon and then link it into the
        // tombstone dir.
        // Context - tombstones are created in a tmp dir and then linked into the tombstones
        // dir. The tombstone daemon waits for the link inotify event and then copies
        // the full contents of the linked file to the host.
        // If the file is instead being written into the tombstones dir on the guest, the integrity
        // of the file written out on the host side cannot be guaranteed.
        CommandResult commandResult = getDevice().
            executeShellV2Command("dd if=/dev/urandom of=/data/tmp-file bs=1K count=" +
                requestedLengthInKb);
        Assert.assertEquals(CommandStatus.SUCCESS, commandResult.getStatus());

        commandResult = getDevice().
            executeShellV2Command("mv /data/tmp-file /data/tombstones/" +
                System.currentTimeMillis());
        Assert.assertEquals(CommandStatus.SUCCESS, commandResult.getStatus());
    }
}
